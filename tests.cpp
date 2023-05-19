#include "kv.hpp"
#include <gtest/gtest.h>
#include <unistd.h>

class TempStore : public testing::Test {
protected:
    std::filesystem::path path;
    void SetUp() override {
        auto pattern = (std::filesystem::temp_directory_path() / "kv-tests-XXXXXX").string();
        char* result = mkdtemp(pattern.data());
        ASSERT_NE(result, nullptr);
        path = result;
    }
    void TearDown() override { if (!path.empty()) std::filesystem::remove_all(path); }
};

TEST_F(TempStore, CompactionPreservesLatestValuesAndContinuedWrites) {
    {
        kv::Store store(path);
        EXPECT_THROW(kv::Store second(path), std::system_error);
        store.set("x", std::string("a\0b", 3));
        store.set("old", "hello");
        store.erase("old");
        auto original = std::filesystem::file_size(path / "store.wal");
        store.compact();
        EXPECT_LT(std::filesystem::file_size(path / "store.wal"), original);
        store.set("y", "");
    }
    kv::Store reopened(path);
    EXPECT_EQ(reopened.get("x"), std::string("a\0b", 3));
    EXPECT_EQ(reopened.get("y"), "");
    EXPECT_FALSE(reopened.get("old").has_value());
}
