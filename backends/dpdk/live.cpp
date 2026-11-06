// Explicit two-port AF_PACKET PMD service; no PCI devices, hugepages or NAT.
#include "policy_binding.hpp"
#include <rte_bus_vdev.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <sched.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
static volatile std::sig_atomic_t stopped = 0;
static void stop(int) { stopped = 1; }
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Devices {
    std::array<bool, 2> created{}, started{};
    std::array<uint16_t, 2> ports{};
    rte_mempool* pool = nullptr;
    ~Devices() {
        for (unsigned i = 0; i < 2; ++i) {
            if (started[i]) (void)rte_eth_dev_stop(ports[i]);
            if (created[i]) (void)rte_vdev_uninit(i ? "net_af_packet1" : "net_af_packet0");
        }
        if (pool) rte_mempool_free(pool);
    }
};
static bool interface_name(const std::string& value) {
    if (value.empty() || value.size() > 15) return false;
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    return true;
}
int main(int argc, char** argv) {
    bool initialized = false;
    try {
        require(argc == 6, "Usage: nf-dpdk-live CONFIG POLICY IN_IFACE OUT_IFACE SECONDS(1..300)");
        const std::string in = argv[3], out = argv[4], duration = argv[5];
        require(interface_name(in) && interface_name(out) && in != out, "invalid explicit interface names");
        require(!duration.empty() && duration.size() <= 3 && duration.find_first_not_of("0123456789") == std::string::npos, "invalid duration");
        const unsigned seconds = static_cast<unsigned>(std::stoul(duration));
        require(seconds >= 1 && seconds <= 300, "duration must be1..300 seconds");
        auto config = netforge::load_config(argv[1]); nf_policy policy{};
        std::ifstream input(argv[2], std::ios::binary); input.read(reinterpret_cast<char*>(&policy), sizeof(policy));
        require(input.gcount() == static_cast<std::streamsize>(sizeof(policy)) && input.peek() == std::char_traits<char>::eof() && !input.bad(), "policy ABI read");
        netforge::dpdk::validate_binding(config, policy);
        require(netforge::find_port(config, "lan")->device == in && netforge::find_port(config, "wan")->device == out,
                "interface arguments must match checked configuration");
        cpu_set_t cpus; CPU_ZERO(&cpus); require(sched_getaffinity(0, sizeof(cpus), &cpus) == 0, "allowed CPU affinity");
        int cpu = 0; while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &cpus)) ++cpu;
        require(cpu < CPU_SETSIZE, "no allowed CPU"); const auto cpu_text = std::to_string(cpu);
        const char* values[] = {"netforge-dpdk-live", "-l", cpu_text.c_str(), "-m", "32", "--no-huge", "--no-pci", "--no-shconf", "--no-telemetry", "--log-level=warning"};
        std::array<char*, 11> arguments{};
        for (unsigned i = 0; i < 10; ++i) arguments[i] = const_cast<char*>(values[i]);
        require(rte_eal_init(10, arguments.data()) >= 0, "EAL initialization"); initialized = true;
        uint64_t received = 0, routed = 0, denied = 0, exceptions = 0, sent = 0, unsent = 0;
        {
            Devices devices;
            devices.pool = rte_pktmbuf_pool_create("nf_live_pool", 255, 0, 0, 2048, SOCKET_ID_ANY);
            require(devices.pool, "bounded live pool creation");
            for (unsigned i = 0; i < 2; ++i) {
                const char* name = i ? "net_af_packet1" : "net_af_packet0";
                const auto options = "iface=" + (i ? out : in) + ",qpairs=1,blocksz=4096,framesz=2048,framecnt=256,qdisc_bypass=0";
                require(rte_vdev_init(name, options.c_str()) == 0, "AF_PACKET PMD initialization"); devices.created[i] = true;
                require(rte_eth_dev_get_port_by_name(name, &devices.ports[i]) == 0, "AF_PACKET port lookup");
                rte_eth_conf configuration{};
                require(rte_eth_dev_configure(devices.ports[i], 1, 1, &configuration) == 0, "live port configure");
                require(rte_eth_rx_queue_setup(devices.ports[i], 0, 128, rte_socket_id(), nullptr, devices.pool) == 0, "live RX queue");
                require(rte_eth_tx_queue_setup(devices.ports[i], 0, 128, rte_socket_id(), nullptr) == 0, "live TX queue");
                require(rte_eth_dev_start(devices.ports[i]) == 0, "live port start"); devices.started[i] = true;
            }
            stopped = 0; std::signal(SIGTERM, stop); std::signal(SIGINT, stop);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            std::cout << "{\"event\":\"dpdk_live_started\",\"generation\":" << config.generation << "}\n" << std::flush;
            while (!stopped && std::chrono::steady_clock::now() < deadline) {
                std::array<rte_mbuf*, 32> packets{}, transmit{};
                const unsigned count = rte_eth_rx_burst(devices.ports[0], 0, packets.data(), 32);
                if (!count) { usleep(1000); continue; }
                unsigned candidates = 0;
                for (unsigned i = 0; i < count; ++i) {
                    auto* packet = packets[i]; ++received;
                    if (packet->nb_segs != 1 || rte_pktmbuf_pkt_len(packet) > 1514) { ++denied; rte_pktmbuf_free(packet); continue; }
                    auto* bytes = rte_pktmbuf_mtod(packet, unsigned char*); unsigned target = 0;
                    const int action = nf_process(bytes, bytes + rte_pktmbuf_data_len(packet), &policy, 1, &target);
                    if (action == NF_ROUTE && target == 2) { transmit[candidates++] = packet; ++routed; }
                    else { if (action == NF_DROP) ++denied; else ++exceptions; rte_pktmbuf_free(packet); }
                }
                const unsigned accepted = rte_eth_tx_burst(devices.ports[1], 0, transmit.data(), static_cast<uint16_t>(candidates));
                sent += accepted; unsent += candidates - accepted;
                for (unsigned i = accepted; i < candidates; ++i) rte_pktmbuf_free(transmit[i]);
            }
            require(received == routed + denied + exceptions && routed == sent + unsent, "live counters inconsistent");
            // AF_PACKET PMD copies successful sends into PACKET_MMAP and frees mbufs.
            require(rte_mempool_avail_count(devices.pool) == 255, "live mbuf ownership leak");
        }
        require(rte_eal_cleanup() == 0, "live EAL cleanup"); initialized = false;
        std::cout << "{\"mode\":\"af_packet_pmd\",\"received\":" << received << ",\"routed\":" << routed
                  << ",\"dropped\":" << denied << ",\"exceptions_discarded\":" << exceptions << ",\"sent\":" << sent
                  << ",\"unsent\":" << unsent << ",\"pool_reclaimed\":255}\n";
        std::cout << "PASS: live DPDK ports stopped, PMDs removed, mbufs reclaimed and EAL cleaned up\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (initialized) (void)rte_eal_cleanup();
        return 1;
    }
}
