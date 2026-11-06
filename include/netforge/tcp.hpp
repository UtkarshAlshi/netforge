#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace netforge {
enum class TcpPhase { syn_sent, syn_ack, established, closing, reset };
struct TcpState {
    TcpPhase phase = TcpPhase::syn_sent;
    std::uint32_t client_isn = 0, server_isn = 0;
    bool client_fin = false, server_fin = false;
};
struct TcpSegment {
    std::uint32_t sequence, acknowledgement;
    std::uint8_t flags;
    std::size_t payload_length;
};
// Observed transit state, not a TCP endpoint. Checks handshake sequence/ACK values,
// retransmitted handshake packets and flags. Does not validate data windows,
// reorder/reassemble streams, authenticate RSTs or interpret TCP options.
std::optional<TcpState> begin_tcp(const TcpSegment& segment);
std::optional<TcpState> observe_tcp(const TcpState& state, const TcpSegment& segment, bool reply);
} // namespace netforge
