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
}
}
