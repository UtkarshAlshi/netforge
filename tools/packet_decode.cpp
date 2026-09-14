#include "common/packet_parser.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: packet_decode <raw-ethernet-frame>\n"; return 2; }
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) { std::cerr << "cannot open input\n"; return 2; }
  std::vector<std::byte> bytes;
  for (char character : std::vector<char>(std::istreambuf_iterator<char>(input), {})) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  const auto result = netforge::parse_ethernet_ipv4(bytes);
  if (!result.ok) { std::cerr << "parse error: " << netforge::parse_error_name(result.error) << '\n'; return 1; }
  std::cout << "ethernet " << netforge::format_mac(result.packet.source_mac) << " -> " << netforge::format_mac(result.packet.destination_mac) << '\n';
  std::cout << "ipv4 " << netforge::format_ipv4(result.packet.ipv4.source) << " -> " << netforge::format_ipv4(result.packet.ipv4.destination) << " ttl=" << static_cast<unsigned>(result.packet.ipv4.ttl) << " protocol=" << static_cast<unsigned>(result.packet.ipv4.protocol) << '\n';
}
