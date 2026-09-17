#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace mobagen::plugins {

  enum class PluginPackageKind : std::uint8_t { Native, PortableWasm };

  enum class PluginPackageInspectionIssueCode : std::uint8_t { InvalidPath, InvalidContents, InspectionFailure };

  struct PluginPackageInspectionIssue {
    PluginPackageInspectionIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
  };

  struct PluginPackageInspectionResult {
    std::optional<PluginPackageKind> kind;
    std::optional<PluginPackageInspectionIssue> issue;

    [[nodiscard]] bool ok() const noexcept { return kind.has_value() && !issue.has_value(); }
  };

  /* Classifies a strict .plugin package by its single canonical binary without loading executable code. */
  [[nodiscard]] PluginPackageInspectionResult inspect_plugin_package(const std::filesystem::path& package);

}  // namespace mobagen::plugins
