#pragma once
#include "pcap_io.hpp"
namespace nf_capture {
struct Owned {
    std::array<rte_mbuf*, 4> packets{};
    ~Owned() { for (auto* packet : packets) if (packet) rte_pktmbuf_free(packet); }
    void release(rte_mbuf* packet) { for (auto& item : packets) if (item == packet) item = nullptr; }
};
inline bool process(Resources& resources, const nf_policy& policy, Reader& reader,
                    const char* output, const char* punt_output) {
    Writer routed(output), punts(punt_output);
    std::uint64_t received_count = 0, routed_count = 0, dropped = 0, punted = 0, sent_count = 0, unsent = 0;
    stopped = 0; auto old_term = std::signal(SIGTERM, stop), old_int = std::signal(SIGINT, stop);
    struct Restore { decltype(old_term) term, interrupt; ~Restore() { std::signal(SIGTERM, term); std::signal(SIGINT, interrupt); } } restore{old_term, old_int};
    std::cout << "{\"event\":\"pcap_started\"}\n" << std::flush;
    while (!stopped) {
        std::array<Packet, 4> records; unsigned count = 0;
        while (count < records.size() && reader.next(records[count])) ++count;
        if (!count) break;
        for (unsigned i = 0; i < count; ++i) {
            auto* packet = rte_pktmbuf_alloc(resources.pool); ensure(packet, "PCAP mbuf allocation");
            void* data = rte_pktmbuf_append(packet, static_cast<uint16_t>(records[i].data.size()));
            if (!data) { rte_pktmbuf_free(packet); throw std::runtime_error("PCAP mbuf append"); }
            std::memcpy(data, records[i].data.data(), records[i].data.size());
            if (rte_ring_enqueue(resources.input, packet)) { rte_pktmbuf_free(packet); throw std::runtime_error("PCAP input ring full"); }
        }
        Owned owned;
        unsigned got = rte_eth_rx_burst(static_cast<uint16_t>(resources.port), 0, owned.packets.data(), static_cast<uint16_t>(count));
        ensure(got == count, "PCAP ring-PMD RX count");
        std::array<rte_mbuf*, 4> transmit{}; std::array<unsigned, 4> metadata{}; unsigned candidates = 0;
        for (unsigned i = 0; i < got; ++i) {
            auto* packet = owned.packets[i]; ensure(packet->nb_segs == 1, "PCAP multisegment unsupported");
            auto* data = rte_pktmbuf_mtod(packet, unsigned char*); unsigned target = 0; ++received_count;
            int action = nf_process(data, data + rte_pktmbuf_data_len(packet), &policy, 1, &target);
            if (action == NF_ROUTE) {
                ensure(target == 2, "PCAP egress binding unsupported");
                transmit[candidates] = packet; metadata[candidates++] = i; ++routed_count;
            } else {
                if (action == NF_DROP) ++dropped;
                else { ++punted; punts.write(records[i], records[i].data.data(), static_cast<unsigned>(records[i].data.size())); }
                owned.release(packet); rte_pktmbuf_free(packet);
            }
        }
        unsigned sent = rte_eth_tx_burst(static_cast<uint16_t>(resources.port), 0, transmit.data(), static_cast<uint16_t>(candidates));
        sent_count += sent; unsent += candidates - sent;
        for (unsigned i = 0; i < sent; ++i) owned.release(transmit[i]);
        // Unsent mbufs remain owned by the RAII guard. Sent packets belong to output ring.
        for (unsigned i = 0; i < sent; ++i) {
            void* object = nullptr; ensure(rte_ring_dequeue(resources.output, &object) == 0, "PCAP TX dequeue");
            auto* packet = static_cast<rte_mbuf*>(object);
            // Adopt dequeued output before potentially throwing during file I/O.
            std::unique_ptr<rte_mbuf, decltype(&rte_pktmbuf_free)> outgoing(packet, &rte_pktmbuf_free);
            routed.write(records[metadata[i]], rte_pktmbuf_mtod(packet, unsigned char*), rte_pktmbuf_data_len(packet));
        }
        // Drop unsent packets now so the invariant can be checked after this batch.
        for (auto& packet : owned.packets) if (packet) { rte_pktmbuf_free(packet); packet = nullptr; }
        ensure(rte_ring_empty(resources.input) && rte_ring_empty(resources.output) && rte_mempool_avail_count(resources.pool) == 255,
                "PCAP batch must reclaim all255 mbufs");
    }
    routed.finish(); punts.finish();
    ensure(received_count == routed_count + dropped + punted && routed_count == sent_count + unsent, "PCAP accounting");
    std::cout << "{\"mode\":\"pcap\",\"complete\":" << (stopped ? "false" : "true")
              << ",\"received\":" << received_count << ",\"routed\":" << routed_count
              << ",\"dropped\":" << dropped << ",\"punted\":" << punted
              << ",\"sent\":" << sent_count << ",\"unsent\":" << unsent << "}\n" << std::flush;
    return !stopped;
}
} // namespace nf_capture
