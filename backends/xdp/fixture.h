#pragma once
#include "policy.h"
static inline void nf_fixture_policy(struct nf_policy* policy) {
    __builtin_memset(policy, 0, sizeof(*policy));
    policy->rule_count = 1; policy->route_count = 1;
    const __u8 ingress[6] = {2, 0x88, 0, 0, 0, 2};
    const __u8 source[6] = {2, 0x89, 0, 0, 0, 1};
    const __u8 destination[6] = {2, 0x89, 0, 0, 0, 2};
    for (int i = 0; i < 6; ++i) {
        policy->ingress_mac[i] = ingress[i];
        policy->routes[0].source_mac[i] = source[i];
        policy->routes[0].destination_mac[i] = destination[i];
    }
    policy->rules[0].source = 0x0a580000; policy->rules[0].source_prefix = 24;
    policy->rules[0].destination = 0x0a590000; policy->rules[0].destination_prefix = 24;
    policy->rules[0].protocol = 17; policy->rules[0].dport = 9000; policy->rules[0].allow = 1;
    policy->routes[0].network = 0x0a590000; policy->routes[0].prefix = 24;
    policy->routes[0].ifindex = 7; policy->routes[0].mtu = 1500;
}
static inline void nf_fixture_packet(__u8* packet) {
    __builtin_memset(packet, 0, 64);
    const __u8 destination[6] = {2, 0x88, 0, 0, 0, 2};
    const __u8 source[6] = {2, 0x88, 0, 0, 0, 1};
    for (int i = 0; i < 6; ++i) { packet[i] = destination[i]; packet[6 + i] = source[i]; }
    nf_put16(packet + 12, 0x0800); packet[14] = 0x45; nf_put16(packet + 16, 50);
    packet[22] = 64; packet[23] = 17;
    packet[26] = 10; packet[27] = 88; packet[29] = 2;
    packet[30] = 10; packet[31] = 89; packet[33] = 2;
    nf_put16(packet + 34, 40000); nf_put16(packet + 36, 9000); nf_put16(packet + 38, 30);
    nf_put16(packet + 24, nf_ip_checksum(packet + 14));
    __u32 sum = 17 + 30;
    for (int i = 0; i < 4; ++i) sum += nf_u16(packet + 26 + 2 * i);
    for (int i = 0; i < 15; ++i) sum += nf_u16(packet + 34 + 2 * i);
    sum = (sum & 65535) + (sum >> 16); sum = (sum & 65535) + (sum >> 16);
    const __u16 checksum = (__u16)~sum;
    nf_put16(packet + 40, checksum ? checksum : 65535);
}
