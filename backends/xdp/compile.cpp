// Offline compiler for the explicitly supported stateless XDP subset.
#include "netforge/appliance.hpp"
#include "compile_policy.hpp"
#include "wire_policy.hpp"
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <net/if.h>
#include <stdexcept>
using namespace netforge;
static_assert(sizeof(nf_policy) == 1044, "XDP policy ABI changed");
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--decode-policy") {
            std::ifstream input(argv[2], std::ios::binary);
            std::vector<__u8> bytes(1035); input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            if (input.bad() || !input.eof()) throw std::runtime_error("wire input unreadable or oversized");
            bytes.resize(static_cast<std::size_t>(input.gcount()));
            const auto policy = netforge::xdp::decode_policy(bytes);
            std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(&policy), sizeof(policy)); output.close();
            if (!output) throw std::runtime_error("decoded policy write failed");
            std::cout << "{\"valid\":true,\"wire_version\":1,\"policy_bytes\":" << sizeof(policy) << "}\n"; return 0;
        }
        if (argc < 5) throw std::runtime_error("Usage: nf-xdp-compile CONFIG INGRESS OUTPUT PORT=IFINDEX|@DEVICE ...");
        Config config = load_config(argv[1]);
        std::map<std::string, unsigned> bindings;
        bool portable = false;
        for (int i = 4; i < argc; ++i) {
            std::string text = argv[i];
            if (text == "--portable" && !portable) { portable = true; continue; }
            auto equal = text.find('=');
            if (equal == std::string::npos) throw std::runtime_error("binding requires PORT=IFINDEX");
            auto name = text.substr(0, equal); auto value = text.substr(equal + 1); unsigned index = 0;
            const Port* port = find_port(config, name);
            if (!value.empty() && value[0] == '@') {
                if (!port || value.substr(1) != port->device)
                    throw std::runtime_error("device binding must match configured port device");
                index = if_nametoindex(value.c_str() + 1);
            } else {
                auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), index);
                if (error != std::errc{} || end != value.data() + value.size())
                    throw std::runtime_error("invalid numeric interface binding");
            }
            if (!port ||
                !index || index > 0x7fffffffU || !bindings.emplace(name, index).second)
                throw std::runtime_error("invalid or duplicate interface binding");
            for (const auto& [other, number] : bindings)
                if (other != name && number == index) throw std::runtime_error("interface indices must be unique");
        }
        if (bindings.size() != config.ports.size()) throw std::runtime_error("every port requires an explicit interface binding");
        nf_policy policy = netforge::xdp::compile_policy(config, argv[2], bindings);
        // Raw policy ABI is target-native, not a portable wire format. Output is
        // created only after validation; consumers must require the exact size.
        std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
        if (portable) {
            const auto bytes = netforge::xdp::encode_policy(policy);
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else output.write(reinterpret_cast<const char*>(&policy), sizeof(policy));
        output.close();
        if (!output) throw std::runtime_error("policy output write failed");
        std::cout << "{\"valid\":true,\"generation\":" << config.generation
                  << ",\"policy_bytes\":" << (portable ? netforge::xdp::encode_policy(policy).size() : sizeof(policy))
                  << ",\"wire_version\":" << (portable ? 1 : 0) << "}\n";
    } catch (const std::exception& error) { std::cerr << "XDP compile rejected: " << error.what() << '\n'; return 1; }
}
