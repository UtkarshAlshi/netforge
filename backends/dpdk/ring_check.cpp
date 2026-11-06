// Real DPDK EAL/ethdev/ring-PMD acceptance, using only process-local packets.
#include "../xdp/fixture.h"
#include "netforge/appliance.hpp"
#include "policy_binding.hpp"
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_eth_ring.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_lcore.h>
#include <rte_ring.h>
#include <rte_version.h>
#include <sched.h>
#include <array>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Resources {
    rte_ring *input = nullptr, *output = nullptr;
    rte_mempool* pool = nullptr;
    int port = -1;
    bool started = false;
    ~Resources() {
        if (started) (void)rte_eth_dev_stop(static_cast<uint16_t>(port));
        if (port >= 0) (void)rte_eth_dev_close(static_cast<uint16_t>(port));
        for (auto* ring : {input, output}) {
            if (!ring) continue;
            void* object = nullptr;
            while (rte_ring_dequeue(ring, &object) == 0) rte_pktmbuf_free(static_cast<rte_mbuf*>(object));
            rte_ring_free(ring);
        }
        if (pool) rte_mempool_free(pool);
    }
};
struct Counts { unsigned received = 0, routed = 0, dropped = 0, punted = 0, sent = 0, unsent = 0, reference_checks = 0; };

static void udp_checksum(unsigned char* packet) {
    nf_put16(packet + 40, 0);
    unsigned sum = 17 + 30;
    for (int i = 0; i < 4; ++i) sum += nf_u16(packet + 26 + 2 * i);
    for (int i = 0; i < 15; ++i) sum += nf_u16(packet + 34 + 2 * i);
    sum = (sum & 65535) + (sum >> 16); sum = (sum & 65535) + (sum >> 16);
    auto value = static_cast<__u16>(~sum); nf_put16(packet + 40, value ? value : 65535);
}

static void batch(Resources& resources, const nf_policy& policy, unsigned first, unsigned count,
                  bool pressure, Counts& counters, netforge::Appliance& reference_engine) {
    std::array<std::array<unsigned char, 64>, 32> expected{};
    std::array<rte_mbuf*, 32> received{}, transmit{};
    unsigned expected_count = 0;
    for (unsigned index = 0; index < count; ++index) {
        unsigned trial = first + index, length = 64;
        std::array<unsigned char, 64> bytes{}; nf_fixture_packet(bytes.data());
        if (!pressure) {
            if (trial == 1) { nf_put16(bytes.data() + 36, 9001); udp_checksum(bytes.data()); }
            if (trial == 2) bytes[24] ^= 1;
            if (trial == 3 || trial == 4) {
                if (trial == 3) bytes[22] = 1;
                else nf_put16(bytes.data() + 20, 0x2000);
                nf_put16(bytes.data() + 24, 0); nf_put16(bytes.data() + 24, nf_ip_checksum(bytes.data() + 14));
            }
            if (trial == 5) nf_put16(bytes.data() + 12, 0x0806);
            if (trial >= 6 && trial < 56) length = 14 + trial - 6;
            if (trial >= 56) {
                unsigned random = (trial - 56) * 1664525U + 1013904223U;
                unsigned offset = (random >> 16) % 64;
                bytes[offset] ^= static_cast<unsigned char>((random >> 24) | 1U);
                if (trial & 1U) {
                    nf_put16(bytes.data() + 24, 0); nf_put16(bytes.data() + 24, nf_ip_checksum(bytes.data() + 14));
                }
            }
        }
        if (pressure) { nf_put16(bytes.data() + 34, static_cast<__u16>(40000 + index)); udp_checksum(bytes.data()); }
        auto reference = bytes; unsigned target = 0;
        int action = nf_process(reference.data(), reference.data() + length, &policy, 1, &target);
        if (!pressure && trial < 6) {
            const int known[] = {NF_ROUTE, NF_DROP, NF_DROP, NF_PASS, NF_PASS, NF_PASS};
            require(action == known[trial], "known policy fixture decision");
        }
        // Compare only the declared common feature subset. TTL/fragment/ARP
        // punts intentionally differ from the reference appliance's full stack.
        if (pressure || trial <= 2) {
            auto decision = reference_engine.process("lan", bytes, counters.reference_checks);
            if (action == NF_ROUTE)
                require(decision.egress == "wan" && decision.frame.size() == length &&
                        std::memcmp(decision.frame.data(), reference.data(), length) == 0,
                        "independent C++ appliance route byte parity");
            else require(decision.egress.empty(), "independent C++ appliance deny parity");
            ++counters.reference_checks;
        }
        if (action == NF_ROUTE) expected[expected_count++] = reference;
        auto* packet = rte_pktmbuf_alloc(resources.pool);
        require(packet != nullptr, "bounded mbuf allocation");
        void* data = rte_pktmbuf_append(packet, static_cast<uint16_t>(length));
        if (!data) { rte_pktmbuf_free(packet); throw std::runtime_error("mbuf append"); }
        std::memcpy(data, bytes.data(), length);
        if (rte_ring_enqueue(resources.input, packet)) {
            rte_pktmbuf_free(packet); throw std::runtime_error("bounded input ring full");
        }
    }
    uint16_t got = rte_eth_rx_burst(static_cast<uint16_t>(resources.port), 0, received.data(), static_cast<uint16_t>(count));
    struct OwnedPackets {
        std::array<rte_mbuf*, 32> packets;
        ~OwnedPackets() { for (auto* packet : packets) if (packet) rte_pktmbuf_free(packet); }
        void release(rte_mbuf* packet) { for (auto& item : packets) if (item == packet) item = nullptr; }
    } owned{received};
    require(got == count, "ring-PMD RX count");
    unsigned candidates = 0;
    for (unsigned index = 0; index < got; ++index) {
        auto* packet = received[index]; ++counters.received;
        if (packet->nb_segs != 1) throw std::runtime_error("unexpected multisegment packet");
        auto* data = rte_pktmbuf_mtod(packet, unsigned char*); unsigned target = 0;
        int action = nf_process(data, data + rte_pktmbuf_data_len(packet), &policy, 1, &target);
        if (action == NF_ROUTE) { transmit[candidates++] = packet; ++counters.routed; }
        else {
            if (action == NF_DROP) ++counters.dropped; else ++counters.punted;
            // This offline backend accounts punts; it has no downstream network stack.
            owned.release(packet); rte_pktmbuf_free(packet);
        }
    }
    require(candidates == expected_count, "candidate decision parity");
    uint16_t sent = rte_eth_tx_burst(static_cast<uint16_t>(resources.port), 0, transmit.data(), static_cast<uint16_t>(candidates));
    counters.sent += sent; counters.unsent += candidates - sent;
    for (unsigned index = 0; index < sent; ++index) owned.release(transmit[index]);
    for (unsigned index = sent; index < candidates; ++index) {
        owned.release(transmit[index]); rte_pktmbuf_free(transmit[index]);
    }
    require(pressure ? sent == 7 && candidates == 16 : sent == candidates, "partial TX handling");
    for (unsigned index = 0; index < sent; ++index) {
        void* object = nullptr;
        require(rte_ring_dequeue(resources.output, &object) == 0, "TX output ownership");
        auto* packet = static_cast<rte_mbuf*>(object);
        bool equal = rte_pktmbuf_pkt_len(packet) == 64 &&
                     std::memcmp(rte_pktmbuf_mtod(packet, void*), expected[index].data(), 64) == 0;
        rte_pktmbuf_free(packet); require(equal, "DPDK output byte parity");
    }
    require(rte_ring_empty(resources.input) && rte_ring_empty(resources.output), "rings drained");
    require(rte_mempool_avail_count(resources.pool) == 255, "all mbufs reclaimed");
}

#include "pcap_service.hpp"

int main(int argc, char** argv) {
    bool initialized = false, completed = true;
    try {
        require(argc == 3 || (argc == 7 && std::string(argv[3]) == "--pcap"),
                "Usage: nf-dpdk-ring CONFIG POLICY.bin [--pcap INPUT.pcap ROUTED.pcap PUNTED.pcap]");
        std::unique_ptr<nf_capture::Reader> capture;
        if (argc == 7) capture = std::make_unique<nf_capture::Reader>(argv[4]);
        netforge::Config config = netforge::load_config(argv[1]);
        netforge::Appliance reference_engine(config);
        nf_policy policy{};
        std::ifstream input(argv[2], std::ios::binary);
        input.read(reinterpret_cast<char*>(&policy), sizeof(policy));
        require(input.gcount() == static_cast<std::streamsize>(sizeof(policy)) &&
                input.peek() == std::char_traits<char>::eof() && !input.bad(), "compiled policy ABI size/read");
        netforge::dpdk::validate_binding(config, policy);
        // The synthetic corpus has a fixed fixture; external capture mode accepts
        // all supported compiler rules/routes rather than imposing that fixture.
        if (!capture) require(policy.rule_count == 1 && policy.route_count == 1 && policy.rules[0].ingress == 1 &&
                policy.routes[0].ifindex == 2 && !policy.default_allow, "compiled acceptance policy bindings");
        cpu_set_t cpus; CPU_ZERO(&cpus);
        require(sched_getaffinity(0, sizeof(cpus), &cpus) == 0, "read allowed CPU affinity");
        int cpu = 0; while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &cpus)) ++cpu;
        require(cpu < CPU_SETSIZE, "no allowed CPU");
        std::string cpu_text = std::to_string(cpu);
        const char* values[] = {"netforge-dpdk", "-l", cpu_text.c_str(), "-m", "32", "--no-huge",
                                "--no-pci", "--no-shconf", "--no-telemetry", "--log-level=warning"};
        std::array<char*, 11> arguments{};
        for (unsigned index = 0; index < 10; ++index) arguments[index] = const_cast<char*>(values[index]);
        require(rte_eal_init(10, arguments.data()) >= 0, "DPDK EAL initialization");
        initialized = true;
        {
            Resources resources;
            resources.input = rte_ring_create("nf_input", 64, SOCKET_ID_ANY, RING_F_SP_ENQ | RING_F_SC_DEQ);
            resources.output = rte_ring_create("nf_output", 8, SOCKET_ID_ANY, RING_F_SP_ENQ | RING_F_SC_DEQ);
            require(resources.input && resources.output, "bounded ring creation");
            resources.pool = rte_pktmbuf_pool_create("nf_pool", 255, 0, 0, 2048, SOCKET_ID_ANY);
            require(resources.pool != nullptr, "bounded mempool creation");
            resources.port = rte_eth_from_rings("net_ring_nf", &resources.input, 1, &resources.output, 1, rte_socket_id());
            require(resources.port >= 0, "actual ring-PMD ethdev creation");
            rte_eth_conf configuration{};
            auto port = static_cast<uint16_t>(resources.port);
            require(rte_eth_dev_configure(port, 1, 1, &configuration) == 0, "ethdev configuration");
            require(rte_eth_rx_queue_setup(port, 0, 64, rte_socket_id(), nullptr, resources.pool) == 0, "RX queue setup");
            require(rte_eth_tx_queue_setup(port, 0, 8, rte_socket_id(), nullptr) == 0, "TX queue setup");
            require(rte_eth_dev_start(port) == 0, "ethdev start"); resources.started = true;
            if (capture) completed = nf_capture::process(resources, policy, *capture, argv[5], argv[6]);
            else {
            Counts counts;
            for (unsigned first = 0; first < 312; first += 4) batch(resources, policy, first, 4, false, counts, reference_engine);
            std::cout << "PASS: " << rte_version() << " ring-PMD RX/TX, 312 policy corpus cases and byte parity\n";
            batch(resources, policy, 0, 16, true, counts, reference_engine);
            require(counts.received == 328 && counts.unsent == 9 && counts.reference_checks == 19, "packet/partial TX accounting");
            std::cout << "PASS: compiled project config and 19 independent C++ appliance route/deny comparisons\n";
            std::cout << "PASS: forced partial TX sent=7 unsent=9; all 255 mbufs reclaimed after every batch\n";
            std::cout << "{\"received\":" << counts.received << ",\"routed\":" << counts.routed
                      << ",\"dropped\":" << counts.dropped << ",\"punted\":" << counts.punted
                      << ",\"sent\":" << counts.sent << ",\"unsent\":" << counts.unsent << "}\n";
            }

        }
        require(rte_eal_cleanup() == 0, "EAL cleanup"); initialized = false;
        std::cout << "PASS: DPDK stopped/closed, rings/pool released and EAL cleaned up; no NIC/hugepages\n";
        if (!completed) return 130;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (initialized) (void)rte_eal_cleanup();
        return 1;
    }
}
