#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mobagen::compositions::detail {

  inline constexpr std::uintmax_t max_project_plugin_binary_bytes = std::uintmax_t{1} << 30U;

  struct ProjectManifestReadResult {
    std::optional<std::string> contents;
    std::filesystem::path absolute_path;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return contents.has_value() && error.empty(); }
  };

  struct ProjectPluginHashResult {
    std::optional<std::string> hash;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return hash.has_value() && error.empty(); }
  };

  [[nodiscard]] ProjectManifestReadResult read_project_manifest_bounded(const std::filesystem::path& path);
  [[nodiscard]] std::optional<std::string> hash_project_manifest(std::string_view contents);
  [[nodiscard]] ProjectPluginHashResult hash_project_plugin_binary(const std::filesystem::path& path,
                                                                   std::uintmax_t max_bytes = max_project_plugin_binary_bytes);

}  // namespace mobagen::compositions::detail
