#pragma once

#include "plugin_host.hpp"
#include "plugin_loader.hpp"

#include "modules/descriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mobagen::plugins {

  inline constexpr std::size_t max_native_plugin_store_packages = 4096;

  enum class NativePluginStoreIssueCode : std::uint8_t {
    InvalidRoot,
    EnumerationFailed,
    LimitExceeded,
    InvalidSource,
    StageFailed,
    CommitFailed,
    RollbackFailed,
    CleanupDeferred,
    InvalidProvider,
    NotFound,
  };

  struct NativePluginStoreIssue {
    NativePluginStoreIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
    std::vector<NativePluginLoadIssue> load_issues;
  };

  struct NativePluginStoreActionResult {
    bool changed{false};
    bool committed{false};
    std::string provider_id;
    modules::SemanticVersion version;
    std::filesystem::path package;
    std::vector<NativePluginStoreIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return committed; }
  };

  struct NativePluginStoreEntry {
    std::string provider_id;
    modules::SemanticVersion version;
    std::filesystem::path package;
  };

  struct NativePluginStoreListResult {
    std::vector<NativePluginStoreEntry> entries;
    std::vector<NativePluginStoreIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  class NativePluginStore {
  public:
    explicit NativePluginStore(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] NativePluginStoreActionResult install(const std::filesystem::path& source, PluginHost& host) const;
    [[nodiscard]] NativePluginStoreActionResult remove(std::string_view provider_id) const;
    [[nodiscard]] NativePluginStoreListResult list(PluginHost& host) const;

  private:
    std::filesystem::path root_;
  };

}  // namespace mobagen::plugins
