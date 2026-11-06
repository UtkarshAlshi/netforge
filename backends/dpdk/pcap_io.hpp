#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <csignal>
#include <cstdio>
#include <memory>
#include <vector>
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdint>
#include <unistd.h>

namespace nf_capture {
inline void ensure(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
constexpr unsigned max_frame = 1514;
constexpr std::uint64_t max_bytes = 16 * 1024 * 1024;
constexpr unsigned max_packets = 65536;
inline volatile std::sig_atomic_t stopped = 0;
inline void stop(int) { stopped = 1; }
inline unsigned little(const unsigned char* p) {
    return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
}
inline void put(unsigned char* p, unsigned n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(n >> (8 * i));
}
struct Packet { unsigned seconds = 0, micros = 0; std::vector<unsigned char> data; };
class Reader {
    std::ifstream file;
    unsigned snap = 0, count = 0;
    std::uint64_t bytes = 24;
public:
    explicit Reader(const char* path) {
        ensure(std::filesystem::is_regular_file(path), "PCAP input must be regular file");
        auto size = std::filesystem::file_size(path);
        ensure(size >= 24 && size <= max_bytes, "PCAP input size bound");
        file.open(path, std::ios::binary);
        unsigned char header[24]{}; file.read(reinterpret_cast<char*>(header), sizeof(header));
        ensure(file.gcount() == 24 && little(header) == 0xa1b2c3d4 &&
                header[4] == 2 && !header[5] && header[6] == 4 && !header[7] &&
                !little(header + 8) && !little(header + 12) && little(header + 20) == 1,
                "PCAP requires little-endian microsecond Ethernet v2.4");
        snap = little(header + 16); ensure(snap > 0 && snap <= 65535, "PCAP snaplen bound");
    }
    bool next(Packet& packet) {
        unsigned char header[16]{}; file.read(reinterpret_cast<char*>(header), sizeof(header));
        if (!file.gcount() && file.eof() && !file.bad()) return false;
        ensure(file.gcount() == 16, "truncated PCAP record header");
        unsigned length = little(header + 8);
        ensure(++count <= max_packets && length && length <= max_frame && length <= snap &&
                length == little(header + 12) && little(header + 4) < 1000000,
                "PCAP record count/length/timestamp bound");
        bytes += 16 + length; ensure(bytes <= max_bytes, "PCAP input exceeds16 MiB");
        packet.seconds = little(header); packet.micros = little(header + 4); packet.data.resize(length);
        file.read(reinterpret_cast<char*>(packet.data.data()), length);
        ensure(file.gcount() == static_cast<std::streamsize>(length), "truncated PCAP packet");
        return true;
    }
};
class Writer {
    FILE* file = nullptr;
public:
    explicit Writer(const char* path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        ensure(fd >= 0, "output must be a new private PCAP file");
        file = fdopen(fd, "wb"); if (!file) { close(fd); throw std::runtime_error("PCAP fdopen"); }
        unsigned char header[24]{}; put(header, 0xa1b2c3d4); header[4] = 2; header[6] = 4;
        put(header + 16, max_frame); put(header + 20, 1);
        if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
            fclose(file); file = nullptr; throw std::runtime_error("PCAP header write");
        }
    }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() { if (file) fclose(file); }
    void write(const Packet& metadata, const unsigned char* data, unsigned length) {
        unsigned char header[16]{}; put(header, metadata.seconds); put(header + 4, metadata.micros);
        put(header + 8, length); put(header + 12, length);
        ensure(fwrite(header, 1, sizeof(header), file) == sizeof(header) && fwrite(data, 1, length, file) == length,
                "PCAP record write");
    }
    void finish() { ensure(fflush(file) == 0 && fsync(fileno(file)) == 0, "PCAP output flush"); }
};
} // namespace nf_capture
