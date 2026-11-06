#include "netforge/ethernet.hpp"

#include <algorithm>

namespace netforge {
EthernetResult parse_ethernet(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 14) {
        return ParseError::truncated_ethernet_header;
    }

    EthernetHeader header{};
    std::copy_n(bytes.begin(), 6, header.destination.begin());
    std::copy_n(bytes.begin() + 6, 6, header.source.begin());
    header.type_or_length = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[12]) << 8) | bytes[13]);
    return header;
}
} // namespace netforge
