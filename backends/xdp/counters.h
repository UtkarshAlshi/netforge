#pragma once
#include <linux/types.h>
// Per-program decision counters. ROUTE is classification, not delivery success.
#define NF_XDP_COUNTER_SLOTS 5
struct nf_xdp_counter { __u64 packets; __u64 bytes; };
