#include "netforge/appliance.hpp"
#include "netforge/observability.hpp"
#include "netforge/workers.hpp"
#include <stdexcept>

#ifndef __linux__
namespace netforge {
int run_linux(const std::string&, unsigned) {
    throw std::runtime_error("live AF_PACKET mode requires Linux; use check/replay on this host");
}
}
#else
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ifaddrs.h>

namespace netforge {
namespace {
volatile std::sig_atomic_t stopping = 0, reloading = 0, reporting = 0;
void signal_handler(int signal) {
    if (signal == SIGHUP) reloading = 1;
    else if (signal == SIGUSR1) reporting = 1;
    else stopping = 1;
}
struct Descriptor {
    int fd = -1;
    explicit Descriptor(int value) : fd(value) {}
    ~Descriptor() { if (fd >= 0) ::close(fd); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) noexcept : fd(other.fd) { other.fd = -1; }
};
struct WorkerSignalMask {
    sigset_t original{}; bool active = false;
    explicit WorkerSignalMask(bool enabled) {
        if (!enabled) return;
        sigset_t blocked; sigemptyset(&blocked);
        for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGUSR1}) sigaddset(&blocked, signal);
        if (pthread_sigmask(SIG_BLOCK, &blocked, &original)) throw std::runtime_error("worker signal mask setup failed");
        active = true;
    }
    void restore() {
        if (active && pthread_sigmask(SIG_SETMASK, &original, nullptr)) throw std::runtime_error("main signal mask restore failed");
        active = false;
    }
    ~WorkerSignalMask() { if (active) (void)pthread_sigmask(SIG_SETMASK, &original, nullptr); }
};
std::uint64_t milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void preflight(const Config& config) {
    int forwarding = -1; std::ifstream("/proc/sys/net/ipv4/ip_forward") >> forwarding;
    if (forwarding != 0) throw std::runtime_error("disable kernel forwarding in this dedicated lab namespace first");
    ifaddrs* addresses = nullptr;
    if (getifaddrs(&addresses) != 0) throw std::runtime_error("cannot inspect interface addresses");
    bool conflict = false;
    for (auto a = addresses; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        auto ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
        for (const auto& p : config.ports) if (p.ip == ip) conflict = true;
        for (const auto& s : config.services) if (s.vip == ip) conflict = true;
    }
    freeifaddrs(addresses);
    if (conflict) throw std::runtime_error("kernel owns a configured appliance IP; use unnumbered dedicated lab interfaces");
}
}
int run_linux(const std::string& path, unsigned worker_count) {
    auto config = load_config(path); preflight(config);
    // Workers inherit blocked control signals; only the socket/main thread
    // handles them after installing handlers and restoring its original mask.
    WorkerSignalMask worker_signals(worker_count != 0);
    std::unique_ptr<Appliance> appliance;
    std::unique_ptr<WorkerPool> workers;
    if (worker_count) workers = std::make_unique<WorkerPool>(config, worker_count);
    else appliance = std::make_unique<Appliance>(config);
    std::array<std::uint64_t, 2> worker_results{}, worker_emitted{}, worker_dropped{};
    std::array<std::uint64_t, 2> worker_unsupported{};
    std::uint64_t worker_ipv6 = 0;
    std::vector<Descriptor> descriptors;
    std::vector<pollfd> polls;
    std::vector<std::string> ids;
    for (const auto& port : config.ports) {
        const auto index = if_nametoindex(port.device.c_str());
        if (index == 0) throw std::runtime_error("interface not found: " + port.device);
        int fd = ::socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(0x0003));
        if (fd < 0) throw std::runtime_error("packet socket requires CAP_NET_RAW: " + std::string(std::strerror(errno)));
        descriptors.emplace_back(fd);
        ifreq request{};
        std::copy(port.device.begin(), port.device.end(), request.ifr_name);
        if (ioctl(fd, SIOCGIFHWADDR, &request) != 0 ||
            !std::equal(port.mac.begin(), port.mac.end(), reinterpret_cast<unsigned char*>(request.ifr_hwaddr.sa_data)))
            throw std::runtime_error("configured MAC does not match interface " + port.device);
        sockaddr_ll binding{}; binding.sll_family = AF_PACKET;
        binding.sll_protocol = htons(0x0003); binding.sll_ifindex = static_cast<int>(index);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&binding), sizeof(binding)) < 0)
            throw std::runtime_error("packet bind failed");
        polls.push_back({fd, POLLIN, 0}); ids.push_back(port.id);
    }
    stopping = 0; reloading = 0; reporting = 0;
    struct sigaction action{}; action.sa_handler = signal_handler; sigemptyset(&action.sa_mask);
    for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGUSR1})
        if (sigaction(signal, &action, nullptr) < 0) throw std::runtime_error("signal setup failed");
    worker_signals.restore();
    std::uint64_t tx_failures = 0, tx_sent = 0, rx_oversize = 0;
    std::vector<std::uint8_t> buffer(65549);
    std::vector<std::pair<std::string, PortCounters>> port_counts;
    for (const auto& id : ids) port_counts.push_back({id, {}});
    std::uint64_t kernel_packets = 0, kernel_drops = 0, kernel_stat_errors = 0;
    unsigned kernel_available_ports = 0;
    bool kernel_ever_available = false;
    auto sample_kernel_stats = [&] {
        kernel_available_ports = 0;
        for (const auto& descriptor : descriptors) {
            tpacket_stats stats{}; socklen_t length = sizeof(stats);
            // PACKET_STATISTICS resets kernel counters on read; accumulate deltas.
            if (getsockopt(descriptor.fd, SOL_PACKET, PACKET_STATISTICS, &stats, &length) == 0 && length == sizeof(stats)) {
                kernel_packets += stats.tp_packets; kernel_drops += stats.tp_drops;
                ++kernel_available_ports; kernel_ever_available = true;
            } else ++kernel_stat_errors;
        }
    };
    const auto started = milliseconds();
    std::vector<WorkerMaintenance> worker_state(worker_count);
    std::uint64_t next_worker_tick = started, next_kernel_tick = started;
    auto transmit = [&](const Decision& decision) {
        if (decision.egress.empty()) return;
        auto output = std::find(ids.begin(), ids.end(), decision.egress);
        if (output == ids.end()) throw std::runtime_error("unbound egress");
        auto sent = ::send(polls[output - ids.begin()].fd, decision.frame.data(), decision.frame.size(), MSG_DONTWAIT);
        auto& counts = port_counts[output - ids.begin()].second;
        if (sent < 0 || static_cast<std::size_t>(sent) != decision.frame.size()) { ++tx_failures; ++counts.tx_failures; }
        else { ++tx_sent; ++counts.tx_packets; counts.tx_bytes += decision.frame.size(); }
    };
    auto report = [&] {
        sample_kernel_stats();
        if (appliance) std::cout << appliance->metrics();
        else {
            std::cout << "netforge_config_generation " << config.generation << '\n'
                      << "netforge_worker_count " << worker_count << '\n';
            for (unsigned owner = 0; owner < worker_count; ++owner)
                std::cout << "netforge_worker_results_total{owner=\"" << owner << "\"} " << worker_results[owner] << '\n'
                          << "netforge_worker_emitted_total{owner=\"" << owner << "\"} " << worker_emitted[owner] << '\n'
                          << "netforge_worker_dropped_total{owner=\"" << owner << "\"} " << worker_dropped[owner] << '\n'
                          << "netforge_worker_unsupported_total{owner=\"" << owner << "\"} " << worker_unsupported[owner] << '\n';
            for (unsigned owner = 0; owner < worker_count; ++owner)
                std::cout << "netforge_worker_flows{owner=\"" << owner << "\"} " << worker_state[owner].flows << '\n'
                          << "netforge_worker_flows_expired_total{owner=\"" << owner << "\"} " << worker_state[owner].expired << '\n';
            std::cout << "netforge_worker_ipv6_received_total " << worker_ipv6 << '\n';
        }
        if (kernel_ever_available)
            std::cout << "netforge_packet_socket_packets_total " << kernel_packets << '\n'
                      << "netforge_packet_socket_drops_total " << kernel_drops << '\n';
        std::cout << "netforge_packet_socket_statistics_read_errors_total " << kernel_stat_errors << '\n'
                  << "netforge_packet_socket_statistics_available_ports " << kernel_available_ports << '\n';
        std::cout << port_metrics(port_counts)
                  << "netforge_tx_sent_total " << tx_sent << '\n'
                  << "netforge_tx_failures_total " << tx_failures << '\n'
                  << "netforge_process_uptime_seconds " << (milliseconds() - started) / 1000.0 << '\n';
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            const double cpu = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
                               (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
            std::cout << "netforge_process_cpu_seconds_total " << cpu << '\n'
                      << "netforge_process_peak_rss_bytes " << static_cast<std::uint64_t>(usage.ru_maxrss) * 1024 << '\n';
        }
        std::uint64_t total_pages = 0, resident_pages = 0;
        const auto page_size = sysconf(_SC_PAGESIZE);
        if (std::ifstream("/proc/self/statm") >> total_pages >> resident_pages && page_size > 0)
            std::cout << "netforge_process_resident_bytes " << resident_pages * static_cast<std::uint64_t>(page_size) << '\n';
        // Preserve the complete-snapshot terminal marker consumed by the agent.
        std::cout << "netforge_rx_oversize_total " << rx_oversize << '\n' << std::flush;
    };
    std::cerr << "{\"event\":\"started\",\"generation\":" << (appliance ? appliance->config().generation : config.generation) << "}\n";
    while (!stopping) {
        const auto sample_time = milliseconds();
        if (sample_time >= next_kernel_tick) {
            sample_kernel_stats(); next_kernel_tick = sample_time + 1000;
        }
        if (reloading) {
            reloading = 0;
            try {
                auto next = load_config(path); preflight(next);
                if (workers) { workers->reload(next); config = std::move(next); }
                else appliance->reload(std::move(next));
                std::cerr << "{\"event\":\"config_published\",\"generation\":" << (appliance ? appliance->config().generation : config.generation) << "}\n";
            } catch (const std::exception&) {
                // Keep exception strings out of structured logs: config text can contain quotes.
                std::cerr << "{\"event\":\"config_rejected\"}\n";
            }
        }
        if (reporting) {
            reporting = 0;
            report();
        }
        if (appliance) for (const auto& decision : appliance->maintenance(milliseconds())) transmit(decision);
        if (workers && milliseconds() >= next_worker_tick) {
            const auto now = milliseconds();
            worker_state = workers->maintenance(now);
            for (const auto& owner : worker_state)
                for (const auto& decision : owner.decisions) transmit(decision);
            next_worker_tick = now + 250;
        }
        int ready = ::poll(polls.data(), polls.size(), 250);
        if (ready < 0) { if (errno == EINTR) continue; throw std::runtime_error("poll failed"); }
        std::vector<WorkerPacket> batch;
        for (std::size_t i = 0; i < polls.size(); ++i) {
            if (polls[i].revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("interface poll error");
            if (!(polls[i].revents & POLLIN)) continue;
            for (unsigned burst = 0; burst < (workers ? 16U : 1U); ++burst) {
            sockaddr_ll sender{}; socklen_t length = sizeof(sender);
            auto received = ::recvfrom(polls[i].fd, buffer.data(), buffer.size(), MSG_TRUNC,
                                      reinterpret_cast<sockaddr*>(&sender), &length);
            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
                throw std::runtime_error("receive failed");
            }
            if (sender.sll_pkttype == PACKET_OUTGOING) continue;
            if (static_cast<std::size_t>(received) > buffer.size()) { ++rx_oversize; continue; }
            if (workers && received > 9014) { ++rx_oversize; continue; }
            ++port_counts[i].second.rx_packets;
            port_counts[i].second.rx_bytes += static_cast<std::uint64_t>(received);
            if (workers) {
                if (received >= 14 && buffer[12] == 0x86 && buffer[13] == 0xdd) ++worker_ipv6;
                batch.push_back({ids[i], std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + received), milliseconds()});
            }
            else transmit(appliance->process(ids[i], std::span(buffer).first(received), milliseconds()));
            }
        }
        if (workers && !batch.empty()) {
            auto completions = workers->submit_batch(std::move(batch));
            for (auto& completion : completions) {
                auto outcome = completion.get(); ++worker_results[outcome.owner];
                if (outcome.decision.reason == "worker_protocol_unsupported") ++worker_unsupported[outcome.owner];
                if (outcome.decision.egress.empty()) ++worker_dropped[outcome.owner];
                else ++worker_emitted[outcome.owner];
                transmit(outcome.decision);
            }
        }
    }
    if (workers) workers->shutdown();
    report();
    std::cerr << "{\"event\":\"stopped\"}\n";
    return 0;
}
} // namespace netforge
#endif
