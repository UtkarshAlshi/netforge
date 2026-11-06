#include "netforge/workers.hpp"
#include "../backends/xdp/fixture.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace netforge;
static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static void checksums(std::vector<std::uint8_t>& packet) {
    nf_put16(packet.data() + 24, 0); nf_put16(packet.data() + 24, nf_ip_checksum(packet.data() + 14));
    if (packet[23] == 1) {
        nf_put16(packet.data() + 36, 0);
        nf_put16(packet.data() + 36, internet_checksum(std::span(packet).subspan(34))); return;
    }
    auto checksum_offset = packet[23] == 6 ? 50U : 40U;
    nf_put16(packet.data() + checksum_offset, 0);
    unsigned sum = packet[23] + static_cast<unsigned>(packet.size() - 34);
    for (int i = 0; i < 4; ++i) sum += nf_u16(packet.data() + 26 + 2 * i);
    for (std::size_t i = 34; i < packet.size(); i += 2)
        sum += (unsigned(packet[i]) << 8) | (i + 1 < packet.size() ? packet[i + 1] : 0);
    sum = (sum & 65535) + (sum >> 16); sum = (sum & 65535) + (sum >> 16);
    auto value = static_cast<__u16>(~sum); nf_put16(packet.data() + checksum_offset, value ? value : 65535);
}
static void put32(std::vector<std::uint8_t>& packet, unsigned offset, unsigned value) {
    for (unsigned index = 0; index < 4; ++index) packet[offset + index] = static_cast<unsigned char>(value >> (24 - 8 * index));
}
static WorkerPacket tcp(WorkerPacket packet, unsigned sequence, unsigned acknowledgement, unsigned flags) {
    auto sport = nf_u16(packet.frame.data() + 34), dport = nf_u16(packet.frame.data() + 36);
    packet.frame.resize(54); std::fill(packet.frame.begin() + 34, packet.frame.end(), 0);
    packet.frame[23] = 6; nf_put16(packet.frame.data() + 16, 40);
    nf_put16(packet.frame.data() + 34, sport); nf_put16(packet.frame.data() + 36, dport);
    put32(packet.frame, 38, sequence); put32(packet.frame, 42, acknowledgement);
    packet.frame[46] = 0x50; packet.frame[47] = static_cast<unsigned char>(flags);
    nf_put16(packet.frame.data() + 48, 4096); checksums(packet.frame); return packet;
}
static WorkerPacket request(unsigned port, std::uint64_t time = 0) {
    std::vector<std::uint8_t> packet(64); nf_fixture_packet(packet.data());
    nf_put16(packet.data() + 34, static_cast<__u16>(port)); checksums(packet);
    return {"lan", std::move(packet), time};
}
static WorkerPacket reply(const Config& config, const Decision& wire, std::uint64_t time) {
    auto packet = wire.frame;
    auto* port = find_port(config, "wan");
    std::memcpy(packet.data(), port->mac.data(), 6);
    auto peer = mac_address("02:89:00:00:00:02"); std::memcpy(packet.data() + 6, peer.data(), 6);
    const std::array<std::uint8_t, 4> source{10, 89, 0, 2}, destination{10, 89, 0, 1};
    std::memcpy(packet.data() + 26, source.data(), 4); std::memcpy(packet.data() + 30, destination.data(), 4);
    nf_put16(packet.data() + 36, nf_u16(packet.data() + 34)); nf_put16(packet.data() + 34, 9000);
    packet[22] = 64; checksums(packet);
    return {"wan", std::move(packet), time};
}
static void compare(const Decision& actual, const Decision& expected) {
    require(actual.reason == expected.reason && actual.egress == expected.egress && actual.frame == expected.frame,
            "worker and serial shard byte/decision parity");
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "requires project directory");
        auto config = load_config(std::string(argv[1]) + "/examples/xdp-veth.conf");
        config.snat = Snat{"wan", address("10.89.0.1"), 40000, 40031}; config.flow_limit = 32;
        config.routes.push_back({{address("10.88.0.2"), 32}, "lan", 0});
        config.neighbors.push_back({"lan", address("10.88.0.2"), mac_address("02:88:00:00:00:01")});
        for (std::size_t count : {1U, 2U}) {
            WorkerPool pool(config, count);
            std::vector<std::unique_ptr<Appliance>> serial;
            for (std::size_t index = 0; index < count; ++index)
                serial.push_back(std::make_unique<Appliance>(pool.worker_config(index)));
            std::vector<WorkerPacket> packets;
            for (unsigned index = 0; index < 16; ++index) packets.push_back(request(10000 + index, index));
            auto futures = pool.submit_batch(packets);
            std::vector<WorkerOutcome> forwards;
            std::set<unsigned> allocated; std::set<std::size_t> owners;
            for (unsigned index = 0; index < 16; ++index) {
                auto outcome = futures[index].get(); owners.insert(outcome.owner);
                compare(outcome.decision, serial[outcome.owner]->process("lan", packets[index].frame, index));
                require(outcome.decision.egress == "wan", "forward emits on wan");
                auto nat = nf_u16(outcome.decision.frame.data() + 34);
                require(allocated.insert(nat).second, "unique NAT allocation across workers");
                auto& shard = pool.worker_config(outcome.owner);
                require(nat >= shard.snat->first && nat <= shard.snat->last, "NAT within owner partition");
                forwards.push_back(std::move(outcome));
            }
            require(owners.size() == count, "both worker owners exercised");
            std::vector<WorkerPacket> replies;
            for (unsigned index = 0; index < 16; ++index) replies.push_back(reply(config, forwards[index].decision, 20 + index));
            auto reverse = pool.submit_batch(replies);
            for (unsigned index = 0; index < 16; ++index) {
                auto outcome = reverse[index].get();
                require(outcome.owner == forwards[index].owner, "translated reply returns to state owner");
                compare(outcome.decision, serial[outcome.owner]->process("wan", replies[index].frame, 20 + index));
                require(outcome.decision.egress == "lan" && nf_u16(outcome.decision.frame.data() + 36) == 10000 + index,
                        "reverse NAT restores original client tuple");
            }
            auto repeats = pool.submit_batch(packets);
            for (unsigned index = 0; index < 16; ++index) {
                auto outcome = repeats[index].get(); require(outcome.owner == forwards[index].owner, "stable forward ownership");
                compare(outcome.decision, serial[outcome.owner]->process("lan", packets[index].frame, index));
            }
            auto later = request(12345, 2000);
            auto expired = pool.submit_batch({later}); auto outcome = expired[0].get();
            compare(outcome.decision, serial[outcome.owner]->process("lan", later.frame, 2000));
            pool.shutdown(); pool.shutdown();
            auto closed = pool.submit_batch({request(1)});
            require(closed[0].get().decision.reason == "worker_pool_closed", "closed admission");
        }
        for (std::size_t count : {1U, 2U}) {
            WorkerPool pool(config, count);
            std::vector<WorkerPacket> packets;
            for (unsigned i = 0; i < 4; ++i) packets.push_back(request(11000 + i, 1));
            auto established = pool.submit_batch(packets);
            std::vector<WorkerOutcome> old;
            for (auto& result : established) old.push_back(result.get());
            auto denied = config; ++denied.generation; denied.rules.clear(); denied.default_allow = false;
            pool.reload(denied);
            for (std::size_t i = 0; i < count; ++i)
                require(pool.worker_config(i).generation == denied.generation, "all owners publish same generation");
            auto kept = pool.submit_batch(packets);
            for (unsigned i = 0; i < 4; ++i) {
                auto result = kept[i].get();
                require(result.owner == old[i].owner, "reload retains state owner");
                require(result.decision.reason == "tracked_udp", "reload keeps pinned established flows");
                require(result.decision.frame == old[i].decision.frame, "reload preserves NAT bytes");
            }
            auto fresh = pool.submit_batch({request(12000, 2), request(12001, 2)});
            for (auto& future : fresh) require(future.get().decision.reason == "acl_deny", "new flows use published deny");
            auto invalid = denied; ++invalid.generation; ++invalid.flow_limit;
            bool rejected = false; try { pool.reload(invalid); } catch (const std::exception&) { rejected = true; }
            require(rejected, "incompatible worker update rejected");
            for (std::size_t i = 0; i < count; ++i)
                require(pool.worker_config(i).generation == denied.generation, "rejection keeps all owner generations");
            invalid = denied; ++invalid.generation; ++invalid.snat->first;
            rejected = false; try { pool.reload(invalid); } catch (const std::exception&) { rejected = true; }
            require(rejected, "PAT repartition requires restart");
            auto still = pool.submit_batch(packets);
            for (auto& future : still) require(future.get().decision.reason == "tracked_udp", "rejected reload retains flows");
        }
        {
            WorkerPool pool(config, 2);
            auto revision = config;
            WorkerPacket pinned;
            unsigned port = 17000;
            for (unsigned generation = 1; generation <= 8; ++generation) {
                if (generation > 1) { ++revision.generation; pool.reload(revision); }
                WorkerPacket packet;
                for (;;) {
                    packet = request(port++, 1);
                    std::uint64_t hash = 1469598103934665603ULL;
                    hash ^= 17; hash *= 1099511628211ULL;
                    for (std::size_t offset = 26; offset < 38; ++offset) { hash ^= packet.frame[offset]; hash *= 1099511628211ULL; }
                    if (hash % 2 == 1) break;
                }
                auto results = pool.submit_batch({packet}); auto outcome = results[0].get();
                require(outcome.owner == 1 && outcome.decision.reason == "new_udp_mapping", "pin generations only on second owner");
                pinned = packet;
            }
            auto rejected_config = revision; ++rejected_config.generation;
            bool rejected = false; try { pool.reload(rejected_config); } catch (const std::exception&) { rejected = true; }
            require(rejected, "second owner rejects ninth pinned generation after first owner stages successfully");
            require(pool.worker_config(0).generation == revision.generation && pool.worker_config(1).generation == revision.generation,
                    "late staging rejection never partially publishes first owner");
            auto results = pool.submit_batch({pinned});
            require(results[0].get().decision.reason == "tracked_udp", "late rejection preserves second-owner flow state");
        }
        for (std::size_t count : {1U, 2U}) {
            auto idle_config = config; idle_config.udp_timeout_ms = 100;
            WorkerPool idle(idle_config, count);
            auto admitted = idle.submit_batch({request(10000, 10), request(10001, 10)});
            for (auto& future : admitted) require(future.get().decision.egress == "wan", "idle test admits flows");
            auto active = idle.maintenance(50);
            std::size_t flows = 0; std::uint64_t expired = 0;
            for (const auto& state : active) { flows += state.flows; expired += state.expired; }
            require(flows == 2 && expired == 0, "maintenance preserves unexpired owner flows");
            auto empty = idle.maintenance(111); flows = 0; expired = 0;
            for (const auto& state : empty) {
                flows += state.flows; expired += state.expired;
                require(state.decisions.empty(), "static neighbor idle tick emits no traffic");
            }
            require(flows == 0 && expired == 2, "idle tick expires all owners without packet processing");
            auto repeated = idle.maintenance(112);
            expired = 0; for (const auto& state : repeated) expired += state.expired;
            require(expired == 2, "idle expiry counters do not double count");
            auto reused = idle.submit_batch({request(10000, 113), request(10001, 113)});
            for (auto& future : reused) require(future.get().decision.reason == "new_udp_mapping", "expired PAT is reusable");
            idle.shutdown(); bool closed = false;
            try { (void)idle.maintenance(200); } catch (const std::exception&) { closed = true; }
            require(closed, "maintenance refuses stopped pool");
        }
        {
            WorkerPool bounded(config, 2, 2);
            auto results = bounded.submit_batch({request(1), request(1), request(1), request(1)});
            unsigned rejected = 0;
            for (auto& future : results) if (future.get().decision.reason == "worker_queue_full") ++rejected;
            require(rejected == 2, "deterministic bounded queue backpressure");
            auto drained = bounded.submit_batch({request(2), request(2)});
            bounded.shutdown(); for (auto& future : drained) require(future.get().decision.egress == "wan", "shutdown drains admitted jobs");
        }
        {
            auto small = config; small.flow_limit = 2; small.snat->last = 40001;
            WorkerPool exhausted(small, 2);
            std::vector<WorkerPacket> burst;
            for (unsigned index = 0; index < 16; ++index) burst.push_back(request(10000 + index));
            auto results = exhausted.submit_batch(std::move(burst)); unsigned emitted = 0;
            for (auto& future : results) if (!future.get().decision.egress.empty()) ++emitted;
            require(emitted == 2, "aggregate flow/PAT bound across workers");
        }
        {
            WorkerPool pool(config, 2);
            auto oversized = request(1); oversized.frame.resize(9015);
            auto results = pool.submit_batch({oversized}); require(results[0].get().decision.reason == "worker_input_invalid", "frame memory bound");
            auto unsupported = request(1); unsupported.frame[23] = 47; checksums(unsupported.frame);
            results = pool.submit_batch({unsupported}); require(results[0].get().decision.reason == "worker_protocol_unsupported", "unsupported protocol rejected");
            bool rejected = false;
            try { (void)pool.submit_batch(std::vector<WorkerPacket>(65, request(1))); } catch (const std::exception&) { rejected = true; }
            require(rejected, "batch bound");
        }
        {
            WorkerPool pool(config, 2, 64); std::atomic<bool> good{true};
            std::array<std::thread, 4> producers;
            for (unsigned index = 0; index < producers.size(); ++index)
                producers[index] = std::thread([&, index] {
                    try {
                        std::size_t owner = 2;
                        for (unsigned turn = 0; turn < 32; ++turn) {
                            auto results = pool.submit_batch({request(10000 + index, turn)});
                            auto result = results[0].get();
                            if (owner == 2) owner = result.owner;
                            if (result.owner != owner || result.decision.egress != "wan") good = false;
                        }
                    } catch (...) { good = false; }
                });
            std::thread maintainer([&] {
                try {
                    for (unsigned tick = 0; tick < 32; ++tick) {
                        auto states = pool.maintenance(100);
                        if (states.size() != 2) good = false;
                    }
                } catch (...) { good = false; }
            });
            std::thread publisher([&] {
                try {
                    auto next = config;
                    for (unsigned revision = 0; revision < 4; ++revision) { ++next.generation; pool.reload(next); }
                } catch (...) { good = false; }
            });
            for (auto& producer : producers) producer.join();
            maintainer.join(); publisher.join();
            require(good.load(), "concurrent producers, maintenance and reload preserve owner/completion");
        }
        for (std::size_t count : {1U, 2U}) {
            auto advanced = config; advanced.tcp_stateful = true;
            advanced.rules.front().destination = {0, 0}; // ACL matches original service VIP tuple.
            auto rule = advanced.rules.front(); rule.protocol = 6; advanced.rules.push_back(rule);
            Service service{"lan", address("10.88.0.100"), 9000, {{address("10.89.0.2"), 9000}}};
            advanced.services.push_back(service); service.protocol = 6; advanced.services.push_back(service);
            WorkerPool pool(advanced, count);
            std::vector<std::unique_ptr<Appliance>> serial;
            for (std::size_t owner = 0; owner < count; ++owner) serial.push_back(std::make_unique<Appliance>(pool.worker_config(owner)));
            auto step = [&](WorkerPacket packet) {
                auto futures = pool.submit_batch({packet}); auto outcome = futures[0].get();
                compare(outcome.decision, serial[outcome.owner]->process(packet.ingress, packet.frame, packet.now_ms));
                return outcome;
            };
            std::set<std::size_t> tcp_owners, udp_owners;
            for (unsigned index = 0; index < 4; ++index) {
                auto udp = request(15000 + index, index); put32(udp.frame, 30, address("10.88.0.100")); checksums(udp.frame);
                auto udp_out = step(udp); udp_owners.insert(udp_out.owner);
                require(udp_out.decision.egress == "wan", "worker UDP service translation");
                auto udp_back = step(reply(advanced, udp_out.decision, 10 + index));
                require(udp_back.owner == udp_out.owner && nf_u32(udp_back.decision.frame.data() + 26) == service.vip,
                        "UDP service reverse translation and owner");
                auto error = reply(advanced, udp_out.decision, 20 + index);
                error.frame.resize(70); error.frame[23] = 1; nf_put16(error.frame.data() + 16, 56);
                std::fill(error.frame.begin() + 34, error.frame.end(), 0); error.frame[34] = 3; error.frame[35] = 3;
                std::copy_n(udp_out.decision.frame.begin() + 14, 28, error.frame.begin() + 42); checksums(error.frame);
                auto related = step(error);
                require(related.owner == udp_out.owner && related.decision.reason == "related_icmp", "quoted NAT port dispatches related ICMP");
                auto client = request(16000 + index, 30 + index);
                put32(client.frame, 30, service.vip); checksums(client.frame);
                auto syn_packet = tcp(client, 100, 0, 2); auto syn = step(syn_packet); tcp_owners.insert(syn.owner);
                require(syn.decision.reason == "new_tcp_mapping", "TCP service SYN mapping");
                auto repeated = step(syn_packet); require(repeated.owner == syn.owner && repeated.decision.reason == "tracked_tcp", "SYN retransmission owner");
                auto server = reply(advanced, syn.decision, 40 + index);
                auto bad = step(tcp(server, 700, 999, 0x12));
                require(bad.owner == syn.owner && bad.decision.reason == "tcp_state_invalid", "bad SYNACK stays on owner and cannot advance");
                auto synack = step(tcp(server, 700, 101, 0x12));
                require(synack.owner == syn.owner && nf_u32(synack.decision.frame.data() + 26) == service.vip,
                        "TCP reverse service translation and owner");
                client.now_ms = 50 + index;
                require(step(tcp(client, 101, 701, 0x10)).decision.reason == "tracked_tcp", "TCP established ACK");
                client.now_ms = 60 + index;
                require(step(tcp(client, 101, 701, 0x11)).decision.reason == "tracked_tcp", "TCP FIN owned");
                server.now_ms = 70 + index;
                require(step(tcp(server, 701, 102, 0x11)).decision.reason == "tracked_tcp", "reverse FIN owned");
                client.now_ms = 80 + index;
                require(step(tcp(client, 102, 702, 0x10)).decision.reason == "tracked_tcp", "TCP closing ACK");
            }
            require(udp_owners.size() == count && tcp_owners.size() == count, "both service protocol owners exercised");
            auto least = advanced; least.services[0].scheduler = Scheduler::least_connections;
            bool rejected = false; try { WorkerPool unsupported_pool(least, count); } catch (const std::exception&) { rejected = true; }
            require(rejected, "global least-connections unsupported rather than silently sharded");
        }
        bool rejected = false; auto unsupported = config; unsupported.neighbor_discovery = true;
        try { WorkerPool pool(unsupported, 2); } catch (const std::exception&) { rejected = true; }
        require(rejected, "unsupported stateful configuration rejected");
        std::cout << "PASS: 1/2-worker UDP/TCP/service/related-ICMP ownership, serial parity, transactional reload, idle maintenance/expiry, bounds and draining shutdown\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
