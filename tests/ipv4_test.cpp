#include "netforge/ipv4.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
using namespace netforge;
void expect_error(std::span<const std::uint8_t> bytes, IPv4Error error) {
    auto result = parse_ipv4(bytes);
    require(std::holds_alternative<IPv4Error>(result), "expected parse failure");
    require(std::get<IPv4Error>(result) == error, "expected precise error reason");
}
void set_checksum(std::vector<std::uint8_t>& packet) {
    packet[10] = packet[11] = 0;
    const auto value = internet_checksum(std::span(packet).first((packet[0] & 15) * 4));
    packet[10] = static_cast<std::uint8_t>(value >> 8);
    packet[11] = static_cast<std::uint8_t>(value);
}
}

int main() {
    using namespace netforge;
    // Fixed IPv4 header, independently precomputed checksum 0xb861.
    const std::vector<std::uint8_t> known{
        0x45,0x00,0x00,0x73,0x00,0x00,0x40,0x00,0x40,0x11,
        0xb8,0x61,0xc0,0xa8,0x00,0x01,0xc0,0xa8,0x00,0xc7};
    require(internet_checksum(known) == 0, "fixed valid header checksum");
    auto zeroed = known; zeroed[10] = zeroed[11] = 0;
    require(internet_checksum(zeroed) == 0xb861, "fixed checksum generation");
    const std::array<std::uint8_t, 3> odd{0x01,0x02,0x03};
    require(internet_checksum(odd) == 0xfbfd, "odd byte zero padding");
    require(internet_checksum({}) == 0xffff, "empty checksum");
    const std::array<std::uint8_t, 4> carry{0xff,0xff,0x00,0x01};
    require(internet_checksum(carry) == 0xfffe, "end-around carry");
    std::vector<std::uint8_t> large(131076, 0xff);
    require(internet_checksum(large) == 0, "accumulator must not overflow on long input");

    auto packet = known; packet.resize(115, 0);
    for (std::size_t bit = 0; bit < 160; ++bit) {
        auto corrupted = packet;
        corrupted[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
        require(std::holds_alternative<IPv4Error>(parse_ipv4(corrupted)),
                "every single-bit header corruption must be rejected");
    }
    for (std::size_t n = 0; n < 20; ++n)
        expect_error(std::span(packet).first(n), IPv4Error::truncated_header);
    expect_error(std::span(packet).first(20), IPv4Error::truncated_datagram);
    auto result = parse_ipv4(packet);
    require(std::holds_alternative<IPv4Header>(result), "valid complete datagram");
    auto header = std::get<IPv4Header>(result);
    require(header.header_length == 20 && header.total_length == 115, "length fields");
    require(header.source == IPv4Address{192,168,0,1} &&
            header.destination == IPv4Address{192,168,0,199}, "address fields");
    require(header.dont_fragment && !header.more_fragments &&
            header.fragment_offset == 0 && header.ttl == 64 && header.protocol == 17,
            "flags TTL protocol");
    packet[12] = 1;
    require(header.source[0] == 192, "owned header fields");
    expect_error(packet, IPv4Error::bad_checksum);
    packet = known; packet.resize(115, 0); packet[8] ^= 1;
    expect_error(packet, IPv4Error::bad_checksum);
    packet = known; packet.resize(120, 0); packet[119] = 0xff;
    require(std::holds_alternative<IPv4Header>(parse_ipv4(packet)), "ignore trailing padding");
    packet[20] = 0xff;
    require(std::holds_alternative<IPv4Header>(parse_ipv4(packet)), "IP checksum excludes payload");
    packet[0] = 0x65; expect_error(packet, IPv4Error::wrong_version);
    packet[0] = 0x44; expect_error(packet, IPv4Error::invalid_header_length);
    packet[0] = 0x45; packet[2] = 0; packet[3] = 19;
    expect_error(packet, IPv4Error::invalid_total_length);

    // Maximum IHL: accept opaque options, but checksum every option byte.
    packet.assign(60, 0); packet[0] = 0x4f; packet[3] = 60;
    packet[8] = 0; packet[9] = 1; packet[20] = 1; set_checksum(packet);
    for (std::size_t n = 20; n < 60; ++n)
        expect_error(std::span(packet).first(n), IPv4Error::truncated_header);
    header = std::get<IPv4Header>(parse_ipv4(packet));
    require(header.header_length == 60 && header.options[0] == 1 && header.ttl == 0,
            "opaque options and TTL zero are parseable, not forwarding permission");
    packet[20] ^= 1; expect_error(packet, IPv4Error::bad_checksum);
    packet[20] ^= 1; packet[6] = 0x20; packet[7] = 3; set_checksum(packet);
    header = std::get<IPv4Header>(parse_ipv4(packet));
    require(header.more_fragments && !header.dont_fragment && header.fragment_offset == 3,
            "fragment offset stays in eight-byte units");
    packet[6] = 0x80; set_checksum(packet);
    expect_error(packet, IPv4Error::reserved_flag);
    packet.assign(65535, 0); packet[0] = 0x45;
    packet[2] = 0xff; packet[3] = 0xff; set_checksum(packet);
    require(std::get<IPv4Header>(parse_ipv4(packet)).total_length == 65535,
            "maximum IPv4 datagram length");
    std::cout << "PASS: IPv4 bounds, fields, checksum vectors, options, fragments and padding\n";
}
