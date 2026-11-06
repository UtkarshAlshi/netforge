#include "../backends/dpdk/pcap_io.hpp"
#include <iostream>
int main(int argc, char** argv) {
    try {
        nf_capture::ensure(argc == 2, "requires new test directory");
        std::filesystem::path root(argv[1]);
        nf_capture::ensure(std::filesystem::create_directory(root), "test directory must be new");
        auto path = (root / "valid.pcap").string();
        nf_capture::Packet first{123, 456, std::vector<unsigned char>(64, 7)};
        nf_capture::Packet second{789, 123, std::vector<unsigned char>(1514, 8)};
        {
            nf_capture::Writer writer(path.c_str());
            writer.write(first, first.data.data(), static_cast<unsigned>(first.data.size()));
            writer.write(second, second.data.data(), static_cast<unsigned>(second.data.size())); writer.finish();
        }
        nf_capture::Reader reader(path.c_str()); nf_capture::Packet actual;
        nf_capture::ensure(reader.next(actual) && actual.seconds == first.seconds && actual.micros == first.micros && actual.data == first.data, "first record parity");
        nf_capture::ensure(reader.next(actual) && actual.seconds == second.seconds && actual.micros == second.micros && actual.data == second.data, "second record parity");
        nf_capture::ensure(!reader.next(actual), "clean EOF");
        bool refused = false; try { nf_capture::Writer duplicate(path.c_str()); } catch (...) { refused = true; }
        nf_capture::ensure(refused, "existing output must not be overwritten");
        auto bad = (root / "bad.pcap").string();
        { std::ofstream invalid(bad, std::ios::binary); invalid << std::string(24, '\0'); }
        refused = false; try { nf_capture::Reader invalid(bad.c_str()); } catch (...) { refused = true; }
        nf_capture::ensure(refused, "invalid header must be rejected");
        std::cout << "PASS: bounded PCAP I/O timestamps/bytes/EOF, exclusive outputs and malformed header refusal\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
