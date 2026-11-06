#pragma once
#include "netforge/appliance.hpp"
#include "policy.h"
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
namespace netforge::xdp {
inline nf_policy compile_policy(const Config& config, const std::string& ingress_id,
                                const std::map<std::string, unsigned>& bindings) {
    validate(config);
        if (config.snat || !config.services.empty() || config.tcp_stateful || config.neighbor_discovery || config.flow_index)
            throw std::runtime_error("XDP subset rejects NAT, services, TCP state, discovery and flow indexing");
        if (config.default_allow || config.rules.size() > NF_RULES || config.routes.size() > NF_ROUTES)
            throw std::runtime_error("XDP requires default deny, at most 32 ACLs and 16 routes");
    if (bindings.size() != config.ports.size()) throw std::runtime_error("every port requires a binding");
    std::set<unsigned> unique;
    for (const auto& port : config.ports) {
        auto found = bindings.find(port.id);
        if (found == bindings.end() || !found->second || found->second > 0x7fffffffU || !unique.insert(found->second).second)
            throw std::runtime_error("invalid interface binding");
    }
        const Port* ingress = find_port(config, ingress_id);
        if (!ingress) throw std::runtime_error("unknown ingress port");
        nf_policy policy{}; std::memcpy(policy.ingress_mac, ingress->mac.data(), 6);
        for (const auto& rule : config.rules) {
            // Unsupported protocol policies are rejected, never silently punted.
            if (rule.protocol != 17) throw std::runtime_error("initial compiler supports UDP ACLs only");
            auto& out = policy.rules[policy.rule_count++];
            out.ingress = rule.ingress == "*" ? 0 : bindings.at(rule.ingress);
            out.source = rule.source.network; out.source_prefix = rule.source.length;
            out.destination = rule.destination.network; out.destination_prefix = rule.destination.length;
            out.sport = rule.source_port.value_or(0); out.dport = rule.destination_port.value_or(0);
            out.protocol = 17; out.allow = rule.allow;
        }
        for (const auto& route : config.routes) {
            const Port* port = find_port(config, route.port);
            Address next = route.gateway;
            if (!next) {
                if (route.prefix.length != 32) throw std::runtime_error("direct XDP routes require /32 and a static neighbor");
                next = route.prefix.network;
            }
            for (const auto& own : config.ports)
                if (route.prefix.contains(own.ip)) throw std::runtime_error("XDP route includes an appliance-owned IP");
            const Neighbor* neighbor = nullptr;
            for (const auto& candidate : config.neighbors)
                if (candidate.port == route.port && candidate.ip == next) neighbor = &candidate;
            if (!neighbor) throw std::runtime_error("route next hop requires a configured static neighbor");
            auto& out = policy.routes[policy.route_count++];
            out.network = route.prefix.network; out.prefix = route.prefix.length;
            out.ifindex = bindings.at(route.port); out.mtu = static_cast<__u16>(port->mtu);
            std::memcpy(out.source_mac, port->mac.data(), 6);
            std::memcpy(out.destination_mac, neighbor->mac.data(), 6);
        }
    return policy;
}
} // namespace netforge::xdp
