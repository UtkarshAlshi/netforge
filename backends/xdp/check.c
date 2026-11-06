// Kernel verifier/test-run acceptance only. Never attaches to an interface.
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixture.h"
static int quiet;
static int check(int fd, const char* name, unsigned char* packet, unsigned length, unsigned expected) {
    unsigned char output[256] = {0};
    struct bpf_test_run_opts options = {0}; options.sz = sizeof(options);
    options.data_in = packet; options.data_size_in = length;
    options.data_out = output; options.data_size_out = sizeof(output); options.repeat = 1;
    if (bpf_prog_test_run_opts(fd, &options) != 0) {
        fprintf(stderr, "FAIL: %s test-run: %s\n", name, strerror(errno)); return 1;
    }
    if (options.retval != expected) {
        fprintf(stderr, "FAIL: %s action=%u expected=%u\n", name, options.retval, expected); return 1;
    }
    if (expected == NF_ROUTE) {
        unsigned char reference[256]; memcpy(reference, packet, length);
        struct nf_policy policy; nf_fixture_policy(&policy); unsigned target = 0;
        if (nf_process(reference, reference + length, &policy, 0, &target) != NF_ROUTE ||
            options.data_size_out != length || memcmp(reference, output, length) ||
            output[22] != reference[22] || nf_ip_checksum(output + 14) != 0) {
            fprintf(stderr, "FAIL: kernel/native routed packet byte parity\n"); return 1;
        }
    }
    if (expected != NF_ROUTE && (options.data_size_out != length || memcmp(packet, output, length))) {
        fprintf(stderr, "FAIL: punt/drop unexpectedly changed packet bytes\n"); return 1;
    }
    if (!quiet) printf("PASS: kernel XDP %s action=%u\n", name, options.retval);
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "Usage: nf-xdp-check PROGRAM.bpf.o\n"); return 2; }
    struct bpf_object* object = bpf_object__open_file(argv[1], NULL);
    if (!object || libbpf_get_error(object)) { fprintf(stderr, "FAIL: open BPF object\n"); return 1; }
    if (bpf_object__load(object)) { fprintf(stderr, "FAIL: BPF load/verifier or capability policy; no attachment attempted\n"); bpf_object__close(object); return 1; }
    struct bpf_program* program = bpf_object__find_program_by_name(object, "nf_xdp");
    int map = bpf_object__find_map_fd_by_name(object, "nf_policy_map");
    if (!program || map < 0) { fprintf(stderr, "FAIL: program/policy map not found\n"); bpf_object__close(object); return 1; }
    struct nf_policy policy; nf_fixture_policy(&policy); unsigned key = 0;
    if (bpf_map_update_elem(map, &key, &policy, BPF_ANY) || bpf_map_freeze(map)) {
        fprintf(stderr, "FAIL: initialize/freeze immutable policy: %s\n", strerror(errno)); bpf_object__close(object); return 1;
    }
    unsigned char packet[64]; unsigned failures = 0;
    const int fd = bpf_program__fd(program);
    nf_fixture_packet(packet); failures += check(fd, "IPv4 ACL/route/TTL/MAC", packet, 64, NF_ROUTE);
    nf_fixture_packet(packet); nf_put16(packet + 36, 9001); failures += check(fd, "default-deny", packet, 64, NF_DROP);
    nf_fixture_packet(packet); packet[24] ^= 1; failures += check(fd, "invalid IPv4 checksum", packet, 64, NF_DROP);
    nf_fixture_packet(packet); packet[22] = 1; nf_put16(packet + 24, 0); nf_put16(packet + 24, nf_ip_checksum(packet + 14));
    failures += check(fd, "TTL exception punt", packet, 64, NF_PASS);
    nf_fixture_packet(packet); nf_put16(packet + 20, 0x2000); nf_put16(packet + 24, 0); nf_put16(packet + 24, nf_ip_checksum(packet + 14));
    failures += check(fd, "fragment exception punt", packet, 64, NF_PASS);
    nf_fixture_packet(packet); nf_put16(packet + 12, 0x0806); failures += check(fd, "ARP punt", packet, 64, NF_PASS);
    // The kernel test-run API requires an Ethernet-sized input. Shorter frames
    // remain covered by the native malformed corpus.
    quiet = 1;
    for (unsigned length = 14; length < 64; ++length) {
        nf_fixture_packet(packet);
        failures += check(fd, "truncated Ethernet/IPv4/UDP", packet, length, NF_DROP);
    }
    unsigned random = 0x4e465847U;
    for (unsigned trial = 0; trial < 256; ++trial) {
        nf_fixture_packet(packet);
        random = random * 1664525U + 1013904223U;
        unsigned offset = (random >> 16) % sizeof(packet);
        random = random * 1664525U + 1013904223U;
        packet[offset] ^= (unsigned char)((random >> 24) | 1U);
        // Repair half of the IPv4 headers to exercise deeper parser/ACL paths.
        if (trial & 1U) {
            nf_put16(packet + 24, 0);
            nf_put16(packet + 24, nf_ip_checksum(packet + 14));
        }
        unsigned char reference[64]; memcpy(reference, packet, sizeof(packet));
        unsigned target = 0;
        unsigned expected = nf_process(reference, reference + sizeof(reference), &policy, 0, &target);
        failures += check(fd, "deterministic mutation parity", packet, sizeof(packet), expected);
    }
    bpf_object__close(object);
    if (failures) return 1;
    puts("PASS: real kernel verifier and 312 XDP test-run cases (six fixtures, 50 truncations, 256 mutations); no interface attachment");
    return 0;
}
