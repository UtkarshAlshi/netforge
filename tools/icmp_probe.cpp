// Lab-only probe: verify a translated UDP port-unreachable reaches its client.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc, char** argv) {
    int fd = -1;
    try {
        if (argc != 3) throw std::runtime_error("Usage: icmp_probe IP PORT");
        unsigned port = 0; const std::string text(argv[2]);
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
        if (error != std::errc{} || end != text.data() + text.size() || port == 0 || port > 65535)
            throw std::runtime_error("invalid port");
        sockaddr_in peer{}; peer.sin_family = AF_INET; peer.sin_port = htons(port);
        if (inet_pton(AF_INET, argv[1], &peer.sin_addr) != 1) throw std::runtime_error("invalid IPv4");
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0 || connect(fd, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) != 0)
            throw std::runtime_error("UDP connect failed");
        const std::string payload = "netforge-related-icmp";
        if (send(fd, payload.data(), payload.size(), 0) != static_cast<ssize_t>(payload.size()))
            throw std::runtime_error("UDP send failed");
        pollfd poller{fd, POLLIN | POLLERR, 0};
        if (poll(&poller, 1, 2000) <= 0 || !(poller.revents & POLLERR))
            throw std::runtime_error("no asynchronous ICMP error within 2 seconds");
        int socket_error = 0; socklen_t size = sizeof(socket_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) != 0 || socket_error != ECONNREFUSED)
            throw std::runtime_error("expected port-unreachable ECONNREFUSED");
        close(fd);
        std::cout << "PASS: translated ICMP port-unreachable reached original UDP client via "
                  << argv[1] << ':' << port << '\n';
        return 0;
    } catch (const std::exception& error) {
        if (fd >= 0) close(fd);
        std::cerr << "icmp_probe: " << error.what() << '\n'; return 1;
    }
}
