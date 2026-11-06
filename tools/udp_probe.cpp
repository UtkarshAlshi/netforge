// Bounded lab-only UDP responder/probe; never part of the privileged dataplane.
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned number(const std::string& text, unsigned maximum) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || value == 0 || value > maximum)
        throw std::runtime_error("invalid numeric argument");
    return value;
}
void token(const std::string& value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) throw std::runtime_error("invalid token length");
    for (const unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            throw std::runtime_error("tokens must be alphanumeric, dash or underscore");
}
struct Socket {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    Socket() { if (fd < 0) throw std::runtime_error("UDP socket failed"); }
    ~Socket() { ::close(fd); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};
sockaddr_in endpoint(const std::string& ip, unsigned port) {
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) throw std::runtime_error("invalid IPv4 address");
    return address;
}
void send(int fd, const sockaddr_in& peer, const std::string& text) {
    auto n = sendto(fd, text.data(), text.size(), 0, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    if (n < 0 || static_cast<std::size_t>(n) != text.size()) throw std::runtime_error("UDP send failed");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") { std::cout << "NetForge UDP lab tool 1\n"; return 0; }
        if (argc >= 2 && std::string(argv[1]) == "serve" && argc == 5) {
            const unsigned port = number(argv[2], 65535), seconds = number(argv[4], 30);
            const std::string label = argv[3]; token(label, 32);
            Socket socket; const auto local = endpoint("0.0.0.0", port);
            if (bind(socket.fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
                throw std::runtime_error("UDP bind failed");
            std::cout << "READY backend=" << label << " port=" << port << std::endl;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            unsigned replies = 0;
            while (std::chrono::steady_clock::now() < deadline) {
                pollfd poller{socket.fd, POLLIN, 0};
                const int ready = poll(&poller, 1, 100);
                if (ready < 0) { if (errno == EINTR) continue; throw std::runtime_error("UDP poll failed"); }
                if (poller.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("UDP socket error");
                if (!(poller.revents & POLLIN)) continue;
                std::array<char, 1200> data{}; sockaddr_in peer{}; socklen_t length = sizeof(peer);
                auto received = recvfrom(socket.fd, data.data(), data.size(), MSG_TRUNC,
                                         reinterpret_cast<sockaddr*>(&peer), &length);
                if (received < 0 || static_cast<std::size_t>(received) > data.size()) continue;
                send(socket.fd, peer, label + "|" + std::string(data.data(), received)); ++replies;
            }
            std::cout << "STOP backend=" << label << " replies=" << replies << '\n'; return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "probe" && (argc == 6 || argc == 7)) {
            const auto peer = endpoint(argv[2], number(argv[3], 65535));
            const std::string request = argv[4], label = argv[5]; token(request, 1000); token(label, 32);
            Socket socket;
            if (argc == 7) {
                const auto local = endpoint("0.0.0.0", number(argv[6], 65535));
                if (bind(socket.fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
                    throw std::runtime_error("probe source port unavailable");
            }
            send(socket.fd, peer, request);
            pollfd poller{socket.fd, POLLIN, 0};
            if (poll(&poller, 1, 2000) <= 0 || !(poller.revents & POLLIN))
                throw std::runtime_error("probe timed out or socket error");
            std::array<char, 1200> data{}; sockaddr_in sender{}; socklen_t length = sizeof(sender);
            auto received = recvfrom(socket.fd, data.data(), data.size(), MSG_TRUNC,
                                     reinterpret_cast<sockaddr*>(&sender), &length);
            if (received < 0 || static_cast<std::size_t>(received) > data.size() ||
                sender.sin_addr.s_addr != peer.sin_addr.s_addr || sender.sin_port != peer.sin_port ||
                std::string(data.data(), received) != label + "|" + request)
                throw std::runtime_error("reply endpoint, backend or token mismatch");
            std::cout << "PASS peer=" << argv[2] << ':' << argv[3] << " backend=" << label
                      << " token=" << request << '\n'; return 0;
        }
        std::cerr << "Usage: udp_probe serve PORT LABEL SECONDS\n"
                     "       udp_probe probe IP PORT TOKEN EXPECTED_LABEL [SOURCE_PORT]\n";
        return 2;
    } catch (const std::exception& error) { std::cerr << "udp_probe: " << error.what() << '\n'; return 1; }
}
