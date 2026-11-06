#pragma once
#ifdef NETFORGE_BPF
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
#endif
#define NF_DROP 1
#define NF_PASS 2
#define NF_ROUTE 4
#define NF_RULES 32
#define NF_ROUTES 16
struct nf_rule {
    __u32 ingress, source, destination;
    __u16 sport, dport;
    __u8 source_prefix, destination_prefix, protocol, allow;
};
struct nf_route {
    __u32 network, ifindex;
    __u16 mtu;
    __u8 prefix, source_mac[6], destination_mac[6];
};
struct nf_policy {
    __u32 rule_count, route_count, default_allow;
    __u8 ingress_mac[6];
    struct nf_rule rules[NF_RULES];
    struct nf_route routes[NF_ROUTES];
};
static inline __u16 nf_u16(const __u8* p) { return ((__u16)p[0] << 8) | p[1]; }
static inline __u32 nf_u32(const __u8* p) { return ((__u32)p[0] << 24) | ((__u32)p[1] << 16) | ((__u32)p[2] << 8) | p[3]; }
static inline void nf_put16(__u8* p, __u16 value) { p[0] = value >> 8; p[1] = value; }
static inline __u16 nf_ip_checksum(const __u8* ip) {
    __u32 sum = 0;
    for (int i = 0; i < 10; ++i) sum += nf_u16(ip + 2 * i);
    sum = (sum & 65535) + (sum >> 16); sum = (sum & 65535) + (sum >> 16);
    return (__u16)~sum;
}
static inline int nf_prefix(__u32 ip, __u32 network, __u8 prefix) {
    if (prefix > 32) return 0;
    const __u32 mask = prefix ? 0xffffffffU << (32 - prefix) : 0;
    return (ip & mask) == network;
}
// Stateless IPv4 routing/ACL subset. Exceptions PASS, never emulate NAT/state.
static inline int nf_process(__u8* data, __u8* end, const struct nf_policy* policy, __u32 ingress, __u32* output) {
    if (data + 14 > end) return NF_DROP;
    if (nf_u16(data + 12) != 0x0800) return NF_PASS;
    if (data + 34 > end || (data[14] >> 4) != 4) return NF_DROP;
    if (data[14] != 0x45) return NF_PASS;
    const __u16 total = nf_u16(data + 16);
    if (total < 20) return NF_DROP;
#ifdef NETFORGE_BPF
    // The verifier tracks packet bounds through comparisons, not pointer subtraction.
    if (data + 14 + total > end) return NF_DROP;
    // Keep subsequent fixed transport-header bounds checks in LLVM output.
    // LLVM can infer them from total; the kernel verifier cannot infer that
    // relationship when validating fixed-offset packet reads.
    __asm__ volatile("" : "+r"(end));
#else
    if (total > (__u32)(end - data) - 14) return NF_DROP;
#endif
    if (nf_ip_checksum(data + 14) != 0) return NF_DROP;
    if (nf_u16(data + 20) & 0x8000) return NF_DROP;
    if (nf_u16(data + 20) & 0x3fff) return NF_PASS;
    if (policy->rule_count > NF_RULES || policy->route_count > NF_ROUTES || policy->default_allow > 1) return NF_DROP;
    __u8 any_source = 0;
    for (int i = 0; i < 6; ++i) {
        if (data[i] != policy->ingress_mac[i]) return NF_PASS;
        any_source |= data[6 + i];
    }
    if (!any_source || (data[6] & 1)) return NF_DROP;
    const __u32 source = nf_u32(data + 26), destination = nf_u32(data + 30);
    if (!(source >> 24) || (source >> 24) == 127 || (source >> 28) >= 14 ||
        !(destination >> 24) || (destination >> 24) == 127 || (destination >> 28) >= 14) return NF_DROP;
    __u16 sport = 0, dport = 0;
    const __u8 protocol = data[23];
    if (protocol == 17) {
        if (total < 28 || data + 42 > end || nf_u16(data + 38) != total - 20) return NF_DROP;
        sport = nf_u16(data + 34); dport = nf_u16(data + 36);
    } else if (protocol == 6) {
        if (total < 40 || data + 54 > end || (data[46] >> 4) < 5 || (data[46] >> 4) * 4 > total - 20) return NF_DROP;
        sport = nf_u16(data + 34); dport = nf_u16(data + 36);
    } else if (protocol == 1) {
        if (total < 28 || data + 42 > end) return NF_DROP;
    } else return NF_PASS;
    int allow = policy->default_allow;
    for (int i = 0; i < NF_RULES; ++i) {
        if ((__u32)i >= policy->rule_count) break;
        const struct nf_rule* rule = &policy->rules[i];
        if (rule->source_prefix > 32 || rule->destination_prefix > 32 || rule->allow > 1) return NF_DROP;
        if ((!rule->ingress || rule->ingress == ingress) && (!rule->protocol || rule->protocol == protocol) &&
            nf_prefix(source, rule->source, rule->source_prefix) && nf_prefix(destination, rule->destination, rule->destination_prefix) &&
            (!rule->sport || rule->sport == sport) && (!rule->dport || rule->dport == dport)) {
            allow = rule->allow; break;
        }
    }
    if (!allow) return NF_DROP;
    if (data[22] <= 1) return NF_PASS;
    __u32 selected = NF_ROUTES;
    __u8 longest = 0;
    for (int i = 0; i < NF_ROUTES; ++i) {
        if ((__u32)i >= policy->route_count) break;
        const struct nf_route* route = &policy->routes[i];
        if (route->prefix > 32) return NF_DROP;
        if (nf_prefix(destination, route->network, route->prefix) && (selected == NF_ROUTES || route->prefix > longest)) {
            selected = i; longest = route->prefix;
        }
    }
    if (selected >= NF_ROUTES) return NF_PASS;
    const struct nf_route* route = &policy->routes[selected];
    if (!route->ifindex || total > route->mtu) return NF_PASS;
    for (int i = 0; i < 6; ++i) { data[i] = route->destination_mac[i]; data[6 + i] = route->source_mac[i]; }
    --data[22]; data[24] = 0; data[25] = 0;
    nf_put16(data + 24, nf_ip_checksum(data + 14));
    *output = route->ifindex;
    return NF_ROUTE;
}
