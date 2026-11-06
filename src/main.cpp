#include "netforge/appliance.hpp"
#include "netforge/workers.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
std::vector<std::uint8_t> decode(const std::string& line) {
    std::string compact;
    for (unsigned char byte : line) {
        if (byte == ' ' || byte == '\t' || byte == '\r') continue;
        compact.push_back(static_cast<char>(byte));
    }
    if (compact.size() % 2 || compact.size() > 131098) throw std::runtime_error("invalid frame hex length");
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < compact.size(); i += 2) {
        unsigned value = 0;
        auto [end, error] = std::from_chars(compact.data() + i, compact.data() + i + 2, value, 16);
        if (error != std::errc{} || end != compact.data() + i + 2) throw std::runtime_error("invalid hex byte");
        result.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}
std::string hex(std::span<const std::uint8_t> bytes) {
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (auto byte : bytes) out << std::setw(2) << unsigned(byte);
    return out.str();
}
std::string json(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
        else out << c;
    }
    out << '"'; return out.str();
}
bool line_bounded(std::istream& input, std::string& line) {
    line.clear(); char c;
    while (input.get(c)) {
        if (c == '\n') return true;
        if (line.size() == 140000) throw std::runtime_error("frame line exceeds limit");
        line.push_back(c);
    }
    if (input.bad()) throw std::runtime_error("frame read failed");
    return !line.empty();
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "NetForge 0.1.0\n"; return 0;
        }
        if (argc < 3) {
            std::cerr << "NetForge 0.1.0\nUsage: netforge check CONFIG\n"
                         "       netforge check-workers CONFIG 1|2\n"
                         "       netforge replay CONFIG INGRESS HEX_FILE\n"
                         "       netforge bench CONFIG INGRESS HEX_FILE ITERATIONS\n"
                         "       netforge bench-mix CONFIG INGRESS HEX_FILE ITERATIONS warm|cold\n"
                         "       netforge run CONFIG  (Linux, CAP_NET_RAW)\n"
                         "       netforge run-workers CONFIG 1|2  (Linux, UDP/TCP/SNAT subset)\n";
            return 2;
        }
        const std::string command = argv[1];
        if (command == "run" && argc == 3) return netforge::run_linux(argv[2]);
        if (command == "run-workers" && argc == 4) {
            const std::string count = argv[3];
            if (count != "1" && count != "2") throw std::runtime_error("workers must be 1 or 2");
            return netforge::run_linux(argv[2], count == "1" ? 1U : 2U);
        }
        if (command == "check-workers" && argc == 4) {
            const std::string count = argv[3];
            if (count != "1" && count != "2") throw std::runtime_error("workers must be 1 or 2");
            auto config = netforge::load_config(argv[2]);
            netforge::WorkerPool pool(config, count == "1" ? 1U : 2U);
            pool.shutdown();
            std::cout << "{\"valid\":true,\"generation\":" << config.generation << ",\"workers\":" << count << "}\n";
            return 0;
        }
        netforge::Appliance appliance(netforge::load_config(argv[2]));
        if (command == "check" && argc == 3) {
            std::cout << "{\"valid\":true,\"generation\":" << appliance.config().generation << "}\n"; return 0;
        }
        if ((command != "replay" || argc != 5) && (command != "bench" || argc != 6) &&
            (command != "bench-mix" || argc != 7))
            throw std::runtime_error("invalid command or arguments");
        if (!netforge::find_port(appliance.config(), argv[3])) throw std::runtime_error("unknown ingress");
        std::ifstream file(argv[4]); if (!file) throw std::runtime_error("cannot open frame file");
        std::string line;
        if (command == "bench-mix") {
            if (appliance.config().neighbor_discovery) throw std::runtime_error("mixed benchmark requires static neighbors");
            const std::string mode = argv[6];
            if (mode != "warm" && mode != "cold") throw std::runtime_error("benchmark mode must be warm or cold");
            unsigned iterations = 0; const std::string n = argv[5];
            auto [end, error] = std::from_chars(n.data(), n.data() + n.size(), iterations);
            if (error != std::errc{} || end != n.data() + n.size() || iterations == 0 || iterations > 1000000)
                throw std::runtime_error("iterations must be 1..1000000");
            std::vector<std::vector<std::uint8_t>> corpus;
            std::size_t bytes = 0, input_bytes = 0;
            while (line_bounded(file, line)) {
                input_bytes += line.size() + 1;
                if (input_bytes > 4194304) throw std::runtime_error("benchmark text exceeds 4 MiB");
                if (line.empty() || line[0] == '#') continue;
                auto packet = decode(line); bytes += packet.size();
                if (corpus.size() >= 4096 || bytes > 1048576) throw std::runtime_error("benchmark corpus exceeds bounds");
                corpus.push_back(std::move(packet));
            }
            if (corpus.empty() || (mode == "cold" && iterations > corpus.size()))
                throw std::runtime_error("cold mode requires at least one corpus frame per iteration");
            const auto warmup = mode == "warm" ? corpus.size() : 0;
            for (std::size_t i = 0; i < warmup; ++i) appliance.process(argv[3], corpus[i], 0);
            const auto before = appliance.counters();
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) appliance.process(argv[3], corpus[i % corpus.size()], 0);
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            const auto& after = appliance.counters();
            auto reason_delta = [&](const std::string& reason) {
                auto value = [&](const auto& counts) { auto it = counts.reasons.find(reason); return it == counts.reasons.end() ? 0ULL : it->second; };
                return value(after) - value(before);
            };
            std::cout << "{\"kind\":\"offline_mixed_microbenchmark\",\"mode\":" << json(mode)
                      << ",\"iterations\":" << iterations << ",\"corpus_frames\":" << corpus.size()
                      << ",\"warmup\":" << warmup << ",\"elapsed_ns\":" << ns
                      << ",\"emitted\":" << after.emitted - before.emitted
                      << ",\"dropped\":" << after.dropped - before.dropped
                      << ",\"input_bytes\":" << after.received_bytes - before.received_bytes
                      << ",\"new_udp_mappings\":" << reason_delta("new_udp_mapping")
                      << ",\"tracked_udp\":" << reason_delta("tracked_udp")
                      << ",\"new_tcp_mappings\":" << reason_delta("new_tcp_mapping")
                      << ",\"flow_count\":" << appliance.flow_count() << "}\n";
        } else if (command == "bench") {
            if (!line_bounded(file, line)) throw std::runtime_error("benchmark needs one frame");
            auto packet = decode(line);
            unsigned iterations = 0; const std::string n = argv[5];
            auto [end, error] = std::from_chars(n.data(), n.data() + n.size(), iterations);
            if (error != std::errc{} || end != n.data() + n.size() || iterations == 0 || iterations > 1000000)
                throw std::runtime_error("iterations must be 1..1000000");
            const auto warmup = std::min(iterations, 1000U);
            for (unsigned i = 0; i < warmup; ++i) appliance.process(argv[3], packet, 0);
            const auto before = appliance.counters();
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) appliance.process(argv[3], packet, 0);
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
            std::cout << "{\"kind\":\"offline_microbenchmark\",\"iterations\":" << iterations
                      << ",\"warmup\":" << warmup << ",\"elapsed_ns\":" << ns
                      << ",\"emitted\":" << appliance.counters().emitted - before.emitted
                      << ",\"dropped\":" << appliance.counters().dropped - before.dropped << "}\n";
        } else {
            std::uint64_t sequence = 0;
            while (line_bounded(file, line)) {
                if (line.empty() || line[0] == '#') continue;
                const auto decision = appliance.process(argv[3], decode(line), sequence);
                std::cout << "{\"sequence\":" << sequence++ << ",\"reason\":" << json(decision.reason)
                          << ",\"egress\":" << json(decision.egress) << ",\"frame\":" << json(hex(decision.frame)) << "}\n";
            }
        }
        std::cerr << appliance.metrics(); return 0;
    } catch (const std::exception& error) {
        std::cerr << "netforge: " << error.what() << '\n'; return 1;
    }
}
