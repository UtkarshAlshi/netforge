#include "netforge/scheduler.hpp"
#include <limits>
#include <stdexcept>

namespace netforge {
std::uint64_t stable_hash(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : text) { hash ^= byte; hash *= 1099511628211ULL; }
    // Final avalanche avoids adjacent endpoint identifiers dominating FNV ordering.
    hash ^= hash >> 30; hash *= 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 27; hash *= 0x94d049bb133111ebULL;
    return hash ^ (hash >> 31);
}
std::optional<std::size_t> select_backend(const Service& service, const std::string& key,
                                         std::uint64_t cursor, const std::vector<std::size_t>& loads) {
    std::vector<std::size_t> eligible;
    for (std::size_t i = 0; i < service.backends.size(); ++i)
        if (!service.backends[i].draining) eligible.push_back(i);
    if (eligible.empty()) return std::nullopt;
    if (service.scheduler == Scheduler::round_robin) return eligible[cursor % eligible.size()];
    if (service.scheduler == Scheduler::weighted_round_robin) {
        std::uint64_t total = 0;
        for (auto i : eligible) total += service.backends[i].weight;
        if (total == 0) throw std::logic_error("zero scheduler weight");
        auto ticket = cursor % total;
        for (auto i : eligible) {
            if (ticket < service.backends[i].weight) return i;
            ticket -= service.backends[i].weight;
        }
    }
    if (service.scheduler == Scheduler::least_connections) {
        if (loads.size() != service.backends.size()) throw std::logic_error("scheduler load shape");
        auto best = eligible[cursor % eligible.size()];
        for (std::size_t offset = 1; offset < eligible.size(); ++offset) {
            auto i = eligible[(cursor % eligible.size() + offset) % eligible.size()];
            if (loads[i] < loads[best]) best = i;
        }
        return best;
    }
    if (service.scheduler == Scheduler::rendezvous) {
        std::optional<std::size_t> best;
        std::uint64_t score = 0;
        std::string best_endpoint;
        for (auto i : eligible) {
            const auto& backend = service.backends[i];
            const auto endpoint = address_text(backend.ip) + ":" + std::to_string(backend.port);
            const auto candidate = stable_hash(key + "|" + endpoint);
            if (!best || candidate > score || (candidate == score && endpoint < best_endpoint)) {
                best = i; score = candidate; best_endpoint = endpoint;
            }
        }
        return best;
    }
    throw std::logic_error("unsupported scheduler");
}
}
