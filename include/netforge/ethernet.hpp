#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <variant>

namespace netforge {
using MacAddress = std::array<std::uint8_t, 6>;

struct EthernetHeader {
    MacAddress destination;
    MacAddress source;
    std::uint16_t type_or_length;
};

enum class ParseError { truncated_ethernet_header };
using EthernetResult = std::variant<EthernetHeader, ParseError>;

// Reads the first 14 bytes of a frame beginning at the destination MAC.
// The caller owns the input. The returned header owns copies of its fields.
// Does not validate payload, FCS, minimum frame size, or VLAN encapsulation.
[[nodiscard]] EthernetResult parse_ethernet(std::span<const std::uint8_t> bytes);
} // namespace netforge
