#pragma once
#include "../xdp/compile_policy.hpp"
#include "../xdp/wire_policy.hpp"
namespace netforge::dpdk {
// Two logical ring endpoints: ingress1=lan, egress2=wan. No live NIC bindings.
inline void validate_binding(const Config& config, const nf_policy& supplied) {
    if (config.ports.size()!=2 || !find_port(config,"lan") || !find_port(config,"wan"))
        throw std::runtime_error("DPDK ring requires exactly lan/wan ports");
    const auto expected = xdp::compile_policy(config,"lan",{{"lan",1},{"wan",2}});
    for (unsigned i=0;i<expected.route_count;++i)
        if (expected.routes[i].ifindex!=2) throw std::runtime_error("DPDK ring supports wan egress only");
    if (xdp::encode_policy(expected)!=xdp::encode_policy(supplied))
        throw std::runtime_error("DPDK compiled policy does not match configuration");
}
}
