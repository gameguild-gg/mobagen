#include "lockfile.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
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

namespace mobagen::modules {

  namespace {

    constexpr std::size_t temporary_file_attempts = 128;
    std::atomic_uint64_t temporary_file_sequence{0};

    std::error_code last_system_error() {
#ifdef _WIN32
      return {static_cast<int>(GetLastError()), std::system_category()};
#else
      return {errno, std::generic_category()};
#endif
    }

    LockfileWriteResult failure(LockfileWriteIssueCode code, const std::filesystem::path& path, std::error_code error, std::string message) {
      if (error) {
        message += ": ";
        message += error.message();
      }
      return {.issue = LockfileWriteIssue{code, path, error, std::move(message)}};
    }

    LockfileReadResult read_failure(LockfileReadIssueCode code, const std::filesystem::path& path, std::error_code error, std::string message) {
      if (error) {
        message += ": ";
        message += error.message();
      }
      return {.issue = LockfileReadIssue{code, path, error, std::move(message)}};
    }

    std::filesystem::path temporary_path_for(const std::filesystem::path& destination, std::uint64_t nonce, std::size_t attempt) {
      auto filename = destination.filename();
      filename += ".tmp-";
      filename += std::to_string(nonce);
      filename += '-';
      filename += std::to_string(temporary_file_sequence.fetch_add(1, std::memory_order_relaxed));
      filename += '-';
      filename += std::to_string(attempt);
      const auto parent = destination.parent_path();
      return parent.empty() ? filename : parent / filename;
    }

    class TemporaryFile {
    public:
#ifdef _WIN32
      using NativeHandle = HANDLE;
      static constexpr NativeHandle invalid_handle = INVALID_HANDLE_VALUE;
#else
      using NativeHandle = int;
      static constexpr NativeHandle invalid_handle = -1;
#endif

      TemporaryFile(std::filesystem::path path, NativeHandle handle) : path_(std::move(path)), handle_(handle) {}
      TemporaryFile(const TemporaryFile&) = delete;
      TemporaryFile& operator=(const TemporaryFile&) = delete;

      ~TemporaryFile() {
        close_ignoring_errors();
        if (!path_.empty()) {
          std::error_code ignored;
          std::filesystem::remove(path_, ignored);
        }
      }

      [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

      bool write_all(std::string_view contents, std::error_code& error) {
        const char* cursor = contents.data();
        std::size_t remaining = contents.size();
        while (remaining > 0) {
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
          if (written < 0 && errno == EINTR) continue;
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

      bool flush_and_close(std::error_code& error) {
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

      void committed() noexcept { path_.clear(); }

    private:
      void close_ignoring_errors() noexcept {
        if (handle_ == invalid_handle) return;
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

    std::unique_ptr<TemporaryFile> create_temporary_file(const std::filesystem::path& destination, std::error_code& error) {
      const auto nonce = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
      for (std::size_t attempt = 0; attempt < temporary_file_attempts; ++attempt) {
        auto temporary_path = temporary_path_for(destination, nonce, attempt);
#ifdef _WIN32
        auto handle = CreateFileW(temporary_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != TemporaryFile::invalid_handle) return std::make_unique<TemporaryFile>(std::move(temporary_path), handle);
        const auto windows_error = GetLastError();
        if (windows_error == ERROR_FILE_EXISTS || windows_error == ERROR_ALREADY_EXISTS) continue;
#else
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#  ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#  endif
        const auto handle = ::open(temporary_path.c_str(), flags, 0666);
        if (handle != TemporaryFile::invalid_handle) return std::make_unique<TemporaryFile>(std::move(temporary_path), handle);
        if (errno == EEXIST) continue;
#endif
        error = last_system_error();
        return nullptr;
      }
      error = std::make_error_code(std::errc::file_exists);
      return nullptr;
    }

    bool commit_temporary_file(const std::filesystem::path& temporary, const std::filesystem::path& destination, std::error_code& error) {
#ifdef _WIN32
      if (MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) return true;
#else
      if (::rename(temporary.c_str(), destination.c_str()) == 0) return true;
#endif
      error = last_system_error();
      return false;
    }

  }  // namespace

  LockfileReadResult read_lockfile_bounded(const std::filesystem::path& source) {
    const auto filename = source.filename();
    if (source.empty() || filename.empty() || filename == "." || filename == "..") {
      return read_failure(LockfileReadIssueCode::InvalidPath, source, {}, "lockfile source must name a file");
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(source, error);
    if (status.type() == std::filesystem::file_type::not_found) {
      return read_failure(LockfileReadIssueCode::NotFound, source, {}, "lockfile does not exist");
    }
    if (error) {
      return read_failure(LockfileReadIssueCode::ReadFailed, source, error, "lockfile could not be inspected");
    }
    if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
      return read_failure(LockfileReadIssueCode::InvalidPath, source, {}, "lockfile must be a real regular file, not a directory or symbolic link");
    }

    const auto expected_size = std::filesystem::file_size(source, error);
    if (error) {
      return read_failure(LockfileReadIssueCode::ReadFailed, source, error, "lockfile size could not be read");
    }
    if (expected_size > max_lockfile_bytes) {
      return read_failure(LockfileReadIssueCode::TooLarge, source, {}, "lockfile exceeds the 1 MiB size limit");
    }
    const auto expected_write_time = std::filesystem::last_write_time(source, error);
    if (error) {
      return read_failure(LockfileReadIssueCode::ReadFailed, source, error, "lockfile modification time could not be read");
    }

    std::ifstream stream(source, std::ios::binary);
    if (!stream.is_open()) {
      return read_failure(LockfileReadIssueCode::ReadFailed, source, std::make_error_code(std::errc::io_error), "lockfile could not be opened");
    }
    std::string contents(static_cast<std::size_t>(expected_size), '\0');
    if (!contents.empty()) {
      stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
      if (stream.gcount() != static_cast<std::streamsize>(contents.size())) {
        return read_failure(LockfileReadIssueCode::Changed, source, {}, "lockfile changed or became unreadable while loading");
      }
    }
    char trailing = 0;
    stream.read(&trailing, 1);
    if (stream.gcount() != 0 || stream.bad()) {
      return read_failure(LockfileReadIssueCode::Changed, source, {}, "lockfile changed or became unreadable while loading");
    }

    const auto actual_size = std::filesystem::file_size(source, error);
    if (error) {
      return read_failure(LockfileReadIssueCode::Changed, source, error, "lockfile changed while loading");
    }
    const auto actual_write_time = std::filesystem::last_write_time(source, error);
    if (error || actual_size != expected_size || actual_write_time != expected_write_time) {
      return read_failure(LockfileReadIssueCode::Changed, source, error, "lockfile changed while loading");
    }
    return {.contents = std::move(contents)};
  }

  LockfileWriteResult write_lockfile_atomic(const std::filesystem::path& destination, std::string_view contents) {
    const auto filename = destination.filename();
    if (destination.empty() || filename.empty() || filename == "." || filename == "..") {
      return failure(LockfileWriteIssueCode::InvalidPath, destination, {}, "lockfile destination must name a file");
    }

    std::error_code error;
    auto temporary = create_temporary_file(destination, error);
    if (!temporary) {
      return failure(LockfileWriteIssueCode::CreateFailed, destination, error, "could not create a lockfile temporary file");
    }
    if (!temporary->write_all(contents, error) || !temporary->flush_and_close(error)) {
      return failure(LockfileWriteIssueCode::WriteFailed, temporary->path(), error, "could not write the complete lockfile temporary file");
    }
    if (!commit_temporary_file(temporary->path(), destination, error)) {
      return failure(LockfileWriteIssueCode::CommitFailed, destination, error, "could not atomically replace the lockfile");
    }

    temporary->committed();
    return {};
  }

}  // namespace mobagen::modules
