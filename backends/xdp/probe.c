#include <net/if.h>
#include <stdlib.h>
#include "live_packets.h"
int main(int argc, char** argv) {
    if (argc != 3 || (strcmp(argv[2], "allow") && strcmp(argv[2], "deny"))) return 2;
    struct nf_policy policy;
    FILE* file = fopen(argv[1], "rb"); if (!file) return 1;
    size_t length = fread(&policy, 1, sizeof(policy), file);
    int extra = fgetc(file), error = ferror(file); fclose(file);
    if (length != sizeof(policy) || extra != EOF || error) return 1;
    unsigned ingress = if_nametoindex("nf-in"), source = if_nametoindex("nf-source"), sink = if_nametoindex("nf-sink");
    if (!ingress || !source || !sink) return 1;
    int sender = packet_socket(source), receiver = packet_socket(sink);
    if (sender < 0 || receiver < 0) { if (sender >= 0) close(sender); if (receiver >= 0) close(receiver); return 1; }
    int denied = !strcmp(argv[2], "deny") ? 3 : 0;
    int result = exchange(sender, receiver, &policy, ingress, 100, denied);
    close(sender); close(receiver);
    if (!result) puts(denied ? "PASS: API-applied XDP deny observed on private veth" : "PASS: API-applied XDP forwarding matches native TTL/MAC/checksum bytes");
    return result;
}
