#include "project_support.hpp"

#include "assets/asset_id.hpp"
#include "modules/manifest_parser.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

namespace mobagen::compositions::detail {

  std::optional<std::string> hash_project_manifest(std::string_view contents) {
    const auto bytes = std::as_bytes(std::span{contents.data(), contents.size()});
    const auto digest = assets::sha256(bytes);
    if (!digest.has_value()) return std::nullopt;
    return assets::to_string(*digest);
  }

  ProjectManifestReadResult read_project_manifest_bounded(const std::filesystem::path& path) {
    ProjectManifestReadResult result;
    std::error_code error;
    result.absolute_path = std::filesystem::absolute(path, error);
    if (error) {
      result.error = "mobagen.yaml path could not be resolved";
      return result;
    }
    const auto status = std::filesystem::symlink_status(result.absolute_path, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
      result.error = "mobagen.yaml must be a readable regular file, not a symbolic link";
      return result;
    }
    const auto size = std::filesystem::file_size(result.absolute_path, error);
    if (error || size > modules::max_product_manifest_bytes || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
      result.error = "mobagen.yaml exceeds the 1 MiB input limit or its size is unavailable";
      return result;
    }

    std::ifstream stream(result.absolute_path, std::ios::binary);
    if (!stream.is_open()) {
      result.error = "mobagen.yaml could not be opened";
      return result;
    }
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (!contents.empty()) {
      stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
      if (stream.gcount() != static_cast<std::streamsize>(contents.size())) {
        result.error = "mobagen.yaml changed or became unreadable while loading";
        return result;
      }
    }
    char trailing = 0;
    stream.read(&trailing, 1);
    if (stream.gcount() != 0 || stream.bad()) {
      result.error = "mobagen.yaml changed or became unreadable while loading";
      return result;
    }
    result.contents = std::move(contents);
    return result;
  }

  ProjectPluginHashResult hash_project_plugin_binary(const std::filesystem::path& path, std::uintmax_t max_bytes) {
    ProjectPluginHashResult result;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
      result.error = "plugin binary is not a real regular file";
      return result;
    }
    const auto expected_size = std::filesystem::file_size(path, error);
    const auto expected_write_time = std::filesystem::last_write_time(path, error);
    if (error || expected_size > max_bytes) {
      result.error = "plugin binary size or modification time is unavailable, or exceeds its hashing limit";
      return result;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
      result.error = "plugin binary could not be opened for hashing";
      return result;
    }
    assets::Sha256Hasher hasher;
    std::array<std::byte, 64 * 1024> buffer{};
    std::uintmax_t total = 0;
    while (stream) {
      stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
      const auto read = stream.gcount();
      if (read > 0) {
        total += static_cast<std::uintmax_t>(read);
        if (total > expected_size || !hasher.update(std::span<const std::byte>{buffer.data(), static_cast<std::size_t>(read)})) {
          result.error = "plugin binary changed or exceeded the hashing limit while being read";
          return result;
        }
      }
    }
    if (stream.bad() || total != expected_size) {
      result.error = "plugin binary changed or became unreadable while being hashed";
      return result;
    }

    const auto actual_size = std::filesystem::file_size(path, error);
    const auto actual_write_time = std::filesystem::last_write_time(path, error);
    if (error || actual_size != expected_size || actual_write_time != expected_write_time) {
      result.error = "plugin binary changed while being hashed";
      return result;
    }
    const auto digest = hasher.finish();
    if (!digest.has_value()) {
      result.error = "plugin binary could not be hashed";
      return result;
    }
    result.hash = assets::to_string(*digest);
    return result;
  }

}  // namespace mobagen::compositions::detail
