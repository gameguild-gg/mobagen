#include "asset_cache.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace mobagen::assets {
  namespace {

    constexpr std::size_t temporary_file_attempts = 128;
    constexpr std::size_t streaming_buffer_size = 64 * 1024;
    std::atomic_uint64_t temporary_file_sequence{0};

    enum class InstallStatus : std::uint8_t { installed, destination_exists, failed };

    struct FileHashResult {
      AssetCacheStatus status{AssetCacheStatus::io_error};
      std::optional<AssetId> id;
      std::uintmax_t size{0};
      std::error_code system_error;
    };

    [[nodiscard]] std::error_code last_system_error() {
#ifdef _WIN32
      return {static_cast<int>(GetLastError()), std::system_category()};
#else
      return {errno, std::generic_category()};
#endif
    }

    [[nodiscard]] std::filesystem::path temporary_path_for(const std::filesystem::path& destination, std::uint64_t nonce, std::size_t attempt) {
      auto filename = destination.filename();
      filename += ".tmp-";
      filename += std::to_string(nonce);
      filename += '-';
      filename += std::to_string(temporary_file_sequence.fetch_add(1, std::memory_order_relaxed));
      filename += '-';
      filename += std::to_string(attempt);
      return destination.parent_path() / filename;
    }

    class TemporaryBlobFile {
    public:
#ifdef _WIN32
      using NativeHandle = HANDLE;
      static constexpr NativeHandle invalid_handle = INVALID_HANDLE_VALUE;
#else
      using NativeHandle = int;
      static constexpr NativeHandle invalid_handle = -1;
#endif

      TemporaryBlobFile(std::filesystem::path path, NativeHandle handle) : path_(std::move(path)), handle_(handle) {}
      TemporaryBlobFile(const TemporaryBlobFile&) = delete;
      TemporaryBlobFile& operator=(const TemporaryBlobFile&) = delete;

      ~TemporaryBlobFile() {
        close_ignoring_errors();
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
      }

      [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

      [[nodiscard]] bool write_all(std::span<const std::byte> bytes, std::error_code& error) {
        const auto* cursor = bytes.data();
        auto remaining = bytes.size();
        while (remaining != 0) {
#ifdef _WIN32
          const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
          DWORD written = 0;
          if (WriteFile(handle_, cursor, chunk, &written, nullptr) == 0 || written == 0) {
            error = last_system_error();
            return false;
          }
#else
          const auto chunk = std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
          const auto written = ::write(handle_, cursor, chunk);
          if (written < 0 && errno == EINTR) {
            continue;
          }
          if (written <= 0) {
            error = last_system_error();
            return false;
          }
#endif
          cursor += written;
          remaining -= written;
        }
        return true;
      }

      [[nodiscard]] bool flush_and_close(std::error_code& error) {
#ifdef _WIN32
        if (FlushFileBuffers(handle_) == 0) {
          error = last_system_error();
          close_ignoring_errors();
          return false;
        }
        if (CloseHandle(handle_) == 0) {
          error = last_system_error();
          handle_ = invalid_handle;
          return false;
        }
#else
        int flush_result = 0;
        do {
          flush_result = ::fsync(handle_);
        } while (flush_result < 0 && errno == EINTR);
        if (flush_result < 0) {
          error = last_system_error();
          close_ignoring_errors();
          return false;
        }
        if (::close(handle_) < 0) {
          error = last_system_error();
          handle_ = invalid_handle;
          return false;
        }
#endif
        handle_ = invalid_handle;
        return true;
      }

    private:
      void close_ignoring_errors() noexcept {
        if (handle_ == invalid_handle) {
          return;
        }
#ifdef _WIN32
        CloseHandle(handle_);
#else
        ::close(handle_);
#endif
        handle_ = invalid_handle;
      }

      std::filesystem::path path_;
      NativeHandle handle_{invalid_handle};
    };

    [[nodiscard]] std::unique_ptr<TemporaryBlobFile> create_temporary_file(const std::filesystem::path& destination, std::error_code& error) {
      const auto nonce = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
      for (std::size_t attempt = 0; attempt < temporary_file_attempts; ++attempt) {
        auto path = temporary_path_for(destination, nonce, attempt);
#ifdef _WIN32
        auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != TemporaryBlobFile::invalid_handle) {
          return std::make_unique<TemporaryBlobFile>(std::move(path), handle);
        }
        const auto windows_error = GetLastError();
        if (windows_error == ERROR_FILE_EXISTS || windows_error == ERROR_ALREADY_EXISTS) {
          continue;
        }
#else
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#  ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#  endif
        const auto handle = ::open(path.c_str(), flags, 0666);
        if (handle != TemporaryBlobFile::invalid_handle) {
          return std::make_unique<TemporaryBlobFile>(std::move(path), handle);
        }
        if (errno == EEXIST) {
          continue;
        }
#endif
        error = last_system_error();
        return nullptr;
      }
      error = std::make_error_code(std::errc::file_exists);
      return nullptr;
    }

    [[nodiscard]] InstallStatus install_temporary_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                                                       std::error_code& error) {
#ifdef _WIN32
      if (MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0) {
        return InstallStatus::installed;
      }
      const auto windows_error = GetLastError();
      if (windows_error == ERROR_FILE_EXISTS || windows_error == ERROR_ALREADY_EXISTS) {
        return InstallStatus::destination_exists;
      }
#else
      if (::link(temporary.c_str(), destination.c_str()) == 0) {
        return InstallStatus::installed;
      }
      if (errno == EEXIST) {
        return InstallStatus::destination_exists;
      }
#endif
      error = last_system_error();
      return InstallStatus::failed;
    }

    [[nodiscard]] AssetCacheStoreResult store_failure(AssetCacheStatus status, std::optional<AssetId> id, std::filesystem::path path,
                                                      std::error_code error = {}) {
      return {status, id, std::move(path), error};
    }

    [[nodiscard]] FileHashResult hash_file(const std::filesystem::path& source, std::size_t max_bytes) {
      std::error_code error;
      const auto exists = std::filesystem::exists(source, error);
      if (error) {
        return {AssetCacheStatus::io_error, std::nullopt, 0, error};
      }
      if (!exists) {
        return {AssetCacheStatus::not_found, std::nullopt, 0, {}};
      }
      if (!std::filesystem::is_regular_file(source, error)) {
        return {AssetCacheStatus::io_error, std::nullopt, 0, error ? error : std::make_error_code(std::errc::invalid_argument)};
      }
      const auto expected_size = std::filesystem::file_size(source, error);
      if (error) {
        return {AssetCacheStatus::io_error, std::nullopt, 0, error};
      }
      if (expected_size > max_bytes) {
        return {AssetCacheStatus::too_large, std::nullopt, expected_size, {}};
      }

      std::ifstream stream(source, std::ios::binary);
      if (!stream.good()) {
        return {AssetCacheStatus::io_error, std::nullopt, 0, std::make_error_code(std::errc::io_error)};
      }
      Sha256Hasher hasher;
      std::array<std::byte, streaming_buffer_size> buffer{};
      std::uintmax_t total = 0;
      while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto read = stream.gcount();
        if (read > 0) {
          total += static_cast<std::uintmax_t>(read);
          if (total > max_bytes) {
            return {AssetCacheStatus::too_large, std::nullopt, total, {}};
          }
          if (!hasher.update(std::span<const std::byte>{buffer.data(), static_cast<std::size_t>(read)})) {
            return {AssetCacheStatus::too_large, std::nullopt, total, {}};
          }
        }
      }
      if (stream.bad()) {
        return {AssetCacheStatus::io_error, std::nullopt, total, std::make_error_code(std::errc::io_error)};
      }
      if (total != expected_size) {
        return {AssetCacheStatus::source_changed, std::nullopt, total, {}};
      }
      return {AssetCacheStatus::loaded, hasher.finish(), total, {}};
    }

    struct StreamStoreState {
      TemporaryBlobFile* file{};
      Sha256Hasher hasher;
      std::size_t expected_size{};
      std::size_t received{};
      std::error_code system_error;
      bool size_mismatch{};
      bool hash_failed{};
    };

    bool store_stream_chunk(void* context, std::span<const std::byte> bytes) noexcept {
      auto& state = *static_cast<StreamStoreState*>(context);
      if (state.size_mismatch || state.hash_failed || state.system_error) return false;
      if (bytes.size() > state.expected_size - state.received) {
        state.size_mismatch = true;
        return false;
      }
      if (!state.hasher.update(bytes)) {
        state.hash_failed = true;
        return false;
      }
      if (!state.file->write_all(bytes, state.system_error)) return false;
      state.received += bytes.size();
      return true;
    }

  }  // namespace

  std::filesystem::path AssetCache::path_for(const AssetId& id) const {
    const auto text = to_string(id);
    constexpr std::size_t prefix_size = 7;
    const auto digest = std::string_view{text}.substr(prefix_size);
    return root_ / "sha256" / digest.substr(0, 2) / digest.substr(2);
  }

  AssetCacheStoreResult AssetCache::store(std::span<const std::byte> bytes) const {
    if (root_.empty()) {
      return store_failure(AssetCacheStatus::invalid_root, std::nullopt, {});
    }
    if (bytes.size() > max_blob_bytes_) {
      return store_failure(AssetCacheStatus::too_large, std::nullopt, {});
    }

    const auto id = sha256(bytes);
    if (!id.has_value()) {
      return store_failure(AssetCacheStatus::too_large, std::nullopt, {});
    }
    const auto destination = path_for(*id);

    std::error_code error;
    const auto exists = std::filesystem::exists(destination, error);
    if (error) {
      return store_failure(AssetCacheStatus::io_error, id, destination, error);
    }
    if (exists) {
      const auto existing = load(*id);
      if (existing.ok()) {
        return {AssetCacheStatus::already_present, id, destination, {}};
      }
      return store_failure(existing.status, id, destination, existing.system_error);
    }

    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
      return store_failure(AssetCacheStatus::io_error, id, destination, error);
    }

    auto temporary = create_temporary_file(destination, error);
    if (!temporary) {
      return store_failure(AssetCacheStatus::io_error, id, destination, error);
    }
    if (!temporary->write_all(bytes, error) || !temporary->flush_and_close(error)) {
      return store_failure(AssetCacheStatus::io_error, id, destination, error);
    }

    const auto installed = install_temporary_file(temporary->path(), destination, error);
    if (installed == InstallStatus::installed) {
      return {AssetCacheStatus::stored, id, destination, {}};
    }
    if (installed == InstallStatus::failed) {
      return store_failure(AssetCacheStatus::io_error, id, destination, error);
    }

    const auto existing = load(*id);
    if (existing.ok()) {
      return {AssetCacheStatus::already_present, id, destination, {}};
    }
    return store_failure(existing.status, id, destination, existing.system_error);
  }

  AssetCacheStoreResult AssetCache::store_file(const std::filesystem::path& source) const {
    if (root_.empty()) {
      return store_failure(AssetCacheStatus::invalid_root, std::nullopt, {});
    }

    const auto hashed = hash_file(source, max_blob_bytes_);
    if (hashed.status != AssetCacheStatus::loaded || !hashed.id.has_value()) {
      return store_failure(hashed.status, std::nullopt, source, hashed.system_error);
    }
    const auto destination = path_for(*hashed.id);

    std::error_code error;
    const auto exists = std::filesystem::exists(destination, error);
    if (error) {
      return store_failure(AssetCacheStatus::io_error, hashed.id, destination, error);
    }
    if (exists) {
      const auto existing = load(*hashed.id);
      if (existing.ok()) {
        return {AssetCacheStatus::already_present, hashed.id, destination, {}};
      }
      return store_failure(existing.status, hashed.id, destination, existing.system_error);
    }

    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
      return store_failure(AssetCacheStatus::io_error, hashed.id, destination, error);
    }
    auto temporary = create_temporary_file(destination, error);
    if (!temporary) {
      return store_failure(AssetCacheStatus::io_error, hashed.id, destination, error);
    }

    std::ifstream stream(source, std::ios::binary);
    if (!stream.good()) {
      return store_failure(AssetCacheStatus::io_error, hashed.id, source, std::make_error_code(std::errc::io_error));
    }
    Sha256Hasher copied_hasher;
    std::array<std::byte, streaming_buffer_size> buffer{};
    std::uintmax_t copied_size = 0;
    while (stream) {
      stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
      const auto read = stream.gcount();
      if (read > 0) {
        copied_size += static_cast<std::uintmax_t>(read);
        if (copied_size > max_blob_bytes_) {
          return store_failure(AssetCacheStatus::too_large, std::nullopt, source);
        }
        const auto chunk = std::span<const std::byte>{buffer.data(), static_cast<std::size_t>(read)};
        if (!copied_hasher.update(chunk)) {
          return store_failure(AssetCacheStatus::too_large, std::nullopt, source);
        }
        if (!temporary->write_all(chunk, error)) {
          return store_failure(AssetCacheStatus::io_error, hashed.id, destination, error);
        }
      }
    }
    if (stream.bad()) {
      return store_failure(AssetCacheStatus::io_error, hashed.id, source, std::make_error_code(std::errc::io_error));
    }
    const auto copied_id = copied_hasher.finish();
    if (copied_size != hashed.size || !copied_id.has_value() || *copied_id != *hashed.id) {
      return store_failure(AssetCacheStatus::source_changed, std::nullopt, source);
    }
    if (!temporary->flush_and_close(error)) {
      return store_failure(AssetCacheStatus::io_error, hashed.id, destination, error);
    }

    const auto installed = install_temporary_file(temporary->path(), destination, error);
    if (installed == InstallStatus::installed) {
      return {AssetCacheStatus::stored, hashed.id, destination, {}};
    }
    if (installed == InstallStatus::failed) {
      return store_failure(AssetCacheStatus::io_error, hashed.id, destination, error);
    }
    const auto existing = load(*hashed.id);
    if (existing.ok()) {
      return {AssetCacheStatus::already_present, hashed.id, destination, {}};
    }
    return store_failure(existing.status, hashed.id, destination, existing.system_error);
  }

  AssetCacheStoreResult AssetCache::store_stream(const AssetId& expected_id, std::size_t expected_size,
                                                 AssetCacheSource source) const {
    if (root_.empty()) {
      return store_failure(AssetCacheStatus::invalid_root, std::nullopt, {});
    }
    if (expected_size > max_blob_bytes_) {
      return store_failure(AssetCacheStatus::too_large, std::nullopt, {});
    }
    if (source.produce == nullptr) {
      return store_failure(AssetCacheStatus::source_changed, std::nullopt, {});
    }

    const auto destination = path_for(expected_id);
    std::error_code error;
    const auto exists = std::filesystem::exists(destination, error);
    if (error) {
      return store_failure(AssetCacheStatus::io_error, expected_id, destination, error);
    }
    if (exists) {
      const auto existing = hash_file(destination, max_blob_bytes_);
      if (existing.status != AssetCacheStatus::loaded || !existing.id.has_value()) {
        return store_failure(existing.status, expected_id, destination, existing.system_error);
      }
      if (*existing.id == expected_id && existing.size == expected_size) {
        return {AssetCacheStatus::already_present, expected_id, destination, {}};
      }
      return store_failure(AssetCacheStatus::integrity_error, expected_id, destination);
    }

    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
      return store_failure(AssetCacheStatus::io_error, expected_id, destination, error);
    }
    auto temporary = create_temporary_file(destination, error);
    if (!temporary) {
      return store_failure(AssetCacheStatus::io_error, expected_id, destination, error);
    }

    StreamStoreState state{.file = temporary.get(), .expected_size = expected_size};
    const auto produced = source.produce(source.context, {.context = &state, .write = store_stream_chunk});
    if (!produced || state.size_mismatch || state.hash_failed || state.received != expected_size) {
      if (state.system_error) {
        return store_failure(AssetCacheStatus::io_error, expected_id, destination, state.system_error);
      }
      return store_failure(AssetCacheStatus::source_changed, std::nullopt, destination);
    }
    const auto actual_id = state.hasher.finish();
    if (!actual_id.has_value() || *actual_id != expected_id) {
      return store_failure(AssetCacheStatus::source_changed, std::nullopt, destination);
    }
    if (!temporary->flush_and_close(error)) {
      return store_failure(AssetCacheStatus::io_error, expected_id, destination, error);
    }

    const auto installed = install_temporary_file(temporary->path(), destination, error);
    if (installed == InstallStatus::installed) {
      return {AssetCacheStatus::stored, expected_id, destination, {}};
    }
    if (installed == InstallStatus::failed) {
      return store_failure(AssetCacheStatus::io_error, expected_id, destination, error);
    }
    const auto existing = hash_file(destination, max_blob_bytes_);
    if (existing.status == AssetCacheStatus::loaded && existing.id == expected_id
        && existing.size == expected_size) {
      return {AssetCacheStatus::already_present, expected_id, destination, {}};
    }
    return store_failure(existing.status == AssetCacheStatus::loaded ? AssetCacheStatus::integrity_error : existing.status,
                         expected_id, destination, existing.system_error);
  }

  AssetCacheLoadResult AssetCache::load(const AssetId& id) const {
    if (root_.empty()) {
      return {AssetCacheStatus::invalid_root, {}, {}};
    }
    const auto path = path_for(id);
    std::error_code error;
    const auto exists = std::filesystem::exists(path, error);
    if (error) {
      return {AssetCacheStatus::io_error, {}, error};
    }
    if (!exists) {
      return {AssetCacheStatus::not_found, {}, {}};
    }
    if (!std::filesystem::is_regular_file(path, error)) {
      return {AssetCacheStatus::io_error, {}, error ? error : std::make_error_code(std::errc::invalid_argument)};
    }

    const auto file_size = std::filesystem::file_size(path, error);
    if (error) {
      return {AssetCacheStatus::io_error, {}, error};
    }
    if (file_size > max_blob_bytes_ || file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())
        || file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
      return {AssetCacheStatus::too_large, {}, {}};
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.good()) {
      return {AssetCacheStatus::io_error, {}, std::make_error_code(std::errc::io_error)};
    }
    std::vector<std::byte> contents(static_cast<std::size_t>(file_size));
    if (!contents.empty()) {
      stream.read(reinterpret_cast<char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
      if (stream.gcount() != static_cast<std::streamsize>(contents.size())) {
        return {AssetCacheStatus::integrity_error, {}, {}};
      }
    }
    char trailing = 0;
    stream.read(&trailing, 1);
    if (stream.gcount() != 0) {
      return {AssetCacheStatus::integrity_error, {}, {}};
    }

    const auto actual = sha256(contents);
    if (!actual.has_value() || *actual != id) {
      return {AssetCacheStatus::integrity_error, {}, {}};
    }
    return {AssetCacheStatus::loaded, std::move(contents), {}};
  }

}  // namespace mobagen::assets
