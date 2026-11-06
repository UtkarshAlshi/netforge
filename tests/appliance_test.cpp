#include "netforge/appliance.hpp"
#include "netforge/observability.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace netforge;
namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool yes, const char* what) {
    if (!yes) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}
std::uint16_t read16(const Bytes& b, std::size_t i) { return (unsigned(b[i]) << 8) | b[i + 1]; }
Address read32(const Bytes& b, std::size_t i) {
    return (Address(b[i]) << 24) | (Address(b[i + 1]) << 16) | (Address(b[i + 2]) << 8) | b[i + 3];
}
void put16(Bytes& b, std::size_t i, unsigned value) { b[i] = value >> 8; b[i + 1] = value & 255; }
void put32(Bytes& b, std::size_t i, Address value) {
    b[i] = value >> 24; b[i + 1] = (value >> 16) & 255; b[i + 2] = (value >> 8) & 255; b[i + 3] = value & 255;
}
// Independent word-at-a-time test oracle, using a wider accumulator.
unsigned reference(std::span<const std::uint8_t> bytes) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 2)
        sum += (unsigned(bytes[i]) << 8) | (i + 1 < bytes.size() ? bytes[i + 1] : 0);
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (~sum) & 65535;
}
unsigned udp_reference(const Bytes& b) {
    Bytes pseudo(12);
    std::copy_n(b.begin() + 26, 8, pseudo.begin());
    pseudo[9] = b[23]; put16(pseudo, 10, b.size() - 34);
    pseudo.insert(pseudo.end(), b.begin() + 34, b.end());
    return reference(pseudo);
}
void set_ip_checksum(Bytes& b) { put16(b, 24, 0); put16(b, 24, reference(std::span(b).subspan(14, 20))); }
Bytes udp(const Config& c, const std::string& ingress, Address source, Address dest,
          unsigned sport = 12345, unsigned dport = 9000, unsigned ttl = 64) {
    Bytes b(45, 0);
    auto p = find_port(c, ingress);
    std::copy(p->mac.begin(), p->mac.end(), b.begin());
    const auto peer = mac_address(ingress == "lan" ? "02:00:00:00:00:0a" : "02:00:00:00:00:0b");
    std::copy(peer.begin(), peer.end(), b.begin() + 6);
    put16(b, 12, 0x0800); b[14] = 0x45; put16(b, 16, b.size() - 14);
    b[22] = ttl; b[23] = 17; put32(b, 26, source); put32(b, 30, dest);
    put16(b, 34, sport); put16(b, 36, dport); put16(b, 38, b.size() - 34);
    b[42] = 0x12; b[43] = 0x34; b[44] = 0x56;
    auto checksum = udp_reference(b); put16(b, 40, checksum == 0 ? 65535 : checksum);
    set_ip_checksum(b); return b;
}
Bytes icmp(const Config& c, Address destination, unsigned ttl = 64) {
    auto b = udp(c, "lan", address("10.77.0.2"), destination, 0, 0, ttl);
    b[23] = 1; std::fill(b.begin() + 34, b.end(), 0); b[34] = 8;
    put16(b, 36, reference(std::span(b).subspan(34))); set_ip_checksum(b); return b;
}
Bytes tcp_packet(const Config& c, const std::string& ingress, Address source, Address destination,
                 unsigned sport, unsigned dport, unsigned sequence, unsigned acknowledgement,
                 unsigned flags, std::size_t data_length = 0) {
    auto packet = udp(c, ingress, source, destination, sport, dport);
    packet.resize(54 + data_length, 0);
    std::fill(packet.begin() + 34, packet.end(), 0);
    packet[23] = 6; put16(packet, 16, packet.size() - 14);
    put16(packet, 34, sport); put16(packet, 36, dport);
    put32(packet, 38, sequence); put32(packet, 42, acknowledgement);
    packet[46] = 0x50; packet[47] = flags; put16(packet, 48, 4096);
    put16(packet, 50, udp_reference(packet)); set_ip_checksum(packet); return packet;
}
void check_udp(const Decision& d) {
    require(!d.egress.empty(), "expected emission");
    require(reference(std::span(d.frame).subspan(14, 20)) == 0, "independent IP checksum");
    require(udp_reference(d.frame) == 0, "independent UDP checksum");
}
template<class Function> void throws(Function function, const char* message) {
    try { function(); } catch (const std::exception&) { return; }
    require(false, message);
}
}
int main(int argc, char** argv) {
    require(argc == 2, "supply example config path");
    const auto config = load_config(argv[1]);
    const auto a = address("10.77.0.2"), b = address("10.77.0.130"), vip = address("10.77.0.100");
    const auto external = address("10.77.0.254");
    require(address_text(a) == "10.77.0.2", "address roundtrip");
    throws([] { address("1.2.3.4."); }, "reject trailing dot");
    throws([] { address("1.2.3.256"); }, "reject octet overflow");
    throws([] { mac_address("ff:gg:00:00:00:00"); }, "reject invalid MAC");
    auto routes = config;
    routes.routes.push_back({{b, 32}, "lan", a});
    require(lookup_route(routes, b)->prefix.length == 32, "longest prefix wins");
    require(lookup_route(config, address("192.0.2.1")) == nullptr, "no route");
    auto invalid = config; invalid.routes.push_back(invalid.routes[0]);
    throws([&] { validate(invalid); }, "duplicate route must reject");

    {
        auto routing = config; routing.snat.reset(); routing.services.clear();
        Appliance router(routing);
        auto disabled = udp(routing, "lan", a, b); put16(disabled, 40, 0);
        for (unsigned turn = 0; turn < 2; ++turn) {
            auto forwarded = router.process("lan", disabled, turn);
            require(forwarded.egress == "wan" && read16(forwarded.frame, 40) == 0,
                    "pure UDP routing preserves absent checksum on new and tracked flow");
            require(read32(forwarded.frame, 26) == a && read32(forwarded.frame, 30) == b &&
                    read16(forwarded.frame, 34) == 12345 && read16(forwarded.frame, 36) == 9000 &&
                    forwarded.frame[22] == 63 && reference(std::span(forwarded.frame).subspan(14, 20)) == 0,
                    "untranslated UDP keeps tuple while repairing TTL/IP checksum");
        }
        auto enabled = udp(routing, "lan", a, b, 12346);
        auto forwarded = router.process("lan", enabled, 2);
        require(forwarded.egress == "wan" && read16(forwarded.frame, 40) == read16(enabled, 40),
                "pure routing preserves enabled UDP checksum too");
        Appliance translating(config);
        auto nat = translating.process("lan", disabled, 0);
        require(nat.egress == "wan" && read16(nat.frame, 40) != 0 && udp_reference(nat.frame) == 0,
                "NAT retains checksum repair for an input with absent UDP checksum");
    }

    // Forward and reverse UDP SNAT with independent checksum verification.
    Appliance appliance(config);
    auto request = udp(config, "lan", a, b);
    auto out = appliance.process("lan", request, 0);
    require(out.reason == "new_udp_mapping" && out.egress == "wan", "SNAT emission");
    check_udp(out);
    require(out.frame[22] == 63 && read32(out.frame, 26) == external && read16(out.frame, 34) == 40000,
            "SNAT source address, port and TTL");
    require(std::equal(config.ports[1].mac.begin(), config.ports[1].mac.end(), out.frame.begin() + 6),
            "egress MAC rewrite");
    auto reply = udp(config, "wan", b, external, 9000, 40000);
    auto back = appliance.process("wan", reply, 1); check_udp(back);
    require(back.egress == "lan" && read32(back.frame, 30) == a && read16(back.frame, 36) == 12345,
            "reverse SNAT restores client");
    require(appliance.process("lan", request, 2).reason == "tracked_udp", "flow reuse");
    require(appliance.flow_count() == 1, "no duplicate state");
    require(appliance.counters().received_bytes == request.size() * 3 &&
            appliance.counters().emitted_bytes == request.size() * 3,
            "engine byte accounting covers every processing/emission");
    require(appliance.counters().acl_evaluations == 1 && appliance.counters().acl_matches == 1 &&
            appliance.counters().route_lookups == 3 && appliance.counters().route_misses == 0,
            "tracked packets bypass ACL evaluation but still route");
    require(appliance.metrics().find("netforge_flows_high_water 1\n") != std::string::npos,
            "flow occupancy high water retained");
    auto wrong_ingress = reply;
    std::copy(config.ports[0].mac.begin(), config.ports[0].mac.end(), wrong_ingress.begin());
    require(appliance.process("lan", wrong_ingress, 3).reason == "acl_deny", "wrong-ingress reply is not stateful");
    auto unsolicited = udp(config, "wan", b, external, 9000, 40001);
    require(appliance.process("wan", unsolicited, 4).reason == "acl_deny", "unsolicited return denied");

    auto blocked = request; put16(blocked, 36, 22); put16(blocked, 40, 0);
    require(appliance.process("lan", blocked, 5).reason == "acl_deny", "default deny");
    require(appliance.process("missing", request, 6).reason == "unknown_ingress", "unknown ingress");
    auto corrupt = request; corrupt[24] ^= 1;
    require(appliance.process("lan", corrupt, 7).reason == "ipv4_invalid", "bad IP checksum");
    corrupt = request; corrupt.back() ^= 1;
    require(appliance.process("lan", corrupt, 8).reason == "udp_checksum", "bad UDP checksum");
    corrupt = request; put16(corrupt, 38, 8);
    require(appliance.process("lan", corrupt, 9).reason == "udp_length", "UDP length mismatch");
    corrupt = request; put16(corrupt, 20, 0x2000); set_ip_checksum(corrupt);
    require(appliance.process("lan", corrupt, 10).reason == "fragment_unsupported", "fragments explicitly rejected");
    corrupt = request; corrupt[0] ^= 1;
    require(appliance.process("lan", corrupt, 11).reason == "not_for_port", "wrong destination MAC");
    auto missing = config; missing.neighbors.clear(); Appliance unresolved(missing);
    require(unresolved.process("lan", request, 0).reason == "neighbor_missing", "missing neighbor drops");
    require(unresolved.flow_count() == 0, "failed forwarding must not commit NAT state");
    auto ordered = config; auto deny = ordered.rules[0]; deny.allow = false;
    ordered.rules.insert(ordered.rules.begin(), deny); Appliance ordered_acl(ordered);
    require(ordered_acl.process("lan", request, 0).reason == "acl_deny", "first matching ACL wins");

    auto small_mtu = config; small_mtu.ports[1].mtu = 68; Appliance mtu_router(small_mtu);
    auto oversized = request; oversized.resize(142, 0); put16(oversized, 16, 128);
    put16(oversized, 38, 108); put16(oversized, 40, 0); put16(oversized, 20, 0x4000); set_ip_checksum(oversized);
    auto mtu_error = mtu_router.process("lan", oversized, 0);
    require(mtu_error.reason == "mtu_exceeded" && mtu_error.frame[34] == 3 && mtu_error.frame[35] == 4 &&
            read16(mtu_error.frame, 40) == 68, "DF packet gets fragmentation-needed with next-hop MTU");
    require(mtu_router.flow_count() == 0, "oversize packet does not commit flow state");
    put16(oversized, 20, 0); set_ip_checksum(oversized);
    require(mtu_router.process("lan", oversized, 1).reason == "fragmentation_required_unsupported", "no silent oversized forwarding");

    auto tcp_config = config; tcp_config.default_allow = true; Appliance tcp_router(tcp_config);
    auto tcp = request; tcp.resize(54, 0); std::fill(tcp.begin() + 34, tcp.end(), 0);
    put16(tcp, 16, 40); tcp[23] = 6; put16(tcp, 34, 1234); put16(tcp, 36, 80);
    tcp[46] = 0x50; tcp[47] = 2; put16(tcp, 50, udp_reference(tcp)); set_ip_checksum(tcp);
    auto tcp_forwarded = tcp_router.process("lan", tcp, 0);
    require(tcp_forwarded.reason == "forward" && tcp_forwarded.frame[22] == 63 &&
            udp_reference(tcp_forwarded.frame) == 0, "stateless TCP forwarding validates checksum");
    require(tcp_router.flow_count() == 0, "do not claim TCP connection tracking");
    tcp[46] = 0x40;
    require(tcp_router.process("lan", tcp, 1).reason == "tcp_length", "reject invalid TCP header length");

    // TCP tracking is opt-in, preserving the previous stateless mode.
    auto stateful = config; stateful.tcp_stateful = true;
    auto tcp_rule = stateful.rules[0]; tcp_rule.protocol = 6; stateful.rules.push_back(tcp_rule);
    Appliance tcp_nat(stateful);
    auto syn = tcp_packet(stateful, "lan", a, b, 12345, 9000, 100, 0, 2);
    auto syn_out = tcp_nat.process("lan", syn, 0);
    require(syn_out.reason == "new_tcp_mapping" && read32(syn_out.frame, 26) == external &&
            read16(syn_out.frame, 34) == 40000 && udp_reference(syn_out.frame) == 0, "TCP SNAT SYN checksum and mapping");
    require(tcp_nat.process("lan", syn, 1).reason == "tracked_tcp", "SYN retransmission keeps mapping");
    auto bad_syn_ack = tcp_packet(stateful, "wan", b, external, 9000, 40000, 900, 999, 0x12);
    require(tcp_nat.process("wan", bad_syn_ack, 2).reason == "tcp_state_invalid", "bad handshake cannot advance state");
    auto syn_ack = tcp_packet(stateful, "wan", b, external, 9000, 40000, 900, 101, 0x12);
    auto reverse_syn = tcp_nat.process("wan", syn_ack, 3);
    require(reverse_syn.reason == "tracked_tcp" && read32(reverse_syn.frame, 30) == a &&
            read16(reverse_syn.frame, 36) == 12345 && udp_reference(reverse_syn.frame) == 0,
            "TCP reverse NAT on SYNACK");
    auto ack_packet = tcp_packet(stateful, "lan", a, b, 12345, 9000, 101, 901, 0x10);
    require(tcp_nat.process("lan", ack_packet, 4).reason == "tracked_tcp", "TCP handshake established");
    require(tcp_nat.process("wan", syn_ack, 5).reason == "tracked_tcp", "SYNACK retransmission after observed ACK");
    auto data_packet = tcp_packet(stateful, "lan", a, b, 12345, 9000, 101, 901, 0x18, 3);
    auto data_out = tcp_nat.process("lan", data_packet, 6);
    require(data_out.reason == "tracked_tcp" && udp_reference(data_out.frame) == 0, "translated TCP data checksum");
    auto orphan_ack = tcp_packet(stateful, "lan", a, b, 12346, 9000, 101, 901, 0x10);
    require(tcp_nat.process("lan", orphan_ack, 7).reason == "tcp_requires_syn", "no midstream state creation");
    // Protocol is part of state identity; identical UDP tuple is an independent mapping.
    require(tcp_nat.process("lan", request, 8).reason == "new_udp_mapping", "TCP and UDP state isolated");
    require(tcp_nat.flow_count() == 2, "one TCP and one UDP mapping");
    auto fin_packet = tcp_packet(stateful, "lan", a, b, 12345, 9000, 104, 901, 0x11);
    require(tcp_nat.process("lan", fin_packet, 9).reason == "tracked_tcp", "TCP FIN tracked");
    tcp_nat.process("wan", tcp_packet(stateful, "wan", b, external, 9000, 40000, 901, 105, 0x11), 10);
    tcp_nat.process("lan", tcp_packet(stateful, "lan", a, b, 12345, 9000, 105, 902, 0x10), 11);
    tcp_nat.process("lan", {}, 5011);
    require(tcp_nat.flow_count() == 1, "TCP closing expiry leaves unrelated UDP mapping");

    Appliance tcp_reset(stateful);
    tcp_reset.process("lan", syn, 0);
    auto rst = tcp_packet(stateful, "wan", b, external, 9000, 40000, 0, 101, 0x14);
    require(tcp_reset.process("wan", rst, 1).reason == "tracked_tcp", "refused connection RST translated");
    require(tcp_reset.process("lan", syn, 2).reason == "tcp_state_invalid", "RST quarantine prevents immediate tuple reuse");
    require(tcp_reset.process("lan", syn, 5001).reason == "new_tcp_mapping", "RST quarantine expires");
    Appliance syn_timeout(stateful); syn_timeout.process("lan", syn, 0);
    syn_timeout.process("lan", {}, 10000);
    require(syn_timeout.flow_count() == 0, "half-open TCP expires");

    auto tcp_service_config = stateful;
    auto tcp_service = stateful.services[0]; tcp_service.protocol = 6;
    tcp_service_config.services.push_back(tcp_service); Appliance tcp_lb(tcp_service_config);
    auto vip_syn = tcp_packet(stateful, "lan", a, vip, 23456, 9000, 100, 0, 2);
    auto vip_out = tcp_lb.process("lan", vip_syn, 0);
    require(vip_out.reason == "new_tcp_mapping" && read32(vip_out.frame, 30) == b, "TCP service first backend");
    require(read32(tcp_lb.process("lan", tcp_packet(stateful, "lan", a, vip, 23457, 9000, 200, 0, 2), 1).frame, 30)
            == address("10.77.0.131"), "TCP service round robin per connection");
    auto vip_reply = tcp_lb.process("wan", syn_ack, 2);
    require(read32(vip_reply.frame, 26) == vip && read16(vip_reply.frame, 36) == 23456 &&
            udp_reference(vip_reply.frame) == 0, "TCP reverse service translation");
    auto tcp_next = stateful; tcp_next.generation = 2; tcp_next.rules.clear(); tcp_nat.reload(tcp_next);
    require(tcp_nat.config().generation == 2, "TCP-capable snapshot reload");
    tcp_next.generation = 3; tcp_next.tcp_stateful = false;
    throws([&] { tcp_nat.reload(tcp_next); }, "TCP mode change requires restart");

    // Established TCP keeps its old generation while new connections obey denial.
    Appliance tcp_reload(stateful);
    tcp_reload.process("lan", syn, 0);
    tcp_reload.process("wan", syn_ack, 1);
    auto ack = ack_packet;
    require(tcp_reload.process("lan", ack, 2).reason == "tracked_tcp", "establish before reload");
    auto deny_tcp = stateful; deny_tcp.generation = 2; deny_tcp.rules.clear();
    tcp_reload.reload(deny_tcp);
    require(tcp_reload.process("lan", ack, 3).reason == "tracked_tcp", "established TCP survives deny reload");
    require(tcp_reload.process("lan", tcp_packet(stateful, "lan", a, b, 23457, 9000, 1, 0, 2), 4).reason == "acl_deny",
            "new TCP obeys new generation");

    // Per-flow service choice, inverse translation, and round robin.
    Appliance lb(config);
    auto first = lb.process("lan", udp(config, "lan", a, vip, 1111), 0); check_udp(first);
    auto second = lb.process("lan", udp(config, "lan", a, vip, 2222), 1); check_udp(second);
    require(read32(first.frame, 30) == b && read32(second.frame, 30) == address("10.77.0.131"),
            "round robin across new flows");
    auto pinned = lb.process("lan", udp(config, "lan", a, vip, 1111), 2);
    require(read32(pinned.frame, 30) == b, "service flow remains pinned");
    auto lb_reply = lb.process("wan", udp(config, "wan", b, external, 9000, 40000), 3);
    check_udp(lb_reply);
    require(read32(lb_reply.frame, 26) == vip && read32(lb_reply.frame, 30) == a &&
            read16(lb_reply.frame, 36) == 1111, "reverse service translation");

    const auto backend_label = "{generation=\"1\",service=\"0\",backend=\"0\"} ";
    require(lb.metrics().find(std::string("netforge_backend_new_flows") + backend_label + "1\n") != std::string::npos &&
            lb.metrics().find(std::string("netforge_backend_forward_bytes") + backend_label + std::to_string(first.frame.size() + pinned.frame.size()) + "\n") != std::string::npos &&
            lb.metrics().find(std::string("netforge_backend_reverse_bytes") + backend_label + std::to_string(lb_reply.frame.size()) + "\n") != std::string::npos,
            "backend new-flow and bidirectional emitted-byte attribution");
    auto next_backend = config; next_backend.generation = 2;
    Appliance backend_reload(config); backend_reload.process("lan", udp(config, "lan", a, vip, 1111), 0);
    backend_reload.reload(next_backend);
    backend_reload.process("lan", udp(config, "lan", a, vip, 1111), 1);
    require(backend_reload.metrics().find("netforge_backend_forward_bytes{generation=\"2\",service=\"0\",backend=\"0\"} 0\n") != std::string::npos,
            "old-generation flow bytes never relabelled to current backend");
    // Scheduler decisions participate in the real NAT/state pipeline.
    auto weighted_config = config;
    weighted_config.services[0].scheduler = Scheduler::weighted_round_robin;
    weighted_config.services[0].backends[0].weight = 3;
    Appliance weighted_lb(weighted_config);
    for (unsigned i = 0; i < 4; ++i) {
        auto decision = weighted_lb.process("lan", udp(config, "lan", a, vip, 6000 + i), i);
        check_udp(decision);
        require(read32(decision.frame, 30) == (i < 3 ? b : address("10.77.0.131")),
                "weighted scheduler integrated with NAT and checksum pipeline");
    }
    require(read32(weighted_lb.process("lan", udp(config, "lan", a, vip, 6000), 5).frame, 30) == b,
            "scheduler is not rerun for an established mapping");

    // Drain affects only new selections; pinned connections keep the old backend.
    auto draining = config; draining.generation = 2; draining.services[0].backends[0].draining = true;
    lb.reload(draining);
    require(read32(lb.process("lan", udp(config, "lan", a, vip, 1111), 4).frame, 30) == b,
            "draining preserves established affinity");
    require(read32(lb.process("lan", udp(config, "lan", a, vip, 3333), 5).frame, 30) == address("10.77.0.131"),
            "new connections skip draining backend");
    draining.generation = 3; draining.services[0].backends[1].draining = true; lb.reload(draining);
    require(lb.process("lan", udp(config, "lan", a, vip, 4444), 6).reason == "service_unavailable",
            "all draining fails closed");

    // Recovery changes new selection only; an established mapping stays pinned.
    draining.generation = 4; draining.services[0].backends[0].draining = false;
    lb.reload(draining);
    require(read32(lb.process("lan", udp(config, "lan", a, vip, 4444), 7).frame, 30) == b,
            "backend recovery admits previously denied new flow");
    require(read32(lb.process("lan", udp(config, "lan", a, vip, 3333), 8).frame, 30) == address("10.77.0.131"),
            "recovery preserves flow pinned to still-drained backend");

    // Related ICMP restores the original tuple and all quoted checksums.
    auto make_error = [&](const Bytes& emitted, std::size_t quote_length) {
        auto packet = udp(config, "wan", b, external);
        packet.resize(42 + quote_length, 0); packet[23] = 1;
        put16(packet, 16, packet.size() - 14); std::fill(packet.begin() + 34, packet.end(), 0);
        packet[34] = 3; packet[35] = 4; put16(packet, 40, 1200);
        std::copy_n(emitted.begin() + 14, quote_length, packet.begin() + 42);
        put16(packet, 36, reference(std::span(packet).subspan(34))); set_ip_checksum(packet);
        return packet;
    };
    Appliance related(config);
    auto service_packet = related.process("lan", udp(config, "lan", a, vip, 5555), 0);
    auto related_error = related.process("wan", make_error(service_packet.frame, 31), 1);
    require(related_error.reason == "related_icmp" && related_error.egress == "lan", "related ICMP bypasses default deny");
    require(read32(related_error.frame, 30) == a && read32(related_error.frame, 26) == vip &&
            read32(related_error.frame, 54) == a && read32(related_error.frame, 58) == vip &&
            read16(related_error.frame, 62) == 5555 && read16(related_error.frame, 64) == 9000,
            "ICMP outer and quoted NAT restored");
    require(reference(std::span(related_error.frame).subspan(34)) == 0 &&
            reference(std::span(related_error.frame).subspan(42, 20)) == 0 &&
            reference(std::span(related_error.frame).subspan(14, 20)) == 0 && read16(related_error.frame, 40) == 1200,
            "ICMP/IP checksum and path MTU preserved");
    Bytes quoted(related_error.frame.begin() + 28, related_error.frame.end());
    require(udp_reference(quoted) == 0, "quoted full UDP checksum repaired independently");
    require(related.process("wan", make_error(service_packet.frame, 28), 2).reason == "related_icmp",
            "minimum truncated quote supported");
    require(related.process("wan", make_error(service_packet.frame, 27), 3).reason == "icmp_quote_invalid",
            "truncated quote rejected");
    auto wrong = make_error(service_packet.frame, 28); put16(wrong, 62, 40003);
    put16(wrong, 36, 0); put16(wrong, 36, reference(std::span(wrong).subspan(34)));
    require(related.process("wan", wrong, 4).reason == "icmp_unrelated", "forged unmapped quote rejected");
    require(related.process("wan", make_error(service_packet.frame, 28), 30000).reason == "icmp_unrelated",
            "errors do not refresh mapping lifetime");

    // Every undersized transport quote must fail closed without refreshing state.
    Appliance quote_bounds(config);
    auto quoted_mapping = quote_bounds.process("lan", udp(config, "lan", a, vip, 5555), 0);
    for (std::size_t length = 0; length < 28; ++length)
        require(quote_bounds.process("wan", make_error(quoted_mapping.frame, length), length + 1).reason == "icmp_quote_invalid",
                "all short ICMP quotes rejected");
    auto corrupt_error = make_error(quoted_mapping.frame, 28);
    corrupt_error[36] ^= 1;
    require(quote_bounds.process("wan", corrupt_error, 30).reason == "icmp_invalid", "bad outer ICMP checksum rejected");
    auto corrupt_quote = make_error(quoted_mapping.frame, 28);
    corrupt_quote[50] ^= 1; // Quoted IPv4 TTL; repair only outer ICMP checksum.
    put16(corrupt_quote, 36, 0); put16(corrupt_quote, 36, reference(std::span(corrupt_quote).subspan(34)));
    require(quote_bounds.process("wan", corrupt_quote, 31).reason == "icmp_quote_invalid", "bad quoted IP checksum rejected");
    quote_bounds.maintenance(30000);
    require(quote_bounds.flow_count() == 0 && quote_bounds.counters().expired_flows == 1,
            "invalid ICMP burst never refreshes or duplicates mapping");

    Appliance related_tcp(stateful);
    auto tcp_emitted = related_tcp.process("lan", syn, 0);
    auto tcp_error = related_tcp.process("wan", make_error(tcp_emitted.frame, 40), 1);
    require(tcp_error.reason == "related_icmp", "TCP related ICMP supported");
    Bytes quoted_tcp(tcp_error.frame.begin() + 28, tcp_error.frame.end());
    require(udp_reference(quoted_tcp) == 0 && read16(tcp_error.frame, 62) == 12345,
            "quoted TCP pseudoheader and port checksum repaired");
    require(related_tcp.process("wan", make_error(tcp_emitted.frame, 28), 2).reason == "related_icmp",
            "TCP minimum eight transport bytes accepted without checksum field");
    auto zero_quote = make_error(service_packet.frame, 31); put16(zero_quote, 68, 0);
    put16(zero_quote, 36, 0); put16(zero_quote, 36, reference(std::span(zero_quote).subspan(34)));
    Appliance zero_related(config);
    zero_related.process("lan", udp(config, "lan", a, vip, 5555), 0);
    auto zero_error = zero_related.process("wan", zero_quote, 1);
    require(zero_error.reason == "related_icmp" && read16(zero_error.frame, 68) == 0,
            "quoted UDP disabled checksum stays zero");

    // Rule diagnostics count only policy evaluation and reset on publication.
    auto diagnosed_config = config;
    auto deny_rule = config.rules.front(); deny_rule.allow = false; deny_rule.source_port = 12346;
    diagnosed_config.rules.insert(diagnosed_config.rules.begin(), deny_rule);
    Appliance diagnosed(diagnosed_config);
    diagnosed.process("lan", request, 0); diagnosed.process("lan", request, 1);
    require(diagnosed.process("lan", udp(config, "lan", a, b, 12346), 2).reason == "acl_deny", "diagnostic deny rule");
    const auto diagnostic_metrics = diagnosed.metrics();
    const auto generation_label = std::to_string(config.generation);
    require(diagnostic_metrics.find("netforge_acl_rule_checks{generation=\"" + generation_label + "\",rule=\"0\"} 2\n") != std::string::npos &&
            diagnostic_metrics.find("netforge_acl_rule_hits{generation=\"" + generation_label + "\",rule=\"0\"} 1\n") != std::string::npos &&
            diagnostic_metrics.find("netforge_acl_rule_hits{generation=\"" + generation_label + "\",rule=\"1\"} 1\n") != std::string::npos,
            "first-match diagnostics exclude established-flow bypass");
    diagnosed_config.generation += 1; diagnosed.reload(diagnosed_config);
    diagnosed.process("lan", request, 3);
    require(diagnosed.metrics().find("netforge_acl_rule_hits{generation=\"" + std::to_string(diagnosed_config.generation) + "\",rule=\"0\"} 0\n") != std::string::npos,
            "new generation resets rule diagnostics while old flow bypasses");
    diagnosed_config.rules.resize(20, deny_rule); diagnosed_config.generation += 1; diagnosed.reload(diagnosed_config);
    require(diagnosed.metrics().find("netforge_acl_rule_series_omitted 4\n") != std::string::npos,
            "rule metric cardinality explicitly capped");

    // Resource limits, port reuse only after expiry, monotonic clock handling.
    auto limited = config; limited.flow_limit = 1; Appliance full(limited);
    full.process("lan", request, 100);
    require(full.process("lan", udp(config, "lan", a, b, 12346), 101).reason == "flow_table_full", "bounded table");
    require(full.process("lan", request, 1).reason == "tracked_udp", "backward clock clamps");
    require(full.process("wan", reply, 30101).reason == "acl_deny", "expired reverse mapping denied");
    require(full.flow_count() == 0, "expired state reclaimed");
    require(full.counters().expired_flows == 1, "expiry event counted once");
    Appliance idle_expiry(config);
    idle_expiry.process("lan", request, 0);
    require(idle_expiry.maintenance(30000).empty() && idle_expiry.flow_count() == 0 &&
            idle_expiry.counters().expired_flows == 1, "idle maintenance expires state without requiring new traffic");
    idle_expiry.maintenance(40000);
    require(idle_expiry.counters().expired_flows == 1, "idle expiry is not counted twice");
    auto ports = config; ports.snat->last = ports.snat->first; Appliance exhausted(ports);
    exhausted.process("lan", request, 0);
    require(exhausted.process("lan", udp(config, "lan", a, b, 12346), 1).reason == "nat_ports_exhausted", "bounded NAT ports");

    // A sustained allocation burst cannot evict the admitted mapping or leak ports.
    for (unsigned port = 20000; port < 20256; ++port)
        require(exhausted.process("lan", udp(config, "lan", a, b, port), 2).reason == "nat_ports_exhausted",
                "PAT pressure fails closed");
    require(exhausted.flow_count() == 1 && exhausted.process("lan", request, 3).reason == "tracked_udp",
            "PAT pressure preserves established mapping");
    exhausted.maintenance(30003);
    auto recovered_port = exhausted.process("lan", udp(config, "lan", a, b, 20256), 30004);
    require(recovered_port.reason == "new_udp_mapping" && read16(recovered_port.frame, 34) == ports.snat->first && exhausted.flow_count() == 1,
            "PAT pressure recovers exactly one port after expiry");

    // Erasing only an early mapping must not leave indexes pointing at old slots.
    Appliance compacted(config);
    compacted.process("lan", udp(config, "lan", a, b, 8100), 0);
    compacted.process("lan", udp(config, "lan", a, b, 8101), 1000);
    compacted.maintenance(30000);
    auto compacted_reply = compacted.process("wan", udp(config, "wan", b, external, 9000, 40001), 30001);
    require(compacted_reply.reason == "tracked_udp" && read16(compacted_reply.frame, 36) == 8101,
            "partial expiry preserves surviving reverse lookup");
    auto reused = compacted.process("lan", udp(config, "lan", a, b, 8102), 30002);
    require(reused.reason == "new_udp_mapping" && read16(reused.frame, 34) == 40000,
            "expired NAT port reused without corrupting surviving state");
    Appliance clock_edge(config);
    clock_edge.process("lan", request, std::numeric_limits<std::uint64_t>::max() - 10);
    clock_edge.maintenance(std::numeric_limits<std::uint64_t>::max());
    require(clock_edge.flow_count() == 1, "saturated expiry deadline does not expire a fresh mapping");

    // Stateless UDP mapping still permits only matching return traffic.
    auto no_nat = config; no_nat.snat.reset(); Appliance tracked(no_nat);
    require(tracked.process("lan", request, 0).reason == "new_udp_mapping", "UDP tracking without translation");
    check_udp(tracked.process("wan", udp(config, "wan", b, a, 9000, 12345), 1));

    // Configuration rollback and established-flow generation pinning.
    auto next = config; next.generation = 2; next.rules.clear();
    appliance.reload(next);
    require(appliance.config().generation == 2, "publish next generation");
    require(appliance.process("lan", request, 12).reason == "tracked_udp", "existing flow retains old ACL");
    require(appliance.process("lan", udp(config, "lan", a, b, 5678), 13).reason == "acl_deny", "new flows use new ACL");
    throws([&] { appliance.reload(next); }, "stale generation rejected");
    next.generation = 3; next.ports[0].ip = address("10.77.0.125");
    throws([&] { appliance.reload(next); }, "interface mutation rejected");
    require(appliance.config().generation == 2, "rejected config preserves active generation");
    auto many = config; many.snat->last = 40020; Appliance versions(many);
    for (unsigned generation = 1; generation <= 8; ++generation) {
        require(versions.process("lan", udp(config, "lan", a, b, 1000 + generation), generation).reason == "new_udp_mapping",
                "pin one flow per generation");
        if (generation < 8) { many.generation = generation + 1; versions.reload(many); }
    }
    many.generation = 9;
    throws([&] { versions.reload(many); }, "pinned configuration generations bounded");
    versions.process("lan", {}, 40000);
    versions.reload(many);
    require(versions.config().generation == 9, "expiry releases old configuration snapshots");

    auto index_mode_change = config; index_mode_change.generation = 3; index_mode_change.flow_index = !config.flow_index;
    throws([&] { appliance.reload(index_mode_change); }, "flow index mode change requires restart");

    // Router errors and local echo. Error header includes an unchanged quote.
    Appliance router(config);
    auto probe = icmp(config, b, 1);
    auto exceeded = router.process("lan", probe, 0);
    require(exceeded.reason == "ttl_exceeded" && exceeded.frame[34] == 11 && exceeded.frame[22] == 64,
            "TTL expiry generates a new ICMP error");
    require(reference(std::span(exceeded.frame).subspan(34)) == 0, "ICMP error checksum");
    require(std::equal(probe.begin() + 14, probe.begin() + 42, exceeded.frame.begin() + 42), "original quoted bytes");
    auto unroutable = router.process("lan", icmp(config, address("192.0.2.1")), 1);
    require(unroutable.reason == "no_route" && unroutable.frame[34] == 3, "ICMP unreachable");
    auto local = router.process("lan", icmp(config, config.ports[0].ip, 1), 2);
    require(local.reason == "echo_reply" && local.frame[34] == 0, "local TTL1 echo does not expire in transit");
    auto incoming_error = probe; incoming_error[34] = 3; put16(incoming_error, 36, 0);
    put16(incoming_error, 36, reference(std::span(incoming_error).subspan(34)));
    require(router.process("lan", incoming_error, 3).reason == "icmp_quote_invalid", "malformed incoming ICMP error rejected");
    for (unsigned i = 0; i < 8; ++i) router.process("lan", probe, 4 + i);
    require(router.process("lan", probe, 20).reason == "icmp_rate_limit", "bounded ICMP errors");
    require(router.process("lan", probe, 1000).reason == "ttl_exceeded", "error rate limit replenishes");

    Bytes arp(42, 0); std::fill_n(arp.begin(), 6, 255);
    auto peer = mac_address("02:00:00:00:00:0a");
    std::copy(peer.begin(), peer.end(), arp.begin() + 6); put16(arp, 12, 0x0806);
    put16(arp, 14, 1); put16(arp, 16, 0x0800); arp[18] = 6; arp[19] = 4; put16(arp, 20, 1);
    std::copy(peer.begin(), peer.end(), arp.begin() + 22); put32(arp, 28, a); put32(arp, 38, vip);
    auto arp_reply = router.process("lan", arp, 1001);
    require(arp_reply.reason == "arp_reply" && read16(arp_reply.frame, 20) == 2 && read32(arp_reply.frame, 28) == vip,
            "ARP ownership for service VIP");

    // Opt-in solicited dynamic ARP, deferred NAT commit and bounded retries.
    auto dynamic = config; dynamic.neighbor_discovery = true; dynamic.neighbors.clear();
    Appliance discovery(dynamic);
    auto dynamic_request = udp(dynamic, "lan", a, b);
    require(discovery.process("lan", dynamic_request, 0).reason == "neighbor_pending" && discovery.flow_count() == 0,
            "unresolved next hop queues before NAT allocation");
    auto arp_queries = discovery.maintenance(0);
    require(arp_queries.size() == 1 && arp_queries[0].reason == "arp_request" && arp_queries[0].egress == "wan" &&
            read32(arp_queries[0].frame, 38) == b && read32(arp_queries[0].frame, 28) == external,
            "ARP query targets next hop using egress interface identity");
    auto answer = [&](const std::string& port_id, Address ip, const std::string& mac) {
        Bytes packet(42, 0); const auto port = find_port(dynamic, port_id); const auto remote = mac_address(mac);
        std::copy(port->mac.begin(), port->mac.end(), packet.begin());
        std::copy(remote.begin(), remote.end(), packet.begin() + 6);
        put16(packet, 12, 0x0806); put16(packet, 14, 1); put16(packet, 16, 0x0800);
        packet[18] = 6; packet[19] = 4; put16(packet, 20, 2);
        std::copy(remote.begin(), remote.end(), packet.begin() + 22); put32(packet, 28, ip);
        std::copy(port->mac.begin(), port->mac.end(), packet.begin() + 32); put32(packet, 38, port->ip);
        return packet;
    };
    auto neighbor_answer = answer("wan", b, "02:00:00:00:00:0b");
    auto forged_answer = neighbor_answer; forged_answer[22] ^= 2;
    require(discovery.process("wan", forged_answer, 1).reason == "arp_reply_invalid", "ARP sender MAC mismatch rejected");
    require(discovery.process("wan", answer("wan", address("10.77.0.131"), "02:00:00:00:00:0c"), 1).reason == "arp_unsolicited",
            "unsolicited ARP cannot populate cache");
    require(discovery.process("wan", neighbor_answer, 2).reason == "arp_learned", "solicited reply learned");
    auto released = discovery.maintenance(2);
    require(released.size() == 1 && released[0].reason == "new_udp_mapping" && released[0].frame[22] == 63 &&
            read16(released[0].frame, 34) == 40000 && discovery.flow_count() == 1,
            "queued packet forwarded once with one TTL decrement and one NAT mapping");
    check_udp(released[0]);
    require(discovery.process("wan", reply, 3).reason == "neighbor_pending", "return path resolves client separately");
    require(discovery.maintenance(3).size() == 1, "client ARP requested");
    require(discovery.process("lan", answer("lan", a, "02:00:00:00:00:0a"), 4).reason == "arp_learned", "client learned");
    auto dynamic_reply = discovery.maintenance(4);
    require(dynamic_reply.size() == 1 && dynamic_reply[0].reason == "tracked_udp" && read32(dynamic_reply[0].frame, 30) == a,
            "reverse NAT works after resolving return next hop");
    check_udp(dynamic_reply[0]);
    require(discovery.process("lan", dynamic_request, 5).reason == "tracked_udp", "cache hit emits immediately");
    discovery.maintenance(31005);
    require(discovery.process("lan", dynamic_request, 31005).reason == "neighbor_pending", "cache TTL requires new solicitation");

    Appliance silent(dynamic);
    for (unsigned i = 0; i < 4; ++i)
        require(silent.process("lan", udp(dynamic, "lan", a, b, 2000 + i), 0).reason == "neighbor_pending", "four packet queue admitted");
    require(silent.process("lan", dynamic_request, 0).reason == "neighbor_queue_full", "per-target packet cap");
    require(silent.maintenance(0).size() == 1 && silent.maintenance(999).empty() &&
            silent.maintenance(1000).size() == 1 && silent.maintenance(2000).size() == 1,
            "exact bounded ARP retry cadence");
    require(silent.maintenance(3000).empty() && silent.metrics().find("netforge_neighbor_queued_bytes 0\n") != std::string::npos,
            "retry exhaustion releases queued memory");
    require(silent.process("lan", dynamic_request, 3001).reason == "neighbor_negative", "negative cache suppresses immediate retry");
    require(silent.process("lan", dynamic_request, 4000).reason == "neighbor_pending", "negative cache expires");

    Appliance target_bounds(dynamic);
    for (unsigned i = 0; i < 32; ++i)
        require(target_bounds.process("lan", udp(dynamic, "lan", a, b + i, 3000 + i), 0).reason == "neighbor_pending", "bounded pending targets admitted");
    require(target_bounds.process("lan", udp(dynamic, "lan", a, b + 32, 4000), 0).reason == "neighbor_queue_full", "target limit rejects overflow");
    require(target_bounds.maintenance(0).size() == 8 && target_bounds.maintenance(0).empty(), "global ARP request rate bounded");
    target_bounds.maintenance(5000);
    require(target_bounds.metrics().find("netforge_neighbor_queued_bytes 0\n") != std::string::npos, "absolute pending deadline reclaims queues");

    auto jumbo_dynamic = dynamic;
    for (auto& port : jumbo_dynamic.ports) port.mtu = 9000;
    Appliance byte_bounds(jumbo_dynamic);
    unsigned admitted = 0;
    for (unsigned i = 0; i < 128; ++i) {
        auto large = udp(jumbo_dynamic, "lan", a, b + i / 4, 5000 + i);
        large.resize(8014, 0); put16(large, 16, 8000); put16(large, 38, 7980);
        put16(large, 40, 0); auto checksum = udp_reference(large);
        put16(large, 40, checksum == 0 ? 65535 : checksum); set_ip_checksum(large);
        const auto reason = byte_bounds.process("lan", large, 0).reason;
        if (reason == "neighbor_pending") ++admitted;
        else require(reason == "neighbor_queue_full", "global byte cap fails closed");
    }
    require(admitted == 32 && byte_bounds.metrics().find("netforge_neighbor_queued_bytes 256448\n") != std::string::npos,
            "aggregate queued packet bytes remain below 256 KiB");
    require(discovery.metrics().find("netforge_reprocessed_bytes_total 90\n") != std::string::npos,
            "queued retries have explicit byte accounting distinct from wire RX");
    const auto& dynamic_counts = discovery.counters();
    require(dynamic_counts.received == dynamic_counts.emitted + dynamic_counts.dropped +
            dynamic_counts.deferred + dynamic_counts.consumed, "deferred and consumed dispositions accounted separately");

    Appliance reload_pending(dynamic);
    reload_pending.process("lan", dynamic_request, 0); reload_pending.maintenance(0);
    auto deny_dynamic = dynamic; deny_dynamic.generation = 2; deny_dynamic.rules.clear(); reload_pending.reload(deny_dynamic);
    reload_pending.process("wan", neighbor_answer, 1);
    auto denied_release = reload_pending.maintenance(1);
    require(denied_release.size() == 1 && denied_release[0].reason == "acl_deny" && reload_pending.flow_count() == 0,
            "queued new packet revalidates current policy after reload");
    deny_dynamic.generation = 3; deny_dynamic.neighbor_discovery = false;
    throws([&] { reload_pending.reload(deny_dynamic); }, "discovery mode change requires restart");

    // Rendered labels cannot inject metric lines or change label boundaries.
    require(metric_label("a\"b\\c\n") == "a\\\"b\\\\c\\n", "Prometheus label escaping");
    const auto rendered = port_metrics({{"lan", {2, 90, 3, 132, 0}}});
    require(rendered.find("netforge_port_rx_bytes_total{port=\"lan\"} 90\n") != std::string::npos &&
            rendered.find("netforge_port_tx_packets_total{port=\"lan\"} 3\n") != std::string::npos,
            "port metrics preserve counter identity and measurement values");

    // Deterministic malformed-frame sweep: bounded work, exercised under sanitizers.
    std::mt19937 random(2026);
    for (unsigned i = 0; i < 2000; ++i) {
        Bytes malformed(random() % 160);
        for (auto& byte : malformed) byte = static_cast<std::uint8_t>(random());
        router.process("lan", malformed, 1002 + i);
    }
    const auto& counts = router.counters();
    require(counts.received == counts.emitted + counts.dropped, "packet disposition accounting");
    require(router.metrics().find("netforge_decisions_total") != std::string::npos, "metrics exported");
    std::cout << "PASS: " << (config.flow_index ? "[indexed] " : "[linear] ") << "routing, ACLs, UDP/TCP state/NAT/LB, checksums, exhaustion, reload, ARP, ICMP and malformed frames\n";
}
