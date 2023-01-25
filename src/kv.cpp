#include "kv.hpp"
#include <array>
#include <cerrno>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kv {
namespace {
constexpr std::uint32_t magic = 0x314c564b;
constexpr std::size_t header = 16;
constexpr std::size_t limit = 8 * 1024 * 1024;
constexpr std::uint32_t tombstone = 0xffffffff;

[[noreturn]] void fail(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}
struct File {
    int fd{-1};
    File() = default;
    explicit File(int fd, const char* name) : fd(fd) { if (fd < 0) fail(name); }
    ~File() { if (fd >= 0) close(fd); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    void swap(File& other) noexcept { std::swap(fd, other.fd); }
};
void sync(int fd, bool directory = false) {
    for (;;) {
        if ((directory ? fsync(fd) : fdatasync(fd)) == 0) return;
        if (errno != EINTR) fail("sync");
    }
}
void write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const auto n = write(fd, bytes.data(), bytes.size());
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) fail("write WAL");
        bytes.remove_prefix(static_cast<std::size_t>(n));
    }
}
bool read_all(int fd, std::span<char> bytes, off_t offset) {
    while (!bytes.empty()) {
        const auto n = pread(fd, bytes.data(), bytes.size(), offset);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) fail("read WAL");
        if (n == 0) return false;
        offset += n;
        bytes = bytes.subspan(static_cast<std::size_t>(n));
    }
    return true;
}
std::uint32_t crc(std::string_view head, std::string_view body) {
    std::uint32_t c = 0xffffffff;
    for (auto part : {head, body})
        for (unsigned char byte : part) {
            c ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
        }
    return ~c;
}
void put_u32(std::string& s, std::uint32_t n) {
    for (unsigned shift = 0; shift < 32; shift += 8) s.push_back(static_cast<char>(n >> shift));
}
std::uint32_t u32(std::string_view s, std::size_t offset) {
    std::uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i)
        n |= std::uint32_t(static_cast<unsigned char>(s[offset + i])) << (i * 8);
    return n;
}
std::string record(std::string_view key, std::optional<std::string_view> value) {
    if (key.empty() || key.size() > limit || (value && value->size() > limit))
        throw std::invalid_argument("keys must have 1 to 8 MiB worth of bytes; values are capped at 8 MiB");
    std::string bytes;
    bytes.reserve(header + key.size() + (value ? value->size() : 0));
    put_u32(bytes, magic);
    put_u32(bytes, static_cast<std::uint32_t>(key.size()));
    put_u32(bytes, value ? static_cast<std::uint32_t>(value->size()) : tombstone);
    put_u32(bytes, 0);
    bytes.append(key);
    if (value) bytes.append(*value);
    auto view = std::string_view(bytes);
    const auto checksum = crc(view.substr(0, 12), view.substr(header));
    for (unsigned i = 0; i < 4; ++i) bytes[12 + i] = static_cast<char>(checksum >> (i * 8));
    return bytes;
}
}
}
