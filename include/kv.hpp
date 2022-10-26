#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kv {
class Store {
    struct State;
public:
    explicit Store(const std::filesystem::path& directory);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    std::optional<std::string> get(std::string_view key) const;
    void set(std::string_view key, std::string_view value);
    void erase(std::string_view key);
    void compact();
private:
    std::unique_ptr<State> state_;
};
}
