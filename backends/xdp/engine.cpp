// Single-ingress generic-XDP engine implementing the existing Agent check/run protocol.
#include "compile_policy.hpp"
#include "counters.h"
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/if_link.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <unistd.h>
#include <poll.h>
#include <csignal>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <type_traits>

namespace {
volatile std::sig_atomic_t stopping = 0, reloading = 0, reporting = 0;
void signal_handler(int signal) {
    if (signal == SIGHUP) reloading = 1;
    else if (signal == SIGUSR1) reporting = 1;
    else stopping = 1;
}
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct CloseObject { void operator()(bpf_object* object) const { if (object) bpf_object__close(object); } };
struct Candidate {
    netforge::Config config;
    std::map<std::string, unsigned> bindings;
    nf_policy policy{};
    std::string ingress;
    unsigned index = 0;
};
Candidate compile(const char* path) {
    Candidate value; value.config = netforge::load_config(path);
    const char* ingress = std::getenv("NETFORGE_XDP_INGRESS"); value.ingress = ingress ? ingress : "lan";
    std::ifstream forwarding("/proc/sys/net/ipv4/ip_forward"); unsigned enabled = 1;
    require(bool(forwarding >> enabled) && !enabled, "XDP engine requires kernel IPv4 forwarding disabled");
    int socket = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0); require(socket >= 0, "interface inspection socket");
    struct SocketCloser { int fd; ~SocketCloser() { ::close(fd); } } closer{socket};
    for (const auto& port : value.config.ports) {
        require(!port.device.empty() && port.device.size() < IFNAMSIZ, "invalid configured device");
        unsigned index = if_nametoindex(port.device.c_str()); require(index != 0, "configured interface absent");
        ifreq request{}; std::memcpy(request.ifr_name, port.device.c_str(), port.device.size());
        require(ioctl(socket, SIOCGIFHWADDR, &request) == 0 &&
                std::memcmp(request.ifr_hwaddr.sa_data, port.mac.data(), 6) == 0,
                "configured interface MAC mismatch");
        value.bindings.emplace(port.id, index);
    }
    value.policy = netforge::xdp::compile_policy(value.config, value.ingress, value.bindings);
    value.index = value.bindings.at(value.ingress);
    return value;
}
bool compatible(const Candidate& old, const Candidate& next) {
    if (next.config.generation <= old.config.generation || old.bindings != next.bindings || old.ingress != next.ingress ||
        old.config.ports.size() != next.config.ports.size()) return false;
    for (const auto& port : old.config.ports) {
        const auto* other = netforge::find_port(next.config, port.id);
        if (!other || port.device != other->device || port.ip != other->ip || port.mac != other->mac ||
            port.subnet.network != other->subnet.network || port.subnet.length != other->subnet.length || port.mtu != other->mtu)
            return false;
    }
    return true;
}
struct Loaded {
    Candidate value;
    std::unique_ptr<bpf_object, CloseObject> object;
    int program = -1, counters = -1;
    __u32 id = 0;
    Loaded(Candidate candidate, const char* object_path) : value(std::move(candidate)) {
        auto* opened = bpf_object__open_file(object_path, nullptr);
        require(opened && !libbpf_get_error(opened), "BPF object open failed"); object.reset(opened);
        require(bpf_object__load(object.get()) == 0, "BPF verifier/load failed");
        auto* entry = bpf_object__find_program_by_name(object.get(), "nf_xdp");
        int map = bpf_object__find_map_fd_by_name(object.get(), "nf_policy_map");
        counters = bpf_object__find_map_fd_by_name(object.get(), "nf_decisions");
        require(entry && map >= 0 && counters >= 0, "missing BPF program/maps");
        unsigned key = 0;
        require(bpf_map_update_elem(map, &key, &value.policy, BPF_ANY) == 0 && bpf_map_freeze(map) == 0,
                "policy map initialization/freeze failed");
        program = bpf_program__fd(entry); bpf_prog_info info{}; __u32 length = sizeof(info);
        require(bpf_obj_get_info_by_fd(program, &info, &length) == 0 && info.id, "BPF program identity unavailable");
        id = info.id;
    }
};
__u32 attached_id(unsigned index) {
    __u32 id = 0; require(bpf_xdp_query_id(static_cast<int>(index), XDP_FLAGS_SKB_MODE, &id) == 0, "XDP ownership query failed");
    return id;
}
void report(const Loaded& current) {
    require(attached_id(current.value.index) == current.id, "attached XDP program ownership changed");
    int cpus = libbpf_num_possible_cpus(); require(cpus > 0 && cpus <= 4096, "CPU count exceeds counter read bound");
    std::vector<nf_xdp_counter> values(static_cast<std::size_t>(cpus));
    std::cout << "netforge_config_generation " << current.value.config.generation << '\n'
              << "netforge_xdp_program_id " << current.id << '\n';
    for (unsigned action : {NF_DROP, NF_PASS, NF_ROUTE}) {
        require(bpf_map_lookup_elem(current.counters, &action, values.data()) == 0, "kernel counter snapshot failed");
        unsigned long long packets = 0, bytes = 0;
        for (const auto& value : values) { packets += value.packets; bytes += value.bytes; }
        const std::string labels = "{program=\"" + std::to_string(current.id) + "\",generation=\"" +
            std::to_string(current.value.config.generation) + "\",action=\"" + std::to_string(action) + "\"} ";
        std::cout << "netforge_xdp_decision_packets_total" << labels << packets << '\n'
                  << "netforge_xdp_decision_bytes_total" << labels << bytes << '\n';
    }
    std::cout << "netforge_metrics_snapshot_complete 1\n" << std::flush;
}
}
int main(int argc, char** argv) {
    std::unique_ptr<Loaded> current;
    bool attached = false; int status = 0;
    try {
        require(argc == 3 && (std::string(argv[1]) == "check" || std::string(argv[1]) == "run"),
                "Usage: nf-xdp-engine check|run CONFIG");
        auto initial = compile(argv[2]);
        if (std::string(argv[1]) == "check") {
            std::cout << "{\"valid\":true,\"generation\":" << initial.config.generation << "}\n"; return 0;
        }
        const char* object = std::getenv("NETFORGE_XDP_OBJECT"); require(object && *object, "NETFORGE_XDP_OBJECT required");
        require(attached_id(initial.index) == 0, "refusing pre-existing XDP program");
        for (int signal : {SIGTERM, SIGINT, SIGHUP, SIGUSR1}) std::signal(signal, signal_handler);
        current = std::make_unique<Loaded>(std::move(initial), object);
        require(bpf_xdp_attach(static_cast<int>(current->value.index), current->program,
                              XDP_FLAGS_SKB_MODE | XDP_FLAGS_UPDATE_IF_NOEXIST, nullptr) == 0, "initial XDP attach failed");
        attached = true;
        std::cout << "{\"event\":\"started\",\"generation\":" << current->value.config.generation << "}\n" << std::flush;
        while (!stopping) {
            if (reloading) {
                reloading = 0;
                bool published = false;
                try {
                    auto next_config = compile(argv[2]);
                    require(compatible(current->value, next_config), "interface identity change or stale generation");
                    auto next = std::make_unique<Loaded>(std::move(next_config), object);
                    bpf_xdp_attach_opts options{}; options.sz = sizeof(options); options.old_prog_fd = current->program;
                    require(bpf_xdp_attach(static_cast<int>(current->value.index), next->program,
                                          XDP_FLAGS_SKB_MODE | XDP_FLAGS_REPLACE, &options) == 0,
                            "owned XDP replacement rejected");
                    published = true;
                    // No allocation/fallible work after kernel publication and before updating local ownership.
                    current.swap(next);
                    std::cout << "{\"event\":\"config_published\",\"generation\":" << current->value.config.generation << "}\n" << std::flush;
                } catch (const std::exception&) {
                    if (published || attached_id(current->value.index) != current->id)
                        throw std::runtime_error("XDP publication/ownership uncertain");
                    std::cout << "{\"event\":\"config_rejected\"}\n" << std::flush;
                }
            }
            if (reporting) { reporting = 0; report(*current); }
            int result = poll(nullptr, 0, 100);
            require(result == 0 || errno == EINTR, "control wait failed");
        }
    } catch (const std::exception& error) { std::cerr << "XDP engine stopped: " << error.what() << '\n'; status = 1; }
    if (attached) {
        bpf_xdp_attach_opts options{}; options.sz = sizeof(options); options.old_prog_fd = current->program;
        __u32 remaining = 0;
        if (bpf_xdp_detach(static_cast<int>(current->value.index), XDP_FLAGS_SKB_MODE, &options) ||
            bpf_xdp_query_id(static_cast<int>(current->value.index), XDP_FLAGS_SKB_MODE, &remaining) || remaining != 0) {
            std::cerr << "XDP owned detach/absence verification failed\n"; status = 1;
        } else std::cout << "{\"event\":\"stopped\"}\n" << std::flush;
    }
    return status;
}
