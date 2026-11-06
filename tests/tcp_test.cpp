#include "netforge/tcp.hpp"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* text) {
    if (!value) { std::cerr << "FAIL: " << text << '\n'; std::exit(1); }
}
}
int main() {
    using namespace netforge;
    require(!begin_tcp({100, 0, 0x10, 0}), "ACK cannot create state");
    require(!begin_tcp({100, 0, 0x03, 0}), "SYN+FIN rejected");
    require(!begin_tcp({100, 0, 0x02, 1}), "SYN data unsupported");
    auto initial = begin_tcp({100, 0, 0xc2, 0});
    require(initial.has_value(), "ECN SYN accepted");
    auto state = *initial;
    require(observe_tcp(state, {100,0,2,0}, false).has_value(), "SYN retransmission");
    require(!observe_tcp(state, {101,0,2,0}, false), "different client ISN rejected");
    require(!observe_tcp(state, {900,100,0x12,0}, true), "bad SYNACK acknowledgement");
    require(!observe_tcp(state, {101,901,0x10,0}, false), "ACK before SYNACK rejected");
    state = *observe_tcp(state, {900,101,0x52,0}, true);
    require(state.phase == TcpPhase::syn_ack, "SYNACK observed");
    require(!observe_tcp(state, {101,902,0x10,0}, false), "wrong server ACK rejected");
    require(!observe_tcp(state, {102,901,0x10,0}, false), "wrong client sequence rejected");
    state = *observe_tcp(state, {101,901,0x10,0}, false);
    require(state.phase == TcpPhase::established, "handshake established");
    require(observe_tcp(state, {900,101,0x12,0}, true).has_value(), "downstream-lost ACK permits SYNACK retransmit");
    require(!observe_tcp(state, {901,101,0x12,0}, true), "forged new SYNACK sequence rejected");
    require(observe_tcp(state, {101,901,0x18,3}, false).has_value(), "data ACK accepted");
    require(!observe_tcp(state, {101,901,0,3}, false), "data missing ACK rejected");
    state = *observe_tcp(state, {104,901,0x11,0}, false);
    require(state.phase == TcpPhase::closing && state.client_fin, "client FIN observed");
    state = *observe_tcp(state, {901,105,0x11,0}, true);
    require(state.server_fin && state.client_fin, "both FIN directions recorded");
    require(observe_tcp(state, {105,902,0x10,0}, false).has_value(), "final ACK forwarded");
    state = *observe_tcp(state, {902,105,0x14,0}, true);
    require(state.phase == TcpPhase::reset, "RST creates bounded quarantine");
    require(!observe_tcp(state, {100,0,2,0}, false), "reset mapping cannot reopen immediately");
    auto wrapped = *begin_tcp({0xffffffffU,0,2,0});
    wrapped = *observe_tcp(wrapped, {0xffffffffU,0,0x12,0}, true);
    wrapped = *observe_tcp(wrapped, {0,0,0x10,0}, false);
    require(wrapped.phase == TcpPhase::established, "handshake sequence wraparound");
    std::cout << "PASS: TCP handshake, retransmissions, ACK validation, FIN, RST and sequence wrap\n";
}
