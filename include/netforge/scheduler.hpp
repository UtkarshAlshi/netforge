#pragma once
#include "netforge/appliance.hpp"
namespace netforge {
// Caller supplies live tracked-flow counts by backend index. Draining entries
// never receive new selections. Key must encode the original client/service tuple.
std::optional<std::size_t> select_backend(const Service& service, const std::string& key,
                                         std::uint64_t cursor, const std::vector<std::size_t>& loads);
std::uint64_t stable_hash(const std::string& text);
}
