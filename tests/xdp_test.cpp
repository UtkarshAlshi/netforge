#include "netforge/appliance.hpp"
#include "../backends/xdp/fixture.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
using namespace netforge;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    nf_policy policy{}; nf_fixture_policy(&policy);
    std::array<unsigned char, 64> original{}; nf_fixture_packet(original.data());
    auto packet = original; unsigned output = 0;
    require(nf_process(packet.data(), packet.data() + packet.size(), &policy, 5, &output) == NF_ROUTE && output == 7,
            "XDP model selects static route");
    Config config; config.generation = 1;
    config.ports.push_back({"lan", "eth0", address("10.88.0.1"), {address("10.88.0.0"), 24}, mac_address("02:88:00:00:00:02")});
    config.ports.push_back({"wan", "eth1", address("10.89.0.1"), {address("10.89.0.0"), 24}, mac_address("02:89:00:00:00:01")});
    config.routes.push_back({{address("10.89.0.0"), 24}, "wan", 0});
    config.neighbors.push_back({"wan", address("10.89.0.2"), mac_address("02:89:00:00:00:02")});
    config.rules.push_back({true, "lan", 17, {address("10.88.0.0"), 24}, {address("10.89.0.0"), 24}, {}, 9000});
    Appliance reference(config);
    auto decision = reference.process("lan", original, 0);
    require(decision.egress == "wan" && std::equal(packet.begin(), packet.end(), decision.frame.begin(), decision.frame.end()),
            "native XDP route bytes match C++ reference on declared valid UDP subset");
    for (unsigned length = 0; length < 64; ++length) {
        packet = original;
        require(nf_process(packet.data(), packet.data() + length, &policy, 5, &output) == NF_DROP, "truncations fail closed");
    }
    packet = original; nf_put16(packet.data() + 36, 9001);
    require(nf_process(packet.data(), packet.data() + 64, &policy, 5, &output) == NF_DROP, "ACL default deny");
    policy.rules[0].ingress = 9; packet = original;
    require(nf_process(packet.data(), packet.data() + 64, &policy, 5, &output) == NF_DROP, "ingress-specific rule");
    policy.rules[0].ingress = 0;
    packet = original; packet[24] ^= 1;
    require(nf_process(packet.data(), packet.data() + 64, &policy, 5, &output) == NF_DROP, "invalid checksum rejected");
    policy.rules[1] = policy.rules[0]; policy.rules[0].allow = 0; policy.rule_count = 2; packet = original;
    require(nf_process(packet.data(), packet.data() + 64, &policy, 5, &output) == NF_DROP, "first ACL match takes precedence");
    nf_fixture_policy(&policy); policy.routes[1] = policy.routes[0]; policy.routes[1].network = 0x0a590002; policy.routes[1].prefix = 32;
    policy.routes[1].ifindex = 8; policy.route_count = 2; packet = original;
    require(nf_process(packet.data(), packet.data() + 64, &policy, 5, &output) == NF_ROUTE && output == 8, "LPM selects specific route");
    std::mt19937 random(826);
    std::array<unsigned char, 256> malformed{};
    for (unsigned i = 0; i < 2000; ++i) {
        for (auto& byte : malformed) byte = static_cast<unsigned char>(random());
        nf_process(malformed.data(), malformed.data() + random() % 257, &policy, 5, &output);
    }
    std::cout << "PASS: native XDP parser/ACL/LPM model, reference byte parity and malformed corpus\n";
}
