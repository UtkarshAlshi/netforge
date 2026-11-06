// Real UDP round trips and TCP service/FIN frames across private AF_PACKET workers.
#include "../backends/xdp/fixture.h"
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <fstream>
#include <csignal>
#include <set>
#include <stdexcept>
static void require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
struct Socket {
    int fd = -1;
    explicit Socket(const char* device) {
        fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
        sockaddr_ll address{}; address.sll_family = AF_PACKET; address.sll_protocol = htons(ETH_P_IP);
        address.sll_ifindex = static_cast<int>(if_nametoindex(device));
        if (fd < 0 || !address.sll_ifindex || bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
            if (fd >= 0) close(fd);
            throw std::runtime_error("private packet socket/bind");
        }
    }
    ~Socket() { close(fd); }
};
static void repair(unsigned char* packet) {
    nf_put16(packet + 24, 0); nf_put16(packet + 24, nf_ip_checksum(packet + 14));
    unsigned length = packet[23] == 6 ? 20 : 30, offset = packet[23] == 6 ? 50 : 40;
    nf_put16(packet + offset, 0); unsigned sum = packet[23] + length;
    for (int i = 0; i < 4; ++i) sum += nf_u16(packet + 26 + 2 * i);
    for (unsigned i = 0; i < length / 2; ++i) sum += nf_u16(packet + 34 + 2 * i);
    sum = (sum & 65535) + (sum >> 16); sum = (sum & 65535) + (sum >> 16);
    auto value = static_cast<__u16>(~sum); nf_put16(packet + offset, value ? value : 65535);
}
static void put32(unsigned char* data, unsigned value) {
    for (unsigned index = 0; index < 4; ++index) data[index] = static_cast<unsigned char>(value >> (24 - 8 * index));
}
static std::array<unsigned char, 64> tcp(bool server, unsigned sport, unsigned dport,
                                      unsigned sequence, unsigned acknowledgement, unsigned flags) {
    std::array<unsigned char, 64> packet{}; nf_fixture_packet(packet.data());
    std::fill(packet.begin() + 34, packet.end(), 0); packet[23] = 6; nf_put16(packet.data() + 16, 40);
    if (server) {
        const unsigned char destination[6] = {2, 0x89, 0, 0, 0, 1}, peer[6] = {2, 0x89, 0, 0, 0, 2};
        std::memcpy(packet.data(), destination, 6); std::memcpy(packet.data() + 6, peer, 6);
        put32(packet.data() + 26, 0x0a590002); put32(packet.data() + 30, 0x0a590001);
    } else put32(packet.data() + 30, 0x0a580064); // Service VIP 10.88.0.100.
    nf_put16(packet.data() + 34, static_cast<__u16>(sport)); nf_put16(packet.data() + 36, static_cast<__u16>(dport));
    put32(packet.data() + 38, sequence); put32(packet.data() + 42, acknowledgement);
    packet[46] = 0x50; packet[47] = static_cast<unsigned char>(flags); nf_put16(packet.data() + 48, 4096);
    repair(packet.data()); return packet;
}
static std::array<unsigned char, 64> receive(int fd, unsigned expected_length = 64) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd ready{fd, POLLIN, 0}; int result = poll(&ready, 1, 100);
        require(result >= 0, "private receive poll"); if (!result) continue;
        std::array<unsigned char, 256> bytes{}; sockaddr_ll sender{}; socklen_t size = sizeof(sender);
        auto length = recvfrom(fd, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&sender), &size);
        if (sender.sll_pkttype == PACKET_OUTGOING) continue;
        require(length == static_cast<ssize_t>(expected_length), "unexpected private frame length");
        std::array<unsigned char, 64> packet{}; std::memcpy(packet.data(), bytes.data(), expected_length); return packet;
    }
    throw std::runtime_error("private UDP receive deadline exceeded");
}
int main(int argc, char** argv) {
    try {
        auto copy_config = [](const char* source) {
            std::ifstream input(source, std::ios::binary);
            std::array<char, 65537> bytes{}; input.read(bytes.data(), bytes.size());
            const auto count = input.gcount();
            require(input.eof() && count > 0 && count <= 65536, "bounded harness config read");
            std::ofstream output("/tmp/netforge-worker-active.conf", std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), count); output.close();
            require(bool(output), "harness config write");
        };
        if (argc == 2 && std::string(argv[1]) == "--launch") {
            copy_config("/tmp/netforge-workers/workers.conf");
            std::ofstream pidfile("/tmp/netforge-worker-runtime.pid");
            pidfile << getpid() << '\n'; pidfile.close(); require(bool(pidfile), "harness PID write");
            execl("/tmp/netforge-workers/netforge", "netforge", "run-workers", "/tmp/netforge-worker-active.conf", "2", nullptr);
            throw std::runtime_error("harness exec failed");
        }
        if (argc == 2 && std::string(argv[1]) == "--update") {
            int pid = 0; std::ifstream pidfile("/tmp/netforge-worker-runtime.pid"); pidfile >> pid;
            require(bool(pidfile) && pid > 1, "invalid harness PID");
            std::array<char, 4096> executable{};
            const auto link = std::string("/proc/") + std::to_string(pid) + "/exe";
            const auto length = readlink(link.c_str(), executable.data(), executable.size());
            require(length > 0 && std::string(executable.data(), static_cast<std::size_t>(length)) == "/tmp/netforge-workers/netforge",
                    "harness PID is not owned NetForge executable");
            copy_config("/tmp/netforge-workers/next.conf");
            require(kill(pid, SIGHUP) == 0, "harness reload signal");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--flood") {
            Socket source("nf-source"); std::array<unsigned char, 64> packet{};
            nf_fixture_packet(packet.data()); repair(packet.data());
            for (unsigned index = 0; index < 10000; ++index)
                require(send(source.fd, packet.data(), packet.size(), 0) == static_cast<ssize_t>(packet.size()), "bounded flood injection");
            std::cout << "PASS: injected10000 private-veth frames for socket pressure test\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--reordered") {
            Socket source("nf-source"), sink("nf-sink");
            // One flow pins all deliberately reordered/duplicate packets to one owner.
            std::array<std::array<unsigned char, 64>, 18> returns{};
            const std::array<unsigned, 18> order{15, 0, 14, 1, 13, 2, 12, 3, 11, 4, 10, 5, 9, 6, 8, 7, 3, 15};
            unsigned nat = 0;
            for (unsigned token : order) {
                std::array<unsigned char, 64> packet{}; nf_fixture_packet(packet.data());
                nf_put16(packet.data() + 34, 10000); packet[42] = static_cast<unsigned char>(token);
                repair(packet.data()); require(send(source.fd, packet.data(), 64, 0) == 64, "reordered injection");
            }
            for (unsigned index = 0; index < order.size(); ++index) {
                auto packet = receive(sink.fd); auto checked = packet; repair(checked.data());
                require(checked == packet && packet[42] == order[index] && packet[22] == 63 &&
                        nf_u32(packet.data() + 26) == 0x0a590001 && nf_u32(packet.data() + 30) == 0x0a590002 &&
                        nf_u16(packet.data() + 36) == 9000, "reordered forward payload/tuple/checksum");
                const unsigned allocated = nf_u16(packet.data() + 34);
                if (!nat) nat = allocated;
                require(allocated == nat && nat >= 40000 && nat <= 40031, "reordered flow affinity");
                const unsigned char destination[6] = {2, 0x89, 0, 0, 0, 1}, peer[6] = {2, 0x89, 0, 0, 0, 2};
                std::memcpy(packet.data(), destination, 6); std::memcpy(packet.data() + 6, peer, 6);
                packet[29] = 2; packet[33] = 1;
                nf_put16(packet.data() + 34, 9000); nf_put16(packet.data() + 36, static_cast<__u16>(nat));
                packet[22] = 64; repair(packet.data()); returns[index] = packet;
            }
            for (auto it = returns.rbegin(); it != returns.rend(); ++it)
                require(send(sink.fd, it->data(), 64, 0) == 64, "reordered reverse injection");
            for (unsigned index = 0; index < order.size(); ++index) {
                auto packet = receive(source.fd); auto checked = packet; repair(checked.data());
                require(checked == packet && packet[42] == order[order.size() - 1 - index] && packet[22] == 63 &&
                        nf_u32(packet.data() + 26) == 0x0a590002 && nf_u32(packet.data() + 30) == 0x0a580002 &&
                        nf_u16(packet.data() + 34) == 9000 && nf_u16(packet.data() + 36) == 10000,
                        "reordered reverse payload/tuple/checksum");
            }
            std::cout << "PASS:18 deliberately reordered/duplicate same-flow UDP frames in each direction preserve payloads, NAT tuple, checksums and affinity\n";
            return 0;
        }
        const bool udp_only = argc == 2 && std::string(argv[1]) == "--udp-only";
        const bool deny_new = argc == 2 && std::string(argv[1]) == "--deny-new";
        require(argc == 1 || udp_only || deny_new, "invalid probe mode");
        Socket source("nf-source"), sink("nf-sink");
        if (deny_new) {
            std::array<unsigned char, 64> packet{}; nf_fixture_packet(packet.data());
            nf_put16(packet.data() + 34, 30000); repair(packet.data());
            require(send(source.fd, packet.data(), 64, 0) == 64, "denied request injection");
            pollfd ready{sink.fd, POLLIN, 0};
            require(poll(&ready, 1, 500) == 0, "new flow unexpectedly reached backend");
            std::cout << "PASS: new UDP flow absent at backend after deny reload\n";
            return 0;
        }
        std::array<std::array<unsigned char, 64>, 16> requests{}, replies{};
        for (unsigned index = 0; index < 16; ++index) {
            auto& packet = requests[index]; nf_fixture_packet(packet.data());
            nf_put16(packet.data() + 34, static_cast<__u16>(10000 + index)); packet[42] = static_cast<unsigned char>(index);
            repair(packet.data()); require(send(source.fd, packet.data(), 64, 0) == 64, "request injection");
        }
        std::set<unsigned> ports, tokens;
        for (unsigned index = 0; index < 16; ++index) {
            auto packet = receive(sink.fd); auto token = packet[42];
            auto checked = packet; repair(checked.data()); require(checked == packet, "forward wire IP/UDP checksums");
            require(token < 16 && tokens.insert(token).second, "forward token uniqueness");
            require(nf_u32(packet.data() + 26) == 0x0a590001 && nf_u32(packet.data() + 30) == 0x0a590002 &&
                    packet[22] == 63 && nf_ip_checksum(packet.data() + 14) == 0, "forward SNAT/TTL/checksum");
            unsigned nat = nf_u16(packet.data() + 34);
            require(nat >= 40000 && nat <= 40031 && ports.insert(nat).second, "unique bounded live PAT");
            const unsigned char destination[6] = {2, 0x89, 0, 0, 0, 1}, peer[6] = {2, 0x89, 0, 0, 0, 2};
            std::memcpy(packet.data(), destination, 6); std::memcpy(packet.data() + 6, peer, 6);
            packet[29] = 2; packet[33] = 1; // Reverse 10.89.0.1 -> 10.89.0.2.
            nf_put16(packet.data() + 34, 9000); nf_put16(packet.data() + 36, static_cast<__u16>(nat));
            packet[22] = 64; repair(packet.data()); replies[index] = packet;
        }
        for (auto& packet : replies) require(send(sink.fd, packet.data(), 64, 0) == 64, "reply injection");
        tokens.clear();
        for (unsigned index = 0; index < 16; ++index) {
            auto packet = receive(source.fd); auto token = packet[42];
            auto checked = packet; repair(checked.data()); require(checked == packet, "return wire IP/UDP checksums");
            require(token < 16 && tokens.insert(token).second, "return token uniqueness");
            require(nf_u32(packet.data() + 26) == 0x0a590002 && nf_u32(packet.data() + 30) == 0x0a580002 &&
                    nf_u16(packet.data() + 34) == 9000 && nf_u16(packet.data() + 36) == 10000 + token &&
                    packet[22] == 63 && nf_ip_checksum(packet.data() + 14) == 0, "reverse NAT tuple/TTL/checksum");
        }
        std::cout << "PASS: 16 real AF_PACKET worker UDP NAT round trips with unique PAT and restored client tuples\n";
        if (udp_only) return 0;
        // TCP and UDP have separate PAT namespaces in the reference engine.
        // A numeric port may be reused across protocols, but not within TCP.
        std::set<unsigned> tcp_ports;
        for (unsigned index = 0; index < 4; ++index) {
            unsigned client = 20000 + index, nat = 0;
            auto forward = [&](unsigned sequence, unsigned acknowledgement, unsigned flags) {
                auto packet = tcp(false, client, 9000, sequence, acknowledgement, flags);
                require(send(source.fd, packet.data(), 54, 0) == 54, "TCP client frame injection");
                auto wire = receive(sink.fd, 54); auto checked = wire; repair(checked.data());
                require(checked == wire && wire[23] == 6 && wire[22] == 63 && unsigned(wire[47]) == flags &&
                        nf_u32(wire.data() + 26) == 0x0a590001 && nf_u32(wire.data() + 30) == 0x0a590002,
                        "TCP service forward tuple/checksums");
                unsigned allocated = nf_u16(wire.data() + 34);
                if (!nat) { require(allocated >= 40000 && allocated <= 40031 && tcp_ports.insert(allocated).second, "TCP PAT uniqueness within protocol"); nat = allocated; }
                else require(allocated == nat, "established TCP keeps NAT port");
            };
            auto backward = [&](unsigned sequence, unsigned acknowledgement, unsigned flags) {
                auto packet = tcp(true, 9000, nat, sequence, acknowledgement, flags);
                require(send(sink.fd, packet.data(), 54, 0) == 54, "TCP backend frame injection");
                auto wire = receive(source.fd, 54); auto checked = wire; repair(checked.data());
                require(checked == wire && wire[22] == 63 && unsigned(wire[47]) == flags &&
                        nf_u32(wire.data() + 26) == 0x0a580064 && nf_u32(wire.data() + 30) == 0x0a580002 &&
                        nf_u16(wire.data() + 34) == 9000 && unsigned(nf_u16(wire.data() + 36)) == client,
                        "TCP reverse service tuple/checksums");
            };
            forward(100, 0, 2); backward(700, 101, 0x12); forward(101, 701, 0x10);
            forward(101, 701, 0x11); backward(701, 102, 0x11); forward(102, 702, 0x10);
        }
        std::cout << "PASS: four real TCP service handshakes, reverse NAT and FIN exchanges through worker owners\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
