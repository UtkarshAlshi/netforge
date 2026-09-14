#include "common/packet_parser.hpp"

#include <iomanip>
#include <sstream>

namespace netforge {
namespace {
constexpr std::size_t kEthernetHeaderLength = 14;
constexpr std::size_t kIpv4MinimumHeaderLength = 20;
constexpr std::uint16_t kEtherTypeIpv4 = 0x0800;

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  return std::to_integer<std::uint8_t>(bytes[offset]);
}

std::uint16_t be16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(byte_at(bytes, offset)) << 8U) | byte_at(bytes, offset + 1));
}

ParseResult failure(ParseError error) noexcept { return {.ok = false, .error = error}; }
}  // namespace

ParseResult parse_ethernet_ipv4(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < kEthernetHeaderLength) return failure(ParseError::truncated_ethernet);
  PacketMetadata packet{};
  for (std::size_t index = 0; index < 6; ++index) {
    packet.destination_mac[index] = byte_at(bytes, index);
    packet.source_mac[index] = byte_at(bytes, index + 6);
  }
  packet.ethertype = be16(bytes, 12);
  if (packet.ethertype != kEtherTypeIpv4) return failure(ParseError::unsupported_ethertype);
  if (bytes.size() < kEthernetHeaderLength + kIpv4MinimumHeaderLength) return failure(ParseError::truncated_ipv4);

  const std::size_t ip = kEthernetHeaderLength;
  const std::uint8_t version_ihl = byte_at(bytes, ip);
  if ((version_ihl >> 4U) != 4U) return failure(ParseError::invalid_ipv4_version);
  packet.ipv4.header_length_bytes = static_cast<std::uint8_t>((version_ihl & 0x0FU) * 4U);
  if (packet.ipv4.header_length_bytes < kIpv4MinimumHeaderLength) return failure(ParseError::invalid_ipv4_ihl);
  if (bytes.size() < ip + packet.ipv4.header_length_bytes) return failure(ParseError::truncated_ipv4);
  packet.ipv4.total_length = be16(bytes, ip + 2);
  if (packet.ipv4.total_length < packet.ipv4.header_length_bytes || bytes.size() < ip + packet.ipv4.total_length) return failure(ParseError::invalid_ipv4_total_length);
  packet.ipv4.ttl = byte_at(bytes, ip + 8);
  packet.ipv4.protocol = byte_at(bytes, ip + 9);
  for (std::size_t index = 0; index < 4; ++index) {
    packet.ipv4.source[index] = byte_at(bytes, ip + 12 + index);
    packet.ipv4.destination[index] = byte_at(bytes, ip + 16 + index);
  }
  return {.ok = true, .packet = packet};
}

std::string format_ipv4(const std::array<std::uint8_t, 4>& address) {
  std::ostringstream output;
  output << static_cast<unsigned>(address[0]) << '.' << static_cast<unsigned>(address[1]) << '.' << static_cast<unsigned>(address[2]) << '.' << static_cast<unsigned>(address[3]);
  return output.str();
}

std::string format_mac(const std::array<std::uint8_t, 6>& address) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < address.size(); ++index) {
    if (index != 0) output << ':';
    output << std::setw(2) << static_cast<unsigned>(address[index]);
  }
  return output.str();
}

const char* parse_error_name(ParseError error) noexcept {
  switch (error) {
    case ParseError::truncated_ethernet: return "truncated Ethernet header";
    case ParseError::unsupported_ethertype: return "unsupported EtherType";
    case ParseError::truncated_ipv4: return "truncated IPv4 header";
    case ParseError::invalid_ipv4_version: return "invalid IPv4 version";
    case ParseError::invalid_ipv4_ihl: return "invalid IPv4 IHL";
    case ParseError::invalid_ipv4_total_length: return "invalid IPv4 total length";
  }
  return "unknown parse error";
}
}  // namespace netforge
