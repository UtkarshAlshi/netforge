#pragma once
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
namespace netforge {
struct PortCounters {
    std::uint64_t rx_packets = 0, rx_bytes = 0, tx_packets = 0, tx_bytes = 0, tx_failures = 0;
};
inline std::string metric_label(const std::string& value) {
    std::string escaped;
    for (const char c : value) {
        if (c == '\\' || c == '"') { escaped += '\\'; escaped += c; }
        else if (c == '\n') escaped += "\\n";
        else escaped += c;
    }
    return escaped;
}
inline std::string port_metrics(const std::vector<std::pair<std::string, PortCounters>>& ports) {
    std::ostringstream out;
    for (const auto& [id, counts] : ports) {
        const auto label = "{port=\"" + metric_label(id) + "\"} ";
        out << "netforge_port_rx_packets_total" << label << counts.rx_packets << '\n'
            << "netforge_port_rx_bytes_total" << label << counts.rx_bytes << '\n'
            << "netforge_port_tx_packets_total" << label << counts.tx_packets << '\n'
            << "netforge_port_tx_bytes_total" << label << counts.tx_bytes << '\n'
            << "netforge_port_tx_failures_total" << label << counts.tx_failures << '\n';
    }
    return out.str();
}
}
