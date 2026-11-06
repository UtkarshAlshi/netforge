#pragma once
#include "netforge/ethernet.hpp"
#include "netforge/ipv4.hpp"
#include "netforge/tcp.hpp"
#include <map>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <limits>

namespace netforge {
using Address = std::uint32_t; // Host numeric value: 10.0.0.1 == 0x0a000001.
struct Prefix { Address network; unsigned length; bool contains(Address ip) const; };
struct Port { std::string id, device; Address ip; Prefix subnet; MacAddress mac; unsigned mtu = 1500; };
struct Route { Prefix prefix; std::string port; Address gateway; };
struct Neighbor { std::string port; Address ip; MacAddress mac; };
struct Rule {
    bool allow; std::string ingress; unsigned protocol; // 0 means any.
    Prefix source, destination;
    std::optional<std::uint16_t> source_port, destination_port;
};
struct Snat { std::string port; Address ip; std::uint16_t first, last; };
enum class Scheduler { round_robin, weighted_round_robin, least_connections, rendezvous };
struct Backend { Address ip; std::uint16_t port; bool draining = false; unsigned weight = 1; };
struct Service {
    std::string ingress; Address vip; std::uint16_t port;
    std::vector<Backend> backends;
    unsigned protocol = 17;
    Scheduler scheduler = Scheduler::round_robin;
};
struct Config {
    std::uint64_t generation = 0;
    std::size_t flow_limit = 1024;
    std::uint64_t udp_timeout_ms = 30000;
    bool tcp_stateful = false;
    bool neighbor_discovery = false;
    bool flow_index = false;
    std::uint64_t tcp_handshake_ms = 10000, tcp_established_ms = 120000, tcp_closing_ms = 5000;
    bool default_allow = false;
    std::vector<Port> ports;
    std::vector<Route> routes;
    std::vector<Neighbor> neighbors;
    std::vector<Rule> rules;
    std::optional<Snat> snat;
    std::vector<Service> services;
};
Address address(const std::string& text);
std::string address_text(Address value);
MacAddress mac_address(const std::string& text);
Config load_config(const std::string& path);
void validate(const Config& config);
const Port* find_port(const Config& config, const std::string& id);
const Route* lookup_route(const Config& config, Address destination);

struct Decision {
    std::string reason;
    std::string egress; // Empty means drop.
    std::vector<std::uint8_t> frame;
};
struct Counters {
    std::uint64_t received = 0, emitted = 0, dropped = 0, deferred = 0, consumed = 0;
    std::uint64_t received_bytes = 0, emitted_bytes = 0, expired_flows = 0;
    std::uint64_t acl_evaluations = 0, acl_matches = 0, acl_defaults = 0, route_lookups = 0, route_misses = 0;
    std::map<std::string, std::uint64_t> reasons;
};

class Appliance {
public:
    explicit Appliance(Config config);
    // Publish a complete configuration or throw without modifying active state.
    // Interface identities and state-table bounds cannot change during a run.
    void reload(Config config);
    Decision process(const std::string& ingress, std::span<const std::uint8_t> frame,
                     std::uint64_t now_ms);
    const Config& config() const { return *config_; }
    const Counters& counters() const { return counters_; }
    std::size_t flow_count() const { return flows_.size(); }
    std::string metrics() const;
    // Live adapter calls this even when idle; bounded ARP requests and queued retries.
    std::vector<Decision> maintenance(std::uint64_t now_ms);
private:
    struct Tuple {
        Address source, destination;
        std::uint16_t source_port, destination_port;
        bool operator==(const Tuple&) const = default;
    };
    struct Flow {
        Tuple original, translated;
        std::string ingress, egress;
        std::uint64_t last_seen;
        std::shared_ptr<const Config> config;
        unsigned protocol = 17;
        TcpState tcp;
    };
    struct FlowKey {
        Tuple tuple; unsigned protocol; std::string port;
        bool operator==(const FlowKey&) const = default;
    };
    struct FlowKeyHash { std::size_t operator()(const FlowKey& key) const; };
    std::unordered_map<FlowKey, std::size_t, FlowKeyHash> forward_index_, reverse_index_;
    std::unordered_set<std::uint64_t> occupied_ports_;
    std::uint64_t next_expiry_ = std::numeric_limits<std::uint64_t>::max();
    static std::uint64_t idle_timeout(const Flow& flow);
    static std::uint64_t expiry(const Flow& flow);
    static std::uint64_t nat_key(unsigned protocol, Address ip, std::uint16_t port);
    void index_flow(std::size_t index);
    struct CachedNeighbor { Neighbor value; std::uint64_t expires; bool negative; };
    struct QueuedPacket { std::string ingress; std::vector<std::uint8_t> frame; };
    struct PendingNeighbor {
        std::string port; Address ip; std::uint64_t created, next_request; unsigned attempts;
        std::vector<QueuedPacket> packets;
    };
    std::vector<CachedNeighbor> neighbor_cache_;
    std::vector<PendingNeighbor> pending_neighbors_;
    std::size_t queued_bytes_ = 0, peak_flows_ = 0, peak_queued_bytes_ = 0;
    std::uint64_t arp_window_ = 0, neighbor_timeouts_ = 0, neighbor_retries_ = 0, reprocessed_ = 0, reprocessed_bytes_ = 0;
    unsigned arp_requests_in_window_ = 0;
    std::shared_ptr<const Config> config_;
    std::vector<Flow> flows_;
    Counters counters_;
    // First16 rules only; generation-scoped diagnostics, fixed memory/series budget.
    std::array<std::uint64_t, 16> rule_evaluations_{}, rule_matches_{};
    std::map<std::string, std::size_t> service_cursor_;
    std::uint64_t clock_ = 0, error_window_ = 0;
    unsigned errors_in_window_ = 0;
    Decision finish(Decision decision);
    void expire_flows(std::uint64_t now);
    const Route* route_for(const Config& config, Address destination);
    Decision defer_neighbor(const Port& port, Address next_hop, const std::string& ingress,
                            std::span<const std::uint8_t> frame, std::uint64_t now);
};

int run_linux(const std::string& config_path, unsigned workers = 0); // Non-Linux build reports unsupported.
} // namespace netforge
