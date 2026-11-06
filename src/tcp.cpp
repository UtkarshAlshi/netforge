#include "netforge/tcp.hpp"

namespace netforge {
namespace {
constexpr unsigned fin = 1, syn = 2, rst = 4, ack = 16;
bool initial_syn(const TcpSegment& s) { return (s.flags & 0x3fU) == syn && s.payload_length == 0; }
bool syn_ack(const TcpSegment& s) { return (s.flags & 0x3fU) == (syn | ack) && s.payload_length == 0; }
}
std::optional<TcpState> begin_tcp(const TcpSegment& s) {
    if (!initial_syn(s)) return std::nullopt;
    TcpState state; state.client_isn = s.sequence; return state;
}
std::optional<TcpState> observe_tcp(const TcpState& state, const TcpSegment& s, bool reply) {
    if (state.phase == TcpPhase::reset) return std::nullopt;
    TcpState next = state;
    if (s.flags & rst) {
        if (s.flags & (syn | fin)) return std::nullopt;
        if (state.phase == TcpPhase::syn_sent || state.phase == TcpPhase::syn_ack) {
            if (reply && (!(s.flags & ack) || s.acknowledgement != state.client_isn + 1U)) return std::nullopt;
            if (!reply && s.sequence != state.client_isn && s.sequence != state.client_isn + 1U) return std::nullopt;
        }
        next.phase = TcpPhase::reset; return next;
    }
    // Exact handshake retransmissions remain admissible after the handshake: an
    // ACK may have been lost downstream after this middlebox observed it.
    if (!reply && initial_syn(s) && s.sequence == state.client_isn) return next;
    if (reply && syn_ack(s) && s.acknowledgement == state.client_isn + 1U) {
        if (state.phase == TcpPhase::syn_sent) {
            next.server_isn = s.sequence; next.phase = TcpPhase::syn_ack; return next;
        }
        if (s.sequence == state.server_isn) return next;
    }
    if (s.flags & syn) return std::nullopt;
    if (state.phase == TcpPhase::syn_sent) return std::nullopt;
    if (state.phase == TcpPhase::syn_ack) {
        if (reply || !(s.flags & ack) || s.sequence != state.client_isn + 1U ||
            s.acknowledgement != state.server_isn + 1U) return std::nullopt;
        next.phase = TcpPhase::established;
    } else if (!(s.flags & ack)) return std::nullopt;
    if (s.flags & fin) {
        if (reply) next.server_fin = true; else next.client_fin = true;
        next.phase = TcpPhase::closing;
    }
    return next;
}
} // namespace netforge
