#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace netforge {

enum class ParseError { truncated_ethernet, unsupported_ethertype, truncated_ipv4, invalid_ipv4_version, invalid_ipv4_ihl, invalid_ipv4_total_length };

struct Ipv4Metadata {
  std::array<std::uint8_t, 4> source{};
  std::array<std::uint8_t, 4> destination{};
  std::uint8_t ttl{};
  std::uint8_t protocol{};
  std::uint8_t header_length_bytes{};
  std::uint16_t total_length{};
};

struct PacketMetadata {
  std::array<std::uint8_t, 6> destination_mac{};
  std::array<std::uint8_t, 6> source_mac{};
  std::uint16_t ethertype{};
  Ipv4Metadata ipv4{};
};

struct ParseResult {
  bool ok{false};
  PacketMetadata packet{};
  ParseError error{ParseError::truncated_ethernet};
};

[[nodiscard]] ParseResult parse_ethernet_ipv4(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] std::string format_ipv4(const std::array<std::uint8_t, 4>& address);
[[nodiscard]] std::string format_mac(const std::array<std::uint8_t, 6>& address);
[[nodiscard]] const char* parse_error_name(ParseError error) noexcept;

}  // namespace netforge
