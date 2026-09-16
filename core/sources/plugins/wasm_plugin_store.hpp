#pragma once

#include "wasm_plugin_loader.hpp"

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

  inline constexpr std::size_t max_portable_wasm_plugin_store_packages = 4096;

  enum class PortableWasmPluginStoreIssueCode : std::uint8_t {
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

  struct PortableWasmPluginStoreIssue {
    PortableWasmPluginStoreIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
    std::vector<PortableWasmPluginLoadIssue> load_issues;
  };

  struct PortableWasmPluginStoreActionResult {
    bool changed{false};
    bool committed{false};
    std::string provider_id;
    modules::SemanticVersion version;
    std::filesystem::path package;
    std::vector<PortableWasmPluginStoreIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return committed; }
  };

  struct PortableWasmPluginStoreEntry {
    modules::ProviderDescriptor provider;
    std::filesystem::path package;
  };

  struct PortableWasmPluginStoreListResult {
    std::vector<PortableWasmPluginStoreEntry> entries;
    std::vector<PortableWasmPluginStoreIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  class PortableWasmPluginStore {
  public:
    explicit PortableWasmPluginStore(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] PortableWasmPluginStoreActionResult install(const std::filesystem::path& source, PortableWasmBackend& backend) const;
    [[nodiscard]] PortableWasmPluginStoreActionResult remove(std::string_view provider_id) const;
    [[nodiscard]] PortableWasmPluginStoreListResult list(PortableWasmBackend& backend) const;

  private:
    std::filesystem::path root_;
  };

}  // namespace mobagen::plugins
