#include "netforge/appliance.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace netforge {
namespace {
unsigned number(const std::string& text, unsigned maximum) {
    unsigned result = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || result > maximum)
        throw std::runtime_error("invalid number: " + text);
    return result;
}
Address mask(unsigned length) { return length == 0 ? 0U : 0xffffffffU << (32 - length); }
Prefix prefix(const std::string& text) {
    auto slash = text.find('/');
    if (slash == std::string::npos) throw std::runtime_error("prefix requires /length");
    auto ip = address(text.substr(0, slash));
    auto length = number(text.substr(slash + 1), 32);
    return {ip & mask(length), length};
}
std::uint16_t port_number(const std::string& text) {
    auto value = number(text, 65535);
    if (value == 0) throw std::runtime_error("port zero is unsupported");
    return static_cast<std::uint16_t>(value);
}
std::optional<std::uint16_t> match_port(const std::string& text) {
    return text == "*" ? std::nullopt : std::optional<std::uint16_t>(port_number(text));
}
bool unicast(Address ip) { return (ip >> 24) != 0 && (ip >> 24) != 127 && (ip >> 28) < 14; }
}
bool Prefix::contains(Address ip) const { return (ip & mask(length)) == network; }
Address address(const std::string& text) {
    std::istringstream input(text); std::string field; Address result = 0;
    for (int i = 0; i < 4; ++i) {
        if (!std::getline(input, field, '.')) throw std::runtime_error("invalid IPv4: " + text);
        result = (result << 8) | number(field, 255);
    }
    if (input.peek() != EOF || text.empty() || text.back() == '.')
        throw std::runtime_error("invalid IPv4: " + text);
    return result;
}
std::string address_text(Address ip) {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." +
           std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
}
MacAddress mac_address(const std::string& text) {
    if (text.size() != 17) throw std::runtime_error("invalid MAC");
    MacAddress mac{};
    for (std::size_t i = 0; i < 6; ++i) {
        unsigned value = 0;
        const char* start = text.data() + i * 3;
        auto [end, error] = std::from_chars(start, start + 2, value, 16);
        if (error != std::errc{} || end != start + 2 || (i < 5 && text[i * 3 + 2] != ':'))
            throw std::runtime_error("invalid MAC");
        mac[i] = static_cast<std::uint8_t>(value);
    }
    return mac;
}
const Port* find_port(const Config& config, const std::string& id) {
    for (const auto& port : config.ports) if (port.id == id) return &port;
    return nullptr;
}
const Route* lookup_route(const Config& config, Address destination) {
    const Route* best = nullptr;
    for (const auto& route : config.routes)
        if (route.prefix.contains(destination) && (!best || route.prefix.length > best->prefix.length))
            best = &route;
    return best;
}
void validate(const Config& config) {
    if (config.generation == 0 || config.ports.empty() || config.ports.size() > 16 ||
        config.routes.size() > 4096 || config.neighbors.size() > 4096 || config.rules.size() > 4096 ||
        config.flow_limit == 0 || config.flow_limit > 65536 || config.udp_timeout_ms < 100 ||
        config.udp_timeout_ms > 3600000 || config.services.size() > 64 ||
        config.tcp_handshake_ms < 100 || config.tcp_handshake_ms > 3600000 ||
        config.tcp_established_ms < 100 || config.tcp_established_ms > 3600000 ||
        config.tcp_closing_ms < 100 || config.tcp_closing_ms > 3600000)
        throw std::runtime_error("configuration bounds violated");
    std::set<std::string> ids, devices, prefixes, neighbors, services;
    for (const auto& p : config.ports) {
        if (p.id.empty() || p.id.size() > 32 || p.device.empty() || p.device.size() > 15 || p.subnet.length > 32 ||
            !unicast(p.ip) || !p.subnet.contains(p.ip) || p.mtu < 68 || p.mtu > 9000 || (p.mac[0] & 1) ||
            p.mac == MacAddress{} || !ids.insert(p.id).second || !devices.insert(p.device).second)
            throw std::runtime_error("invalid or duplicate port");
    }
    for (const auto& r : config.routes) {
        auto p = find_port(config, r.port);
        if (r.prefix.length > 32 || !p || (r.gateway && (!unicast(r.gateway) || !p->subnet.contains(r.gateway))) ||
            !prefixes.insert(address_text(r.prefix.network) + "/" + std::to_string(r.prefix.length)).second)
            throw std::runtime_error("invalid or duplicate route");
    }
    for (const auto& n : config.neighbors) {
        auto p = find_port(config, n.port);
        if (!p || !unicast(n.ip) || !p->subnet.contains(n.ip) || (n.mac[0] & 1) || n.mac == MacAddress{} ||
            !neighbors.insert(n.port + ":" + address_text(n.ip)).second)
            throw std::runtime_error("invalid or duplicate static neighbor");
    }
    for (const auto& rule : config.rules) {
        if ((rule.ingress != "*" && !find_port(config, rule.ingress)) ||
            (rule.protocol != 0 && rule.protocol != 1 && rule.protocol != 6 && rule.protocol != 17) ||
            rule.source.length > 32 || rule.destination.length > 32 ||
            ((rule.source_port || rule.destination_port) && rule.protocol != 6 && rule.protocol != 17))
            throw std::runtime_error("invalid ACL");
    }
    if (config.snat) {
        auto p = find_port(config, config.snat->port);
        if (!p || config.snat->ip != p->ip || config.snat->first == 0 || config.snat->last < config.snat->first)
            throw std::runtime_error("SNAT must use egress interface IP and valid port range");
    }
    for (const auto& service : config.services) {
        auto p = find_port(config, service.ingress);
        if (!p || !unicast(service.vip) || !p->subnet.contains(service.vip) || service.port == 0 ||
            (service.protocol != 17 && service.protocol != 6) || (service.protocol == 6 && !config.tcp_stateful) ||
            service.backends.empty() || service.backends.size() > 64 ||
            !services.insert(service.ingress + ":" + address_text(service.vip) + ":" + std::to_string(service.port) +
                             ":" + std::to_string(service.protocol)).second)
            throw std::runtime_error("invalid or duplicate service");
        std::set<std::string> endpoints;
        for (const auto& backend : service.backends)
            if (!unicast(backend.ip) || backend.port == 0 || backend.weight < 1 || backend.weight > 64 ||
                (backend.weight != 1 && service.scheduler != Scheduler::weighted_round_robin) ||
                !endpoints.insert(address_text(backend.ip) + ":" + std::to_string(backend.port)).second)
                throw std::runtime_error("invalid or duplicate backend");
    }
}
Config load_config(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open configuration: " + path);
    // Cap input before line parsing, including a single maliciously long line.
    std::string contents; char byte;
    while (file.get(byte)) {
        if (contents.size() == 65536) throw std::runtime_error("configuration exceeds 64 KiB");
        contents.push_back(byte);
    }
    if (file.bad()) throw std::runtime_error("configuration read failed");
    Config config; bool version_seen = false, defaults_seen = false, limits_seen = false;
    bool tcp_seen = false, tcp_timeouts_seen = false, discovery_seen = false, index_seen = false;
    std::istringstream input(contents); std::string line; unsigned line_no = 0;
    while (std::getline(input, line)) {
        ++line_no; line = line.substr(0, line.find('#'));
        std::istringstream words(line); std::vector<std::string> t; std::string word;
        while (words >> word) t.push_back(word);
        if (t.empty()) continue;
        try {
            auto count = [&](std::size_t n) { if (t.size() != n) throw std::runtime_error("wrong field count"); };
            if (t[0] == "version") {
                count(2); if (version_seen) throw std::runtime_error("duplicate version");
                version_seen = true; config.generation = number(t[1], 0xffffffffU);
            } else if (t[0] == "default") {
                count(2); if (defaults_seen || (t[1] != "allow" && t[1] != "deny")) throw std::runtime_error("invalid default");
                defaults_seen = true; config.default_allow = t[1] == "allow";
            } else if (t[0] == "limits") {
                count(3); if (limits_seen) throw std::runtime_error("duplicate limits");
                limits_seen = true; config.flow_limit = number(t[1], 65536); config.udp_timeout_ms = number(t[2], 3600000);
            } else if (t[0] == "flow-index") {
                count(2);
                if (index_seen || (t[1] != "on" && t[1] != "off")) throw std::runtime_error("invalid flow index mode");
                index_seen = true; config.flow_index = t[1] == "on";
            } else if (t[0] == "neighbor-discovery") {
                count(2);
                if (discovery_seen || (t[1] != "on" && t[1] != "off")) throw std::runtime_error("invalid neighbor discovery mode");
                discovery_seen = true; config.neighbor_discovery = t[1] == "on";
            } else if (t[0] == "tcp-stateful") {
                count(2);
                if (tcp_seen || (t[1] != "on" && t[1] != "off")) throw std::runtime_error("invalid TCP mode");
                tcp_seen = true; config.tcp_stateful = t[1] == "on";
            } else if (t[0] == "tcp-timeouts") {
                count(4); if (tcp_timeouts_seen) throw std::runtime_error("duplicate TCP timeouts");
                tcp_timeouts_seen = true;
                config.tcp_handshake_ms = number(t[1], 3600000);
                config.tcp_established_ms = number(t[2], 3600000);
                config.tcp_closing_ms = number(t[3], 3600000);
            } else if (t[0] == "port") {
                if (t.size() != 5 && t.size() != 6) throw std::runtime_error("port needs 4 fields and optional MTU");
                auto slash = t[3].find('/');
                config.ports.push_back({t[1], t[2], address(t[3].substr(0, slash)), prefix(t[3]), mac_address(t[4]),
                                       t.size() == 6 ? number(t[5], 9000) : 1500});
            } else if (t[0] == "route") {
                count(4); auto p = prefix(t[1]);
                if (address(t[1].substr(0, t[1].find('/'))) != p.network) throw std::runtime_error("route prefix has host bits");
                config.routes.push_back({p, t[2], t[3] == "direct" ? 0 : address(t[3])});
            } else if (t[0] == "neighbor") {
                count(4); config.neighbors.push_back({t[1], address(t[2]), mac_address(t[3])});
            } else if (t[0] == "acl") {
                count(8); if (t[1] != "allow" && t[1] != "deny") throw std::runtime_error("ACL action");
                unsigned protocol = t[3] == "any" ? 0 : t[3] == "icmp" ? 1 : t[3] == "tcp" ? 6 : t[3] == "udp" ? 17 : 255;
                config.rules.push_back({t[1] == "allow", t[2], protocol, prefix(t[4]), prefix(t[5]), match_port(t[6]), match_port(t[7])});
            } else if (t[0] == "snat") {
                count(5); if (config.snat) throw std::runtime_error("duplicate SNAT");
                config.snat = Snat{t[1], address(t[2]), port_number(t[3]), port_number(t[4])};
            } else if (t[0] == "service" || t[0] == "tcp-service") {
                if (t.size() < 5) throw std::runtime_error("service needs backends");
                Service service{t[1], address(t[2]), port_number(t[3]), {}};
                service.protocol = t[0] == "tcp-service" ? 6 : 17;
                std::size_t first = 4;
                if (t[4].starts_with("policy=")) {
                    if (t[4] == "policy=round-robin") service.scheduler = Scheduler::round_robin;
                    else if (t[4] == "policy=weighted-round-robin") service.scheduler = Scheduler::weighted_round_robin;
                    else if (t[4] == "policy=least-connections") service.scheduler = Scheduler::least_connections;
                    else if (t[4] == "policy=rendezvous") service.scheduler = Scheduler::rendezvous;
                    else throw std::runtime_error("unknown scheduler policy");
                    first = 5;
                }
                for (std::size_t i = first; i < t.size(); ++i) {
                    auto endpoint = t[i];
                    const bool draining = endpoint.ends_with("/drain");
                    if (draining) endpoint.resize(endpoint.size() - 6);
                    unsigned weight = 1;
                    const auto at = endpoint.find('@');
                    if (at != std::string::npos) {
                        weight = port_number(endpoint.substr(at + 1)); endpoint.resize(at);
                    }
                    auto colon = endpoint.find(':');
                    if (colon == std::string::npos) throw std::runtime_error("backend needs ip:port");
                    service.backends.push_back({address(endpoint.substr(0, colon)), port_number(endpoint.substr(colon + 1)), draining, weight});
                }
                config.services.push_back(std::move(service));
            } else throw std::runtime_error("unknown directive: " + t[0]);
        } catch (const std::exception& error) {
            throw std::runtime_error("configuration line " + std::to_string(line_no) + ": " + error.what());
        }
    }
    validate(config); return config;
}
} // namespace netforge
