#include "netforge/ipv4.hpp"

#include <algorithm>
#include <cstddef>

namespace netforge {
namespace {
std::uint16_t read16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
}
}

std::uint16_t internet_checksum(std::span<const std::uint8_t> bytes) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        sum += (i % 2 == 0) ? static_cast<std::uint32_t>(bytes[i]) << 8 : bytes[i];
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return static_cast<std::uint16_t>(~sum);
}

std::variant<IPv4Header, IPv4Error> parse_ipv4(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 20) return IPv4Error::truncated_header;
    if ((bytes[0] >> 4) != 4) return IPv4Error::wrong_version;
    const auto length = static_cast<std::uint8_t>((bytes[0] & 0x0fU) * 4);
    if (length < 20) return IPv4Error::invalid_header_length;
    if (bytes.size() < length) return IPv4Error::truncated_header;
    const auto total = read16(bytes, 2);
    if (total < length) return IPv4Error::invalid_total_length;
    if (bytes.size() < total) return IPv4Error::truncated_datagram;
    const auto flags_offset = read16(bytes, 6);
    if ((flags_offset & 0x8000U) != 0) return IPv4Error::reserved_flag;
    if (internet_checksum(bytes.first(length)) != 0) return IPv4Error::bad_checksum;

    IPv4Header header{};
    header.header_length = length;
    header.dscp_ecn = bytes[1];
    header.total_length = total;
    header.identification = read16(bytes, 4);
    header.dont_fragment = (flags_offset & 0x4000U) != 0;
    header.more_fragments = (flags_offset & 0x2000U) != 0;
    header.fragment_offset = flags_offset & 0x1fffU;
    header.ttl = bytes[8];
    header.protocol = bytes[9];
    std::copy_n(bytes.begin() + 12, 4, header.source.begin());
    std::copy_n(bytes.begin() + 16, 4, header.destination.begin());
    std::copy_n(bytes.begin() + 20, length - 20, header.options.begin());
    return header;
}
} // namespace netforge
