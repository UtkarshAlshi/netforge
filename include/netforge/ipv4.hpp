#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <variant>

namespace netforge {
using IPv4Address = std::array<std::uint8_t, 4>;

struct IPv4Header {
    std::uint8_t header_length; // Bytes, including options.
    std::uint8_t dscp_ecn;
    std::uint16_t total_length; // Bytes, excluding trailing link-layer padding.
    std::uint16_t identification;
    bool dont_fragment;
    bool more_fragments;
    std::uint16_t fragment_offset; // Units of eight bytes, not bytes.
    std::uint8_t ttl;
    std::uint8_t protocol;
    IPv4Address source;
    IPv4Address destination;
    std::array<std::uint8_t, 40> options{}; // Opaque bytes; semantics unvalidated.
};

enum class IPv4Error {
    truncated_header,
    wrong_version,
    invalid_header_length,
    invalid_total_length,
    truncated_datagram,
    reserved_flag,
    bad_checksum
};

// Internet checksum in host numeric representation. An odd final byte is the
// high byte of a zero-padded word. A correctly checksummed header returns zero.
[[nodiscard]] std::uint16_t internet_checksum(std::span<const std::uint8_t> bytes);

// Input starts at IPv4's version/IHL byte and must contain the complete datagram.
// Extra bytes are ignored. The result owns its fields. Options and fragments are
// exposed, not interpreted/reassembled. TTL validity for forwarding is separate.
[[nodiscard]] std::variant<IPv4Header, IPv4Error>
parse_ipv4(std::span<const std::uint8_t> bytes);
} // namespace netforge
