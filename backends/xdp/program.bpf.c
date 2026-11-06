// SPDX-License-Identifier: (MIT OR GPL-2.0-only)
#define NETFORGE_BPF
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "policy.h"
#include "counters.h"
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct nf_policy);
} nf_policy_map SEC(".maps");
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, NF_XDP_COUNTER_SLOTS);
    __type(key, __u32);
    __type(value, struct nf_xdp_counter);
} nf_decisions SEC(".maps");
static __always_inline void account(__u32 action, __u32 bytes) {
    struct nf_xdp_counter* count = bpf_map_lookup_elem(&nf_decisions, &action);
    if (count) {
        __sync_fetch_and_add(&count->packets, 1);
        __sync_fetch_and_add(&count->bytes, bytes);
    }
}
SEC("xdp") int nf_xdp(struct xdp_md* context) {
    __u32 key = 0, output = 0;
    const struct nf_policy* policy = bpf_map_lookup_elem(&nf_policy_map, &key);
    if (!policy) { account(NF_PASS, context->data_end - context->data); return XDP_PASS; }
    __u8* data = (void*)(long)context->data;
    __u8* end = (void*)(long)context->data_end;
    int action = nf_process(data, end, policy, context->ingress_ifindex, &output);
    account((__u32)action, context->data_end - context->data);
    if (action == NF_ROUTE) {
        if (output == context->ingress_ifindex) return XDP_TX;
        return bpf_redirect(output, 0);
    }
    return action;
}
char LICENSE[] SEC("license") = "Dual MIT/GPL";
