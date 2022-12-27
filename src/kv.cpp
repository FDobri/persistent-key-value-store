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
}
}
