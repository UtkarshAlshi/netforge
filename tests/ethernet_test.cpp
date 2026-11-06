#include "netforge/ethernet.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main() {
    using namespace netforge;
    // Broadcast destination, synthetic source, IPv4 EtherType; no IP payload.
    std::array<std::uint8_t, 15> frame{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x08, 0x00, 0xab};

    for (std::size_t length = 0; length < 14; ++length) {
        auto result = parse_ethernet(std::span(frame).first(length));
        require(std::holds_alternative<ParseError>(result),
                "every truncated header must be rejected");
    }

    auto result = parse_ethernet(std::span(frame).first(14));
    require(std::holds_alternative<EthernetHeader>(result),
            "exactly 14 bytes must suffice for header parsing");
    auto header = std::get<EthernetHeader>(result);
    require(header.destination == MacAddress{0xff,0xff,0xff,0xff,0xff,0xff},
            "destination MAC must preserve wire order");
    require(header.source == MacAddress{0x02,0x11,0x22,0x33,0x44,0x55},
            "source MAC must preserve wire order");
    require(header.type_or_length == 0x0800, "EtherType must decode big endian");
    require(std::holds_alternative<EthernetHeader>(parse_ethernet(frame)),
            "payload bytes must not prevent header parsing");

    frame[6] = 0x99;
    require(header.source[0] == 0x02, "result must not borrow source storage");
    frame[12] = 0x08; frame[13] = 0x06;
    require(std::get<EthernetHeader>(parse_ethernet(frame)).type_or_length == 0x0806,
            "ARP EtherType must decode correctly");
    frame[12] = 0; frame[13] = 42;
    require(std::get<EthernetHeader>(parse_ethernet(frame)).type_or_length == 42,
            "length-field values must not be mislabeled as IPv4");
    std::cout << "PASS: Ethernet truncation, MAC fields, byte order, ownership, and length field\n";
}
