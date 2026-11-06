#include "netforge/appliance.hpp"
#include "netforge/scheduler.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace netforge {
namespace {
using Bytes = std::vector<std::uint8_t>;
std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t i) {
    return static_cast<std::uint16_t>((unsigned(b[i]) << 8) | b[i + 1]);
}
Address u32(std::span<const std::uint8_t> b, std::size_t i) {
    return (Address(b[i]) << 24) | (Address(b[i + 1]) << 16) | (Address(b[i + 2]) << 8) | b[i + 3];
}
void put16(Bytes& b, std::size_t i, std::uint16_t v) { b[i] = v >> 8; b[i + 1] = v & 255; }
void put32(Bytes& b, std::size_t i, Address v) {
    b[i] = v >> 24; b[i + 1] = (v >> 16) & 255; b[i + 2] = (v >> 8) & 255; b[i + 3] = v & 255;
}
void ethernet(Bytes& frame, MacAddress source, MacAddress destination, std::uint16_t type) {
    std::copy(destination.begin(), destination.end(), frame.begin());
    std::copy(source.begin(), source.end(), frame.begin() + 6); put16(frame, 12, type);
}
std::uint16_t transport_checksum(std::span<const std::uint8_t> ip) {
    const auto header = (ip[0] & 15U) * 4;
    const auto length = ip.size() - header;
    Bytes pseudo(12 + length);
    std::copy_n(ip.begin() + 12, 8, pseudo.begin());
    pseudo[9] = ip[9]; put16(pseudo, 10, static_cast<std::uint16_t>(length));
    std::copy(ip.begin() + header, ip.end(), pseudo.begin() + 12);
    return internet_checksum(pseudo);
}
void ip_checksum(Bytes& frame) {
    put16(frame, 24, 0);
    put16(frame, 24, internet_checksum(std::span(frame).subspan(14, (frame[14] & 15U) * 4)));
}
bool invalid_endpoint(const Config& c, Address ip) {
    if ((ip >> 24) == 0 || (ip >> 24) == 127 || (ip >> 28) >= 14) return true;
    for (const auto& p : c.ports) {
        if (p.subnet.length < 31) {
            const Address host_mask = 0xffffffffU >> p.subnet.length;
            if (ip == p.subnet.network || ip == (p.subnet.network | host_mask)) return true;
        }
    }
    return false;
}
}

Appliance::Appliance(Config config) {
    validate(config); config_ = std::make_shared<const Config>(std::move(config));
}
void Appliance::reload(Config config) {
    validate(config);
    if (config.generation <= config_->generation) throw std::runtime_error("generation must increase");
    if (config.ports.size() != config_->ports.size() || config.flow_limit != config_->flow_limit ||
        config.flow_index != config_->flow_index || config.udp_timeout_ms != config_->udp_timeout_ms || config.neighbor_discovery != config_->neighbor_discovery || config.tcp_stateful != config_->tcp_stateful ||
        config.tcp_handshake_ms != config_->tcp_handshake_ms || config.tcp_established_ms != config_->tcp_established_ms ||
        config.tcp_closing_ms != config_->tcp_closing_ms) throw std::runtime_error("restart required for port/state limits");
    for (const auto& p : config.ports) {
        auto old = find_port(*config_, p.id);
        if (!old || old->ip != p.ip || old->mac != p.mac || old->device != p.device ||
            old->subnet.network != p.subnet.network || old->subnet.length != p.subnet.length || old->mtu != p.mtu)
            throw std::runtime_error("restart required for interface changes");
    }
    std::set<std::uint64_t> generations{config.generation};
    for (const auto& flow : flows_) generations.insert(flow.config->generation);
    if (generations.size() > 8) throw std::runtime_error("too many flow-pinned generations; wait for expiry");
    auto next = std::make_shared<const Config>(std::move(config));
    config_ = std::move(next); service_cursor_.clear();
    rule_evaluations_.fill(0); rule_matches_.fill(0); backend_counts_.fill({});
}
Decision Appliance::finish(Decision decision) {
    ++counters_.reasons[decision.reason];
    if (decision.reason == "neighbor_pending") ++counters_.deferred;
    else if (decision.reason == "arp_learned") ++counters_.consumed;
    else if (decision.egress.empty()) ++counters_.dropped;
    else { ++counters_.emitted; counters_.emitted_bytes += decision.frame.size(); }
    return decision;
}
Decision Appliance::process(const std::string& ingress, std::span<const std::uint8_t> frame,
                            std::uint64_t now) {
    ++counters_.received; counters_.received_bytes += frame.size();
    now = std::max(now, clock_); clock_ = now;
    expire_flows(now);
    auto drop = [&](const std::string& reason) { return finish({reason, {}, {}}); };
    if (frame.size() > 65549) return drop("frame_oversize");
    auto in = find_port(*config_, ingress);
    if (!in) return drop("unknown_ingress");
    auto parsed_eth = parse_ethernet(frame);
    if (!std::holds_alternative<EthernetHeader>(parsed_eth)) return drop("ethernet_truncated");
    const auto eth = std::get<EthernetHeader>(parsed_eth);
    if ((eth.source[0] & 1) || eth.source == MacAddress{}) return drop("invalid_source_mac");
    const MacAddress broadcast{255,255,255,255,255,255};
    if (eth.destination != in->mac && !(eth.type_or_length == 0x0806 && eth.destination == broadcast))
        return drop("not_for_port");

    if (eth.type_or_length == 0x0806) {
        if (frame.size() < 42) return drop("arp_truncated");
        if (config_->neighbor_discovery && u16(frame, 20) == 2) {
            if (u16(frame, 14) != 1 || u16(frame, 16) != 0x0800 || frame[18] != 6 || frame[19] != 4 ||
                eth.destination != in->mac || !std::equal(eth.source.begin(), eth.source.end(), frame.begin() + 22) ||
                !std::equal(in->mac.begin(), in->mac.end(), frame.begin() + 32) || u32(frame, 38) != in->ip ||
                !in->subnet.contains(u32(frame, 28)) || invalid_endpoint(*config_, u32(frame, 28)))
                return drop("arp_reply_invalid");
            const auto sender = u32(frame, 28);
            auto pending = std::find_if(pending_neighbors_.begin(), pending_neighbors_.end(), [&](const auto& n) {
                return n.port == ingress && n.ip == sender && n.attempts > 0 && now - n.created < 5000;
            });
            if (pending == pending_neighbors_.end()) return drop("arp_unsolicited");
            std::erase_if(neighbor_cache_, [&](const auto& n) { return n.value.port == ingress && n.value.ip == sender; });
            if (neighbor_cache_.size() >= 128) neighbor_cache_.erase(neighbor_cache_.begin());
            neighbor_cache_.push_back({{ingress, sender, eth.source}, now + 30000, false});
            return drop("arp_learned");
        }
        if (u16(frame, 14) != 1 || u16(frame, 16) != 0x0800 || frame[18] != 6 || frame[19] != 4 ||
            u16(frame, 20) != 1 || !std::equal(eth.source.begin(), eth.source.end(), frame.begin() + 22))
            return drop("arp_unsupported");
        const auto target = u32(frame, 38);
        bool owned = target == in->ip;
        for (const auto& service : config_->services)
            owned = owned || (service.ingress == ingress && service.vip == target);
        if (!owned) return drop("arp_not_owned");
        Bytes reply(42, 0); ethernet(reply, in->mac, eth.source, 0x0806);
        put16(reply, 14, 1); put16(reply, 16, 0x0800); reply[18] = 6; reply[19] = 4; put16(reply, 20, 2);
        std::copy(in->mac.begin(), in->mac.end(), reply.begin() + 22); put32(reply, 28, target);
        std::copy(eth.source.begin(), eth.source.end(), reply.begin() + 32); put32(reply, 38, u32(frame, 28));
        return finish({"arp_reply", ingress, std::move(reply)});
    }
    if (eth.type_or_length != 0x0800) return drop("ethertype_unsupported");
    auto parsed = parse_ipv4(frame.subspan(14));
    if (!std::holds_alternative<IPv4Header>(parsed)) return drop("ipv4_invalid");
    const auto header = std::get<IPv4Header>(parsed);
    if (header.header_length != 20) return drop("ipv4_options_unsupported");
    if (header.more_fragments || header.fragment_offset != 0) return drop("fragment_unsupported");
    const auto ip = frame.subspan(14, header.total_length);
    const Address source = u32(ip, 12), destination = u32(ip, 16);
    if (invalid_endpoint(*config_, source) || invalid_endpoint(*config_, destination)) return drop("non_unicast");
    const auto payload = ip.subspan(20);
    std::uint16_t source_port = 0, destination_port = 0;
    if (header.protocol == 17) {
        if (payload.size() < 8 || u16(payload, 4) != payload.size()) return drop("udp_length");
        if (u16(payload, 6) != 0 && transport_checksum(ip) != 0) return drop("udp_checksum");
        source_port = u16(payload, 0); destination_port = u16(payload, 2);
    } else if (header.protocol == 6) {
        if (payload.size() < 20 || (payload[12] >> 4) < 5 || (payload[12] >> 4) * 4U > payload.size())
            return drop("tcp_length");
        if (transport_checksum(ip) != 0) return drop("tcp_checksum");
        source_port = u16(payload, 0); destination_port = u16(payload, 2);
    } else if (header.protocol == 1) {
        if (payload.size() < 8 || internet_checksum(payload) != 0) return drop("icmp_invalid");
    } else return drop("protocol_unsupported");

    // Related ICMP errors quote the post-NAT packet. Only exact live mappings
    // authorize reverse translation; unrelated errors never bypass the ACL.
    if (header.protocol == 1 && (payload[0] == 3 || payload[0] == 11 || payload[0] == 12)) {
        if ((payload[0] == 3 && payload[1] > 15) || (payload[0] == 11 && payload[1] > 1) ||
            (payload[0] == 12 && payload[1] > 2)) return drop("icmp_error_code");
        const auto quote = payload.subspan(8);
        if (quote.size() < 28 || quote[0] != 0x45 || u16(quote, 2) < 28 ||
            (u16(quote, 6) & 0x3fff) != 0 || internet_checksum(quote.first(20)) != 0 ||
            (quote[9] != 6 && quote[9] != 17)) return drop("icmp_quote_invalid");
        for (const auto& f : flows_) {
            if (ingress != f.egress || quote[9] != f.protocol || destination != f.translated.source ||
                u32(quote, 12) != f.translated.source || u32(quote, 16) != f.translated.destination ||
                u16(quote, 20) != f.translated.source_port || u16(quote, 22) != f.translated.destination_port) continue;
            if (header.ttl <= 1) return drop("icmp_related_ttl");
            auto route = route_for(*f.config, f.original.source);
            if (!route || route->port != f.ingress) return drop("icmp_related_route");
            const auto port = find_port(*f.config, route->port);
            if (header.total_length > port->mtu) return drop("icmp_related_mtu");
            const auto hop = route->gateway ? route->gateway : f.original.source;
            const Neighbor* peer = nullptr;
            for (const auto& n : f.config->neighbors) if (n.port == route->port && n.ip == hop) { peer = &n; break; }
            if (!peer && f.config->neighbor_discovery) {
                for (const auto& entry : neighbor_cache_) {
                    if (entry.value.port == route->port && entry.value.ip == hop && entry.expires > now) {
                        if (entry.negative) return drop("neighbor_negative");
                        peer = &entry.value; break;
                    }
                }
                if (!peer) return defer_neighbor(*port, hop, ingress, frame, now);
            }
            if (!peer) return drop("neighbor_missing");
            Bytes out(frame.begin(), frame.begin() + 14 + header.total_length);
            ethernet(out, port->mac, peer->mac, 0x0800); out[22] = header.ttl - 1;
            put32(out, 30, f.original.source);
            if (source == f.translated.destination) put32(out, 26, f.original.destination);
            // RFC 1624 incremental checksum repair works even for truncated quotes.
            const std::size_t offset = f.protocol == 17 ? 26 : 36;
            if (quote.size() >= offset + 2 && !(f.protocol == 17 && u16(quote, offset) == 0)) {
                std::uint32_t sum = (~u16(quote, offset)) & 0xffffU;
                const Address before[] = {f.translated.source, f.translated.destination};
                const Address after[] = {f.original.source, f.original.destination};
                for (unsigned i = 0; i < 2; ++i) {
                    sum += (~(before[i] >> 16)) & 0xffffU; sum += after[i] >> 16;
                    sum += (~before[i]) & 0xffffU; sum += after[i] & 0xffffU;
                }
                sum += (~f.translated.source_port) & 0xffffU; sum += f.original.source_port;
                sum += (~f.translated.destination_port) & 0xffffU; sum += f.original.destination_port;
                while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
                auto repaired = static_cast<std::uint16_t>(~sum);
                put16(out, 42 + offset, f.protocol == 17 && repaired == 0 ? 0xffff : repaired);
            }
            put32(out, 54, f.original.source); put32(out, 58, f.original.destination);
            put16(out, 62, f.original.source_port); put16(out, 64, f.original.destination_port);
            put16(out, 52, 0); put16(out, 52, internet_checksum(std::span(out).subspan(42, 20)));
            put16(out, 36, 0); put16(out, 36, internet_checksum(std::span(out).subspan(34)));
            ip_checksum(out);
            // Errors do not refresh connection lifetime or alter TCP handshake state.
            return finish({"related_icmp", route->port, std::move(out)});
        }
        return drop("icmp_unrelated");
    }

    Tuple original{source, destination, source_port, destination_port}, translated = original;
    auto reversed = [](Tuple t) { return Tuple{t.destination, t.source, t.destination_port, t.source_port}; };
    Flow* matched = nullptr;
    bool reply_direction = false;
    std::optional<TcpState> next_tcp;
    auto selected = config_;
    const bool tracked_protocol = header.protocol == 17 || (header.protocol == 6 && config_->tcp_stateful);
    if (tracked_protocol) {
        if (config_->flow_index) {
            const FlowKey key{original, header.protocol, ingress};
            const auto forward = forward_index_.find(key), reverse = reverse_index_.find(key);
            if (forward != forward_index_.end() && (reverse == reverse_index_.end() || forward->second <= reverse->second)) {
                matched = &flows_[forward->second]; translated = matched->translated; selected = matched->config;
            } else if (reverse != reverse_index_.end()) {
                matched = &flows_[reverse->second]; reply_direction = true;
                translated = reversed(matched->original); selected = matched->config;
            }
        } else {
        for (auto& f : flows_) {
            if (f.protocol != header.protocol) continue;
            if (ingress == f.ingress && original == f.original) {
                matched = &f; translated = f.translated; selected = f.config; break;
            }
            if (ingress == f.egress && original == reversed(f.translated)) {
                matched = &f; reply_direction = true;
                translated = reversed(f.original); selected = f.config; break;
            }
        }
        }
    }
    if (!matched) {
        bool allow = selected->default_allow;
        bool rule_matched = false;
        for (std::size_t index = 0; index < selected->rules.size(); ++index) {
            const auto& rule = selected->rules[index];
            ++counters_.acl_evaluations;
            if (index < rule_evaluations_.size()) ++rule_evaluations_[index];
            if ((rule.ingress == "*" || rule.ingress == ingress) &&
                (rule.protocol == 0 || rule.protocol == header.protocol) &&
                rule.source.contains(source) && rule.destination.contains(destination) &&
                (!rule.source_port || *rule.source_port == source_port) &&
                (!rule.destination_port || *rule.destination_port == destination_port)) {
                allow = rule.allow; rule_matched = true; ++counters_.acl_matches;
                if (index < rule_matches_.size()) ++rule_matches_[index];
                break;
            }
        }
        if (!rule_matched) ++counters_.acl_defaults;
        if (!allow) return drop("acl_deny");
    }
    if (header.protocol == 6 && selected->tcp_stateful) {
        const TcpSegment segment{u32(payload, 4), u32(payload, 8), payload[13],
                                 payload.size() - ((payload[12] >> 4) * 4U)};
        next_tcp = matched ? observe_tcp(matched->tcp, segment, reply_direction) : begin_tcp(segment);
        if (!next_tcp) return drop(matched ? "tcp_state_invalid" : "tcp_requires_syn");
    }

    auto error = [&](std::uint8_t type, std::uint8_t code, const std::string& reason, std::uint16_t mtu = 0) {
        // Do not emit ICMP errors in response to ICMP errors.
        if (header.protocol == 1 && (payload[0] == 3 || payload[0] == 4 || payload[0] == 5 ||
                                    payload[0] == 11 || payload[0] == 12)) return drop("icmp_error_suppressed");
        if (now - error_window_ >= 1000) { error_window_ = now; errors_in_window_ = 0; }
        if (errors_in_window_ >= 10) return drop("icmp_rate_limit");
        ++errors_in_window_;
        const auto quote = std::min<std::size_t>(ip.size(), 28);
        Bytes out(14 + 20 + 8 + quote, 0);
        ethernet(out, in->mac, eth.source, 0x0800);
        out[14] = 0x45; put16(out, 16, static_cast<std::uint16_t>(out.size() - 14));
        out[22] = 64; out[23] = 1; put32(out, 26, in->ip); put32(out, 30, source);
        out[34] = type; out[35] = code;
        if (type == 3 && code == 4) put16(out, 40, mtu);
        std::copy_n(ip.begin(), quote, out.begin() + 42);
        put16(out, 36, internet_checksum(std::span(out).subspan(34))); ip_checksum(out);
        return finish({reason, ingress, std::move(out)});
    };
    const Service* service = nullptr; std::string service_key;
    if (!matched && tracked_protocol) {
        for (const auto& s : selected->services) {
            if (s.protocol == header.protocol && s.ingress == ingress && s.vip == destination && s.port == destination_port) {
                service = &s;
                service_key = ingress + ":" + address_text(s.vip) + ":" + std::to_string(s.port) + ":" + std::to_string(s.protocol);
                std::vector<std::size_t> loads(s.backends.size(), 0);
                if (s.scheduler == Scheduler::least_connections) {
                    for (const auto& f : flows_) {
                        if (f.protocol != s.protocol || f.ingress != s.ingress ||
                            f.original.destination != s.vip || f.original.destination_port != s.port) continue;
                        for (std::size_t i = 0; i < s.backends.size(); ++i)
                            if (f.translated.destination == s.backends[i].ip &&
                                f.translated.destination_port == s.backends[i].port) ++loads[i];
                    }
                }
                const auto key = service_key + ":" + address_text(source) + ":" + std::to_string(source_port);
                auto index = select_backend(s, key, service_cursor_[service_key], loads);
                if (!index) return drop("service_unavailable");
                const auto& backend = s.backends[*index];
                translated.destination = backend.ip; translated.destination_port = backend.port;
                break;
            }
        }
    }
    if (!matched && !service && destination == in->ip) {
        if (header.total_length > in->mtu) return drop("local_mtu");
        if (header.protocol != 1 || payload[0] != 8 || payload[1] != 0) return drop("local_unsupported");
        Bytes reply(frame.begin(), frame.begin() + 14 + header.total_length);
        ethernet(reply, in->mac, eth.source, 0x0800);
        put32(reply, 26, destination); put32(reply, 30, source); reply[22] = 64;
        reply[34] = 0; put16(reply, 36, 0);
        put16(reply, 36, internet_checksum(std::span(reply).subspan(34))); ip_checksum(reply);
        return finish({"echo_reply", ingress, std::move(reply)});
    }
    if (header.ttl <= 1) return error(11, 0, "ttl_exceeded");
    const auto route = route_for(*selected, translated.destination);
    if (!route) return error(3, 0, "no_route");
    const auto out_port = find_port(*selected, route->port);
    if (header.total_length > out_port->mtu) {
        if (header.dont_fragment) return error(3, 4, "mtu_exceeded", static_cast<std::uint16_t>(out_port->mtu));
        return drop("fragmentation_required_unsupported");
    }
    const Address next_hop = route->gateway ? route->gateway : translated.destination;
    const Neighbor* neighbor = nullptr;
    for (const auto& n : selected->neighbors)
        if (n.port == route->port && n.ip == next_hop) { neighbor = &n; break; }
    if (!neighbor && selected->neighbor_discovery) {
        for (const auto& entry : neighbor_cache_) {
            if (entry.value.port == route->port && entry.value.ip == next_hop && entry.expires > now) {
                if (entry.negative) return drop("neighbor_negative");
                neighbor = &entry.value; break;
            }
        }
        if (!neighbor) {
            return defer_neighbor(*out_port, next_hop, ingress, frame, now);
        }
    }
    if (!neighbor) return drop("neighbor_missing");

    bool new_flow = false;
    if (!matched && tracked_protocol) {
        if (selected->snat && selected->snat->port == route->port) {
            const auto& snat = *selected->snat;
            std::set<std::uint16_t> used;
            if (!config_->flow_index) {
                for (const auto& f : flows_)
                    if (f.protocol == header.protocol && f.translated.source == snat.ip) used.insert(f.translated.source_port);
            }
            bool allocated = false;
            for (unsigned candidate = snat.first; candidate <= snat.last; ++candidate) {
                const bool occupied = config_->flow_index ? occupied_ports_.contains(nat_key(header.protocol, snat.ip, static_cast<std::uint16_t>(candidate))) :
                    used.contains(static_cast<std::uint16_t>(candidate));
                if (!occupied) {
                    translated.source = snat.ip; translated.source_port = static_cast<std::uint16_t>(candidate);
                    allocated = true; break;
                }
            }
            if (!allocated) return drop("nat_ports_exhausted");
        }
        // Track admitted transport flows so default-deny policy can admit replies.
        new_flow = true;
        if (new_flow) {
            if (flows_.size() >= selected->flow_limit) return drop("flow_table_full");
            if (config_->flow_index) {
                if (reverse_index_.contains({reversed(translated), header.protocol, route->port})) return drop("flow_collision");
            } else {
                for (const auto& f : flows_)
                    if (f.protocol == header.protocol && f.egress == route->port && f.translated == translated)
                        return drop("flow_collision");
            }
        }
    }
    Bytes out(frame.begin(), frame.begin() + 14 + header.total_length);
    ethernet(out, out_port->mac, neighbor->mac, 0x0800); out[22] = header.ttl - 1;
    put32(out, 26, translated.source); put32(out, 30, translated.destination);
    if (tracked_protocol && (new_flow || matched)) {
        put16(out, 34, translated.source_port); put16(out, 36, translated.destination_port);
        const std::size_t checksum_offset = header.protocol == 17 ? 40 : 50;
        // Pure routing does not enable an explicitly absent IPv4 UDP checksum.
        // Translation retains the existing checksum-repair contract.
        const bool preserve_absent_udp = header.protocol == 17 && u16(payload, 6) == 0 && translated == original;
        if (!preserve_absent_udp) {
            put16(out, checksum_offset, 0);
            auto checksum = transport_checksum(std::span(out).subspan(14));
            put16(out, checksum_offset, header.protocol == 17 && checksum == 0 ? 0xffff : checksum);
        }
    }
    ip_checksum(out);
    if (matched) {
        matched->last_seen = now;
        if (next_tcp) matched->tcp = *next_tcp;
        if (config_->flow_index) next_expiry_ = std::min(next_expiry_, expiry(*matched));
    }
    if (new_flow) {
        flows_.push_back({original, translated, ingress, route->port, now, selected,
                          header.protocol, next_tcp.value_or(TcpState{})});
        if (config_->flow_index) index_flow(flows_.size() - 1);
        peak_flows_ = std::max(peak_flows_, flows_.size());
        if (service) ++service_cursor_[service_key];
    }
    if (new_flow || matched) account_backend(new_flow ? flows_.back() : *matched, reply_direction, new_flow, out.size());
    const std::string reason = new_flow ? (header.protocol == 6 ? "new_tcp_mapping" : "new_udp_mapping") :
        matched ? (header.protocol == 6 ? "tracked_tcp" : "tracked_udp") : "forward";
    return finish({reason, route->port, std::move(out)});
}
std::size_t Appliance::FlowKeyHash::operator()(const FlowKey& key) const {
    std::uint64_t value = (std::uint64_t(key.tuple.source) << 32) | key.tuple.destination;
    value ^= (std::uint64_t(key.tuple.source_port) << 48) | (std::uint64_t(key.tuple.destination_port) << 16) | key.protocol;
    value ^= std::hash<std::string>{}(key.port);
    value ^= value >> 30; value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27; value *= 0x94d049bb133111ebULL;
    return static_cast<std::size_t>(value ^ (value >> 31));
}
std::uint64_t Appliance::nat_key(unsigned protocol, Address ip, std::uint16_t port) {
    return (std::uint64_t(protocol) << 48) | (std::uint64_t(ip) << 16) | port;
}
std::uint64_t Appliance::idle_timeout(const Flow& flow) {
    return flow.protocol == 17 ? flow.config->udp_timeout_ms :
        flow.tcp.phase == TcpPhase::established ? flow.config->tcp_established_ms :
        (flow.tcp.phase == TcpPhase::closing || flow.tcp.phase == TcpPhase::reset) ? flow.config->tcp_closing_ms :
        flow.config->tcp_handshake_ms;
}
std::uint64_t Appliance::expiry(const Flow& flow) {
    const auto timeout = idle_timeout(flow);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return flow.last_seen > maximum - timeout ? maximum : flow.last_seen + timeout;
}
void Appliance::index_flow(std::size_t index) {
    const auto& flow = flows_[index];
    forward_index_.emplace(FlowKey{flow.original, flow.protocol, flow.ingress}, index);
    const auto& t = flow.translated;
    reverse_index_.emplace(FlowKey{{t.destination, t.source, t.destination_port, t.source_port}, flow.protocol, flow.egress}, index);
    occupied_ports_.insert(nat_key(flow.protocol, t.source, t.source_port));
    next_expiry_ = std::min(next_expiry_, expiry(flow));
}
void Appliance::expire_flows(std::uint64_t now) {
    if (config_->flow_index && now < next_expiry_) return;
    const auto removed = std::erase_if(flows_, [&](const Flow& f) { return now - f.last_seen >= idle_timeout(f); });
    counters_.expired_flows += removed;
    if (config_->flow_index) {
        next_expiry_ = std::numeric_limits<std::uint64_t>::max();
        if (removed) {
            forward_index_.clear(); reverse_index_.clear(); occupied_ports_.clear();
            for (std::size_t i = 0; i < flows_.size(); ++i) index_flow(i);
        } else {
            for (const auto& flow : flows_) next_expiry_ = std::min(next_expiry_, expiry(flow));
        }
    }
}
const Route* Appliance::route_for(const Config& config, Address destination) {
    ++counters_.route_lookups;
    const auto route = lookup_route(config, destination);
    if (!route) ++counters_.route_misses;
    return route;
}
Decision Appliance::defer_neighbor(const Port& port, Address next_hop, const std::string& ingress,
                                    std::span<const std::uint8_t> frame, std::uint64_t now) {
    if (!port.subnet.contains(next_hop) || invalid_endpoint(*config_, next_hop) || next_hop == port.ip)
        return finish({"neighbor_offlink", {}, {}});
    auto pending = std::find_if(pending_neighbors_.begin(), pending_neighbors_.end(), [&](const auto& n) {
        return n.port == port.id && n.ip == next_hop;
    });
    if (queued_bytes_ + frame.size() > 262144 ||
        (pending == pending_neighbors_.end() && pending_neighbors_.size() >= 32) ||
        (pending != pending_neighbors_.end() && pending->packets.size() >= 4))
        return finish({"neighbor_queue_full", {}, {}});
    if (pending == pending_neighbors_.end()) {
        pending_neighbors_.push_back({port.id, next_hop, now, now, 0, {}});
        pending = std::prev(pending_neighbors_.end());
    }
    pending->packets.push_back({ingress, Bytes(frame.begin(), frame.end())}); queued_bytes_ += frame.size();
    peak_queued_bytes_ = std::max(peak_queued_bytes_, queued_bytes_);
    return finish({"neighbor_pending", {}, {}});
}
std::vector<Decision> Appliance::maintenance(std::uint64_t now) {
    now = std::max(now, clock_); clock_ = now;
    std::vector<Decision> output;
    expire_flows(now);
    if (!config_->neighbor_discovery) return output;
    std::erase_if(neighbor_cache_, [&](const auto& n) { return n.expires <= now; });
    if (now - arp_window_ >= 1000) { arp_window_ = now; arp_requests_in_window_ = 0; }
    std::vector<QueuedPacket> ready;
    for (auto it = pending_neighbors_.begin(); it != pending_neighbors_.end();) {
        const auto cache = std::find_if(neighbor_cache_.begin(), neighbor_cache_.end(), [&](const auto& n) {
            return !n.negative && n.value.port == it->port && n.value.ip == it->ip;
        });
        const bool resolved = cache != neighbor_cache_.end();
        const bool expired = now - it->created >= 5000 || (it->attempts >= 3 && now >= it->next_request);
        if (resolved || expired) {
            for (auto& packet : it->packets) {
                queued_bytes_ -= packet.frame.size();
                if (resolved) ready.push_back(std::move(packet)); else ++neighbor_timeouts_;
            }
            if (!resolved) {
                if (neighbor_cache_.size() >= 128) neighbor_cache_.erase(neighbor_cache_.begin());
                neighbor_cache_.push_back({{it->port, it->ip, {}}, now + 1000, true});
            }
            it = pending_neighbors_.erase(it); continue;
        }
        if (now >= it->next_request && it->attempts < 3 && arp_requests_in_window_ < 8) {
            const auto port = find_port(*config_, it->port);
            Bytes request(42, 0);
            ethernet(request, port->mac, MacAddress{255,255,255,255,255,255}, 0x0806);
            put16(request, 14, 1); put16(request, 16, 0x0800); request[18] = 6; request[19] = 4; put16(request, 20, 1);
            std::copy(port->mac.begin(), port->mac.end(), request.begin() + 22);
            put32(request, 28, port->ip); put32(request, 38, it->ip);
            output.push_back({"arp_request", port->id, std::move(request)});
            ++arp_requests_in_window_; ++neighbor_retries_; ++it->attempts; it->next_request = now + 1000;
        }
        ++it;
    }
    // Re-run all validation/policy/routing under current or flow-pinned config.
    // Never decrement TTL twice or reserve NAT state before resolution succeeds.
    for (auto& packet : ready) {
        ++reprocessed_; reprocessed_bytes_ += packet.frame.size();
        output.push_back(process(packet.ingress, packet.frame, now));
    }
    return output;
}
void Appliance::account_backend(const Flow& flow, bool reverse, bool fresh, std::size_t bytes) {
    // Generation-scoped counters never relabel traffic pinned to an old config.
    if (flow.config != config_) return;
    std::size_t index = 0;
    for (const auto& service : config_->services) {
        for (const auto& backend : service.backends) {
            if (index >= backend_counts_.size()) return;
            if (flow.protocol == service.protocol && flow.ingress == service.ingress &&
                flow.original.destination == service.vip && flow.original.destination_port == service.port &&
                flow.translated.destination == backend.ip && flow.translated.destination_port == backend.port) {
                auto& counts = backend_counts_[index];
                if (fresh) ++counts.new_flows;
                if (reverse) counts.reverse_bytes += bytes;
                else counts.forward_bytes += bytes;
                return;
            }
            ++index;
        }
    }
}
std::string Appliance::metrics() const {
    std::ostringstream out;
    out << "netforge_received_total " << counters_.received << '\n'
        << "netforge_emitted_total " << counters_.emitted << '\n'
        << "netforge_dropped_total " << counters_.dropped << '\n'
        << "netforge_deferred_total " << counters_.deferred << '\n'
        << "netforge_consumed_total " << counters_.consumed << '\n'
        << "netforge_reprocessed_total " << reprocessed_ << '\n'
        << "netforge_udp_mappings " << std::count_if(flows_.begin(), flows_.end(), [](const Flow& f) { return f.protocol == 17; }) << '\n'
        << "netforge_tcp_mappings " << std::count_if(flows_.begin(), flows_.end(), [](const Flow& f) { return f.protocol == 6; }) << '\n'
        << "netforge_config_generation " << config_->generation << '\n';
    out << "netforge_flow_index_enabled " << (config_->flow_index ? 1 : 0) << '\n'
        << "netforge_flow_index_forward_entries " << forward_index_.size() << '\n'
        << "netforge_flow_index_reverse_entries " << reverse_index_.size() << '\n'
        << "netforge_flow_index_occupied_ports " << occupied_ports_.size() << '\n';
    out << "netforge_engine_received_bytes_total " << counters_.received_bytes << '\n'
        << "netforge_engine_emitted_bytes_total " << counters_.emitted_bytes << '\n'
        << "netforge_reprocessed_bytes_total " << reprocessed_bytes_ << '\n'
        << "netforge_flows_expired_total " << counters_.expired_flows << '\n'
        << "netforge_flows_high_water " << peak_flows_ << '\n'
        << "netforge_neighbor_queued_bytes_high_water " << peak_queued_bytes_ << '\n'
        << "netforge_acl_rule_evaluations_total " << counters_.acl_evaluations << '\n'
        << "netforge_acl_rule_matches_total " << counters_.acl_matches << '\n'
        << "netforge_acl_default_decisions_total " << counters_.acl_defaults << '\n'
        << "netforge_route_lookups_total " << counters_.route_lookups << '\n'
        << "netforge_route_misses_total " << counters_.route_misses << '\n';
    out << "netforge_neighbor_cache_entries " << neighbor_cache_.size() << '\n'
        << "netforge_neighbor_pending_targets " << pending_neighbors_.size() << '\n'
        << "netforge_neighbor_queued_bytes " << queued_bytes_ << '\n'
        << "netforge_neighbor_timeouts_total " << neighbor_timeouts_ << '\n'
        << "netforge_arp_requests_total " << neighbor_retries_ << '\n';
    const auto visible_rules = std::min(config_->rules.size(), rule_matches_.size());
    out << "netforge_acl_rule_series_omitted " << config_->rules.size() - visible_rules << '\n';
    for (std::size_t index = 0; index < visible_rules; ++index) {
        out << "netforge_acl_rule_checks{generation=\"" << config_->generation << "\",rule=\"" << index
            << "\"} " << rule_evaluations_[index] << '\n';
        out << "netforge_acl_rule_hits{generation=\"" << config_->generation << "\",rule=\"" << index
            << "\"} " << rule_matches_[index] << '\n';
    }
    std::size_t backend_index = 0, backend_total = 0;
    for (std::size_t service_index = 0; service_index < config_->services.size(); ++service_index) {
        const auto& service = config_->services[service_index];
        backend_total += service.backends.size();
        for (std::size_t index = 0; index < service.backends.size() && backend_index < backend_counts_.size(); ++index, ++backend_index) {
            const auto& counts = backend_counts_[backend_index];
            const auto labels = "{generation=\"" + std::to_string(config_->generation) + "\",service=\"" +
                std::to_string(service_index) + "\",backend=\"" + std::to_string(index) + "\"} ";
            out << "netforge_backend_new_flows" << labels << counts.new_flows << '\n'
                << "netforge_backend_forward_bytes" << labels << counts.forward_bytes << '\n'
                << "netforge_backend_reverse_bytes" << labels << counts.reverse_bytes << '\n';
        }
    }
    out << "netforge_backend_series_omitted " << backend_total - backend_index << '\n';
    for (const auto& [reason, count] : counters_.reasons)
        out << "netforge_decisions_total{reason=\"" << reason << "\"} " << count << '\n';
    return out.str();
}
} // namespace netforge
