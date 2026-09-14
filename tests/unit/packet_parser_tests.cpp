#include "common/packet_parser.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace {
using netforge::ParseError;

std::array<std::byte, 34> valid_frame() {
  return {std::byte{0x00},std::byte{0x11},std::byte{0x22},std::byte{0x33},std::byte{0x44},std::byte{0x55}, std::byte{0x66},std::byte{0x77},std::byte{0x88},std::byte{0x99},std::byte{0xaa},std::byte{0xbb}, std::byte{0x08},std::byte{0x00}, std::byte{0x45},std::byte{0x00},std::byte{0x00},std::byte{0x14},std::byte{0x00},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x40},std::byte{0x01},std::byte{0x00},std::byte{0x00},std::byte{0x0a},std::byte{0x00},std::byte{0x01},std::byte{0x02},std::byte{0x0a},std::byte{0x00},std::byte{0x02},std::byte{0x02}};
}

void require(bool condition, const char* message) { if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); } }
}

int main() {
  const auto valid = valid_frame();
  const auto parsed = netforge::parse_ethernet_ipv4(valid);
  require(parsed.ok, "valid IPv4 frame parses");
  require(parsed.packet.ethertype == 0x0800, "EtherType decoded as big-endian");
  require(parsed.packet.ipv4.ttl == 64, "TTL decoded");
  require(parsed.packet.ipv4.source == std::array<std::uint8_t,4>{10,0,1,2}, "source address decoded");
  require(parsed.packet.ipv4.destination == std::array<std::uint8_t,4>{10,0,2,2}, "destination address decoded");
  for (std::size_t size = 0; size < valid.size(); ++size) {
    const auto result = netforge::parse_ethernet_ipv4(std::span<const std::byte>(valid.data(), size));
    require(!result.ok, "all truncated frame prefixes fail safely");
  }
  auto arp = valid; arp[12] = std::byte{0x08}; arp[13] = std::byte{0x06};
  require(netforge::parse_ethernet_ipv4(arp).error == ParseError::unsupported_ethertype, "ARP is not silently treated as IPv4");
  auto wrong_version = valid; wrong_version[14] = std::byte{0x65};
  require(netforge::parse_ethernet_ipv4(wrong_version).error == ParseError::invalid_ipv4_version, "wrong version rejected");
  auto invalid_ihl = valid; invalid_ihl[14] = std::byte{0x44};
  require(netforge::parse_ethernet_ipv4(invalid_ihl).error == ParseError::invalid_ipv4_ihl, "short IHL rejected");
  auto oversized_total = valid; oversized_total[16] = std::byte{0x01};
  require(netforge::parse_ethernet_ipv4(oversized_total).error == ParseError::invalid_ipv4_total_length, "incomplete declared packet rejected");
  std::cout << "packet parser tests passed\n";
}
