// Private-veth acceptance only. No host interfaces or external network needed.
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "fixture.h"
#include "counters.h"

static int counter(struct bpf_object* object, unsigned action, unsigned long long packets,
                   unsigned long long bytes, const char* instance) {
    int map = bpf_object__find_map_fd_by_name(object, "nf_decisions");
    int cpus = libbpf_num_possible_cpus();
    if (map < 0 || cpus < 1 || cpus > 4096) return 1;
    struct nf_xdp_counter* values = calloc((size_t)cpus, sizeof(*values));
    if (!values) return 1;
    unsigned key = action;
    if (bpf_map_lookup_elem(map, &key, values)) { free(values); return 1; }
    unsigned long long total_packets = 0, total_bytes = 0;
    for (int cpu = 0; cpu < cpus; ++cpu) {
        total_packets += values[cpu].packets; total_bytes += values[cpu].bytes;
    }
    free(values);
    if (total_packets != packets || total_bytes != bytes) {
        fprintf(stderr, "FAIL: %s decision%u packets=%llu bytes=%llu expected=%llu/%llu\n",
                instance, action, total_packets, total_bytes, packets, bytes); return 1;
    }
    printf("netforge_xdp_decision_packets_total{instance=\"%s\",action=\"%u\"} %llu\n", instance, action, total_packets);
    printf("netforge_xdp_decision_bytes_total{instance=\"%s\",action=\"%u\"} %llu\n", instance, action, total_bytes);
    return 0;
}

#include "live_packets.h"

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) { fprintf(stderr, "Usage: nf-xdp-live PROGRAM.bpf.o [POLICY.bin]\n"); return 2; }
    unsigned ingress = if_nametoindex("nf-in"), output = if_nametoindex("nf-out");
    unsigned source = if_nametoindex("nf-source"), sink = if_nametoindex("nf-sink");
    if (!ingress || !output || !source || !sink) { perror("private veth inventory"); return 1; }
    __u32 existing = 0;
    if (bpf_xdp_query_id((int)ingress, XDP_FLAGS_SKB_MODE, &existing) || existing) {
        fprintf(stderr, "FAIL: ingress already has XDP or query failed; refusing replacement\n"); return 1;
    }
    struct bpf_object* object = bpf_object__open_file(argv[1], NULL);
    if (!object || libbpf_get_error(object)) return 1;
    int status = 1, attached = 0, sender = -1, receiver = -1, fd = -1;
    struct bpf_object* replacement = NULL;
    if (bpf_object__load(object)) goto cleanup;
    struct bpf_program* program = bpf_object__find_program_by_name(object, "nf_xdp");
    int map = bpf_object__find_map_fd_by_name(object, "nf_policy_map");
    if (!program || map < 0) goto cleanup;
    fd = bpf_program__fd(program);
    struct nf_policy policy; nf_fixture_policy(&policy); unsigned key = 0;
    policy.routes[0].ifindex = output;
    if (argc == 3) {
        FILE* input = fopen(argv[2], "rb");
        if (!input) { perror("compiled policy"); goto cleanup; }
        size_t count = fread(&policy, 1, sizeof(policy), input);
        int extra = fgetc(input); int error = ferror(input); fclose(input);
        if (count != sizeof(policy) || extra != EOF || error || policy.rule_count != 1 ||
            policy.route_count != 1 || policy.default_allow || policy.rules[0].ingress != ingress ||
            policy.routes[0].ifindex != output) {
            fprintf(stderr, "FAIL: compiled acceptance policy ABI/bindings\n"); goto cleanup;
        }
        puts("PASS: compiled project configuration loaded with actual veth interface bindings");
    }
    if (bpf_map_update_elem(map, &key, &policy, BPF_ANY) || bpf_map_freeze(map)) goto cleanup;
    sender = packet_socket(source); receiver = packet_socket(sink);
    if (sender < 0 || receiver < 0) { perror("private packet socket"); goto cleanup; }
    if (bpf_xdp_attach((int)ingress, fd, XDP_FLAGS_SKB_MODE | XDP_FLAGS_UPDATE_IF_NOEXIST, NULL)) {
        fprintf(stderr, "FAIL: generic XDP attach\n"); goto cleanup;
    }
    attached = 1;
    if (exchange(sender, receiver, &policy, ingress, 1, 0) ||
        exchange(sender, receiver, &policy, ingress, 2, 1) ||
        exchange(sender, receiver, &policy, ingress, 3, 0) ||
        exchange(sender, receiver, &policy, ingress, 4, 2) ||
        exchange(sender, receiver, &policy, ingress, 5, 0)) goto cleanup;
    puts("PASS: three real veth redirects match native TTL/MAC/checksum bytes");
    puts("PASS: ACL deny and bad IPv4 checksum absent at egress between positive controls");
    // Prepare a separate program/map pair. A frozen map is never mutated in place.
    replacement = bpf_object__open_file(argv[1], NULL);
    if (!replacement || libbpf_get_error(replacement)) { replacement = NULL; goto cleanup; }
    if (bpf_object__load(replacement)) goto cleanup;
    struct bpf_program* next_program = bpf_object__find_program_by_name(replacement, "nf_xdp");
    int next_map = bpf_object__find_map_fd_by_name(replacement, "nf_policy_map");
    if (!next_program || next_map < 0) goto cleanup;
    int next_fd = bpf_program__fd(next_program), original_fd = fd;
    struct nf_policy denied = policy; denied.rule_count = 0; denied.default_allow = 0;
    if (bpf_map_update_elem(next_map, &key, &denied, BPF_ANY) || bpf_map_freeze(next_map)) goto cleanup;
    errno = 0;
    if (bpf_map_update_elem(next_map, &key, &policy, BPF_ANY) == 0 || errno != EPERM) {
        fprintf(stderr, "FAIL: frozen candidate policy unexpectedly mutable\n"); goto cleanup;
    }
    __u32 original_id = 0, current_id = 0;
    if (bpf_xdp_query_id((int)ingress, XDP_FLAGS_SKB_MODE, &original_id) || !original_id) goto cleanup;
    struct bpf_xdp_attach_opts replace_options = {0}; replace_options.sz = sizeof(replace_options);
    replace_options.old_prog_fd = next_fd; // Deliberately incorrect expected owner.
    if (bpf_xdp_attach((int)ingress, next_fd, XDP_FLAGS_SKB_MODE | XDP_FLAGS_REPLACE, &replace_options) == 0) {
        fd = next_fd;
        fprintf(stderr, "FAIL: stale-owner replacement accepted\n"); goto cleanup;
    }
    if (bpf_xdp_query_id((int)ingress, XDP_FLAGS_SKB_MODE, &current_id) || current_id != original_id ||
        exchange(sender, receiver, &policy, ingress, 6, 0)) goto cleanup;
    puts("PASS: wrong expected-owner replacement rejected; original program/forwarding retained");
    replace_options.old_prog_fd = original_fd;
    if (bpf_xdp_attach((int)ingress, next_fd, XDP_FLAGS_SKB_MODE | XDP_FLAGS_REPLACE, &replace_options)) goto cleanup;
    fd = next_fd;
    if (bpf_xdp_query_id((int)ingress, XDP_FLAGS_SKB_MODE, &current_id) || !current_id || current_id == original_id ||
        exchange(sender, receiver, &denied, ingress, 7, 3)) goto cleanup;
    puts("PASS: prepared frozen deny policy replaces owned program; live packet denied");
    replace_options.old_prog_fd = next_fd;
    if (bpf_xdp_attach((int)ingress, original_fd, XDP_FLAGS_SKB_MODE | XDP_FLAGS_REPLACE, &replace_options)) goto cleanup;
    fd = original_fd;
    if (bpf_xdp_query_id((int)ingress, XDP_FLAGS_SKB_MODE, &current_id) || current_id != original_id ||
        exchange(sender, receiver, &policy, ingress, 8, 0)) goto cleanup;
    puts("PASS: conditional rollback restores original program identity and native-parity forwarding");
    if (counter(object, NF_ROUTE, 5, 320, "original") ||
        counter(object, NF_DROP, 2, 128, "original") ||
        counter(replacement, NF_ROUTE, 0, 0, "replacement") ||
        counter(replacement, NF_DROP, 1, 64, "replacement")) goto cleanup;
    puts("PASS: kernel per-CPU decision packets/bytes correlate across replacement and rollback; retired-instance counters retained");
    status = 0;
cleanup:
    if (attached) {
        struct bpf_xdp_attach_opts options = {0};
        options.sz = sizeof(options); options.old_prog_fd = fd;
        if (bpf_xdp_detach((int)ingress, XDP_FLAGS_SKB_MODE, &options) ||
            bpf_xdp_query_id((int)ingress, XDP_FLAGS_SKB_MODE, &existing) || existing) {
            fprintf(stderr, "FAIL: owned XDP detach verification\n"); status = 1;
        } else puts("PASS: owned XDP program detached and absence verified");
    }
    if (sender >= 0) close(sender);
    if (receiver >= 0) close(receiver);
    if (replacement) bpf_object__close(replacement);
    bpf_object__close(object);
    if (status) fprintf(stderr, "FAIL: private-veth XDP acceptance\n");
    return status;
}
