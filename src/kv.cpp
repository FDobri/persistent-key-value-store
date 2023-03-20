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

struct Store::State {
    explicit State(const std::filesystem::path& path)
        : directory(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC), "open database directory"),
          lock(openat(directory.fd, "LOCK", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600), "open lock") {
        if (flock(lock.fd, LOCK_EX | LOCK_NB) < 0) fail("lock database");
        File log(openat(directory.fd, "store.wal", O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                        0600), "open WAL");
        wal.swap(log);
        sync(directory.fd, true);
        recover();
    }
    File directory, lock, wal;
    std::map<std::string, std::string, std::less<>> values;
    bool poisoned{};

    void check() const { if (poisoned) throw std::runtime_error("reopen the database after a write failure"); }
    void recover() {
        struct stat state{};
        if (fstat(wal.fd, &state) < 0) fail("inspect WAL");
        off_t offset = 0;
        std::array<char, header> bytes{};
        while (state.st_size - offset >= static_cast<off_t>(header)) {
            if (!read_all(wal.fd, bytes, offset)) break;
            const std::string_view head(bytes.data(), bytes.size());
            const auto key_len = u32(head, 4), value_len = u32(head, 8);
            if (u32(head, 0) != magic || key_len == 0 || key_len > limit ||
                (value_len != tombstone && value_len > limit))
                throw std::runtime_error("invalid WAL record header");
            const auto size = key_len + (value_len == tombstone ? 0 : value_len);
            if (state.st_size - offset - header < size) break;
            std::string body(size, '\0');
            if (!read_all(wal.fd, body, offset + header)) break;
            if (crc(head.substr(0, 12), body) != u32(head, 12)) {
                if (offset + static_cast<off_t>(header + size) == state.st_size) break;
                throw std::runtime_error("checksum mismatch inside WAL");
            }
            auto key = body.substr(0, key_len);
            if (value_len == tombstone) values.erase(key);
            else values.insert_or_assign(std::move(key), body.substr(key_len));
            offset += header + size;
        }
        if (offset != state.st_size) {
            if (ftruncate(wal.fd, offset) < 0) fail("truncate incomplete WAL tail");
            sync(wal.fd);
        }
    }
    void append(std::string_view bytes) {
        check();
        try { write_all(wal.fd, bytes); sync(wal.fd); }
        catch (...) { poisoned = true; throw; }
    }
};

Store::Store(const std::filesystem::path& path) {
    if (std::filesystem::create_directory(path)) {
        auto parent = path.parent_path();
        if (parent.empty()) parent = ".";
        File parent_dir(open(parent.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC), "open parent directory");
        sync(parent_dir.fd, true);
    }
    state_ = std::make_unique<State>(path);
}
Store::~Store() = default;
std::optional<std::string> Store::get(std::string_view key) const {
    state_->check();
    auto it = state_->values.find(key);
    if (it == state_->values.end()) return std::nullopt;
    return it->second;
}
void Store::set(std::string_view key, std::string_view value) {
    auto bytes = record(key, value);
    decltype(state_->values) staging;
    staging.emplace(key, value);
    auto node = staging.extract(staging.begin());
    state_->append(bytes);
    auto inserted = state_->values.insert(std::move(node));
    if (!inserted.inserted) inserted.position->second.swap(inserted.node.mapped());
}
void Store::erase(std::string_view key) {
    auto bytes = record(key, std::nullopt);
    auto it = state_->values.find(key);
    state_->append(bytes);
    if (it != state_->values.end()) state_->values.erase(it);
}
}
