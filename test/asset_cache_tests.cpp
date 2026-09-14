#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "assets/asset_cache.hpp"

namespace {

  std::span<const std::byte> bytes(std::string_view value) { return {reinterpret_cast<const std::byte*>(value.data()), value.size()}; }

  class TemporaryCacheDirectory {
  public:
    TemporaryCacheDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() / ("mobagen-asset-cache-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryCacheDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(stream.good());
  }

  void write_bytes(const std::filesystem::path& path, std::span<const std::byte> contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
    REQUIRE(stream.good());
  }

  bool has_temporary_file(const std::filesystem::path& root) {
    return std::ranges::any_of(std::filesystem::recursive_directory_iterator(root),
                               [](const auto& entry) { return entry.path().filename().string().contains(".tmp-"); });
  }

}  // namespace

TEST_CASE("Asset cache: store and load use a canonical content path") {
  using mobagen::assets::AssetCache;
  using mobagen::assets::AssetCacheStatus;

  TemporaryCacheDirectory directory;
  AssetCache cache(directory.path(), 1024);
  const auto stored = cache.store(bytes("immutable asset bytes"));
  REQUIRE(stored.status == AssetCacheStatus::stored);
  REQUIRE(stored.id.has_value());
  CHECK(stored.path == cache.path_for(*stored.id));
  CHECK(stored.path.parent_path().filename().string().size() == 2);
  CHECK(stored.path.filename().string().size() == 62);
  CHECK(std::filesystem::is_regular_file(stored.path));
  CHECK_FALSE(has_temporary_file(directory.path()));

  const auto loaded = cache.load(*stored.id);
  REQUIRE(loaded.status == AssetCacheStatus::loaded);
  CHECK(loaded.bytes.size() == bytes("immutable asset bytes").size());
  CHECK(std::equal(loaded.bytes.begin(), loaded.bytes.end(), bytes("immutable asset bytes").begin()));

  const auto duplicate = cache.store(bytes("immutable asset bytes"));
  REQUIRE(duplicate.status == AssetCacheStatus::already_present);
  REQUIRE(duplicate.id.has_value());
  CHECK(*duplicate.id == *stored.id);
  CHECK_FALSE(has_temporary_file(directory.path()));
}

TEST_CASE("Asset cache: corrupt entries are detected and never overwritten silently") {
  using mobagen::assets::AssetCache;
  using mobagen::assets::AssetCacheStatus;

  TemporaryCacheDirectory directory;
  AssetCache cache(directory.path(), 1024);
  const auto stored = cache.store(bytes("trusted bytes"));
  REQUIRE(stored.status == AssetCacheStatus::stored);
  REQUIRE(stored.id.has_value());
  write_text(stored.path, "tampered");

  const auto loaded = cache.load(*stored.id);
  CHECK(loaded.status == AssetCacheStatus::integrity_error);
  CHECK(loaded.bytes.empty());
  const auto repeated = cache.store(bytes("trusted bytes"));
  CHECK(repeated.status == AssetCacheStatus::integrity_error);
  CHECK_FALSE(has_temporary_file(directory.path()));
}

TEST_CASE("Asset cache: concurrent writers converge on one immutable blob") {
  using mobagen::assets::AssetCache;
  using mobagen::assets::AssetCacheStatus;
  using mobagen::assets::AssetCacheStoreResult;

  TemporaryCacheDirectory directory;
  const AssetCache cache(directory.path(), 1024);
  std::array<AssetCacheStoreResult, 8> results;
  std::array<std::thread, 8> writers;
  std::barrier gate(static_cast<std::ptrdiff_t>(writers.size()));
  for (std::size_t index = 0; index < writers.size(); ++index) {
    writers[index] = std::thread([&, index] {
      gate.arrive_and_wait();
      results[index] = cache.store(bytes("concurrent immutable bytes"));
    });
  }
  for (auto& writer : writers) {
    writer.join();
  }

  CHECK(std::ranges::count_if(results, [](const auto& result) { return result.status == AssetCacheStatus::stored; }) == 1);
  CHECK(std::ranges::all_of(results, [](const auto& result) { return result.ok(); }));
  REQUIRE(results[0].id.has_value());
  const auto loaded = cache.load(*results[0].id);
  CHECK(loaded.status == AssetCacheStatus::loaded);
  CHECK_FALSE(has_temporary_file(directory.path()));
}

TEST_CASE("Asset cache: files are ingested incrementally without changing their identity") {
  using mobagen::assets::AssetCache;
  using mobagen::assets::AssetCacheStatus;

  TemporaryCacheDirectory directory;
  const auto source = directory.path() / "large-source.bin";
  std::vector<std::byte> contents(1024 * 1024 + 17);
  for (std::size_t index = 0; index < contents.size(); ++index) {
    contents[index] = std::byte{static_cast<unsigned char>(index % 251)};
  }
  write_bytes(source, contents);

  AssetCache cache(directory.path() / "cache", contents.size());
  const auto from_file = cache.store_file(source);
  REQUIRE(from_file.status == AssetCacheStatus::stored);
  REQUIRE(from_file.id.has_value());
  const auto from_memory = cache.store(contents);
  CHECK(from_memory.status == AssetCacheStatus::already_present);
  REQUIRE(from_memory.id.has_value());
  CHECK(*from_file.id == *from_memory.id);

  const auto loaded = cache.load(*from_file.id);
  REQUIRE(loaded.status == AssetCacheStatus::loaded);
  CHECK(loaded.bytes.size() == contents.size());
  CHECK((std::equal(loaded.bytes.begin(), loaded.bytes.end(), contents.begin())));
  CHECK_FALSE(has_temporary_file(cache.root()));
}

TEST_CASE("Asset cache: configured size limits apply before allocation or writes") {
  using mobagen::assets::AssetCache;
  using mobagen::assets::AssetCacheStatus;

  TemporaryCacheDirectory directory;
  AssetCache cache(directory.path(), 4);
  const auto rejected = cache.store(bytes("12345"));
  CHECK(rejected.status == AssetCacheStatus::too_large);
  CHECK_FALSE(rejected.id.has_value());

  const auto id = mobagen::assets::sha256(bytes("12345"));
  REQUIRE(id.has_value());
  const auto path = cache.path_for(*id);
  REQUIRE(std::filesystem::create_directories(path.parent_path()));
  write_text(path, "12345");
  const auto loaded = cache.load(*id);
  CHECK(loaded.status == AssetCacheStatus::too_large);
  CHECK(loaded.bytes.empty());

  const auto source = directory.path() / "oversized-source.bin";
  write_text(source, "12345");
  const auto file_rejected = cache.store_file(source);
  CHECK(file_rejected.status == AssetCacheStatus::too_large);
  CHECK_FALSE(file_rejected.id.has_value());
}

TEST_CASE("Asset cache: invalid roots and missing entries report explicit status") {
  using mobagen::assets::AssetCache;
  using mobagen::assets::AssetCacheStatus;

  const auto id = mobagen::assets::sha256(bytes("missing"));
  REQUIRE(id.has_value());
  AssetCache invalid({}, 1024);
  CHECK(invalid.store(bytes("data")).status == AssetCacheStatus::invalid_root);
  CHECK(invalid.load(*id).status == AssetCacheStatus::invalid_root);

  TemporaryCacheDirectory directory;
  AssetCache valid(directory.path(), 1024);
  CHECK(valid.load(*id).status == AssetCacheStatus::not_found);
  CHECK(valid.store_file(directory.path() / "missing.bin").status == AssetCacheStatus::not_found);
}
