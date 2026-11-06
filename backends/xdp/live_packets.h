#pragma once
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "fixture.h"
static int packet_socket(unsigned index) {
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
    struct sockaddr_ll address = {0};
    address.sll_family = AF_PACKET; address.sll_protocol = htons(ETH_P_IP);
    address.sll_ifindex = (int)index;
    if (fd >= 0 && bind(fd, (struct sockaddr*)&address, sizeof(address)) == 0) return fd;
    if (fd >= 0) close(fd);
    return -1;
}

static int exchange(int sender, int receiver, const struct nf_policy* policy,
                    unsigned ingress, unsigned sequence, int denied) {
    unsigned char packet[64], expected[64], received[256];
    nf_fixture_packet(packet);
    nf_put16(packet + 18, (__u16)sequence);
    nf_put16(packet + 24, 0); nf_put16(packet + 24, nf_ip_checksum(packet + 14));
    if (denied == 1) nf_put16(packet + 36, 9001);
    if (denied == 2) packet[24] ^= 1;
    memcpy(expected, packet, sizeof(packet)); unsigned target = 0;
    int action = nf_process(expected, expected + sizeof(expected), policy, ingress, &target);
    if (action != (denied ? NF_DROP : NF_ROUTE)) return 1;
    if (send(sender, packet, sizeof(packet), 0) != (ssize_t)sizeof(packet)) {
        perror("send private veth frame"); return 1;
    }
    struct pollfd ready = {.fd = receiver, .events = POLLIN};
    int result = poll(&ready, 1, denied ? 200 : 1000);
    if (denied) {
        if (result != 0) { fprintf(stderr, "FAIL: denied frame reached egress or poll failed\n"); return 1; }
        return 0;
    }
    if (result != 1) { fprintf(stderr, "FAIL: routed frame did not reach private egress\n"); return 1; }
    ssize_t length = recv(receiver, received, sizeof(received), MSG_DONTWAIT);
    if (length != (ssize_t)sizeof(expected) || memcmp(received, expected, sizeof(expected))) {
        fprintf(stderr, "FAIL: live egress differs from native route bytes\n"); return 1;
    }
    return 0;
}

