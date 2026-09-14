#pragma once

#include "plugin_abi.h"
#include "plugin_contract.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace mobagen::plugins {

  enum class NativePluginLoadIssueCode : std::uint8_t {
    invalid_host,
    invalid_path,
    open_failed,
    missing_entry_point,
    entry_failed,
    invalid_descriptor,
  };

  struct NativePluginLoadIssue {
    NativePluginLoadIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
    std::vector<PluginContractIssue> contract_issues;
  };

  class NativePlugin {
  public:
    NativePlugin() = default;
    ~NativePlugin();
    NativePlugin(const NativePlugin&) = delete;
    NativePlugin& operator=(const NativePlugin&) = delete;
    NativePlugin(NativePlugin&& other) noexcept;
    NativePlugin& operator=(NativePlugin&& other) noexcept;

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] const NativePluginContract& contract() const noexcept { return *contract_; }

  private:
    friend struct NativePluginLoadResult;
    friend NativePluginLoadResult load_native_plugin_binary(const std::filesystem::path&, const MobagenHostApiV1&);

    NativePlugin(std::filesystem::path path, void* library_handle, NativePluginContract contract);
    void reset() noexcept;

    std::filesystem::path path_;
    void* library_handle_{nullptr};
    std::optional<NativePluginContract> contract_;
  };

  struct NativePluginLoadResult {
    std::optional<NativePlugin> plugin;
    std::vector<NativePluginLoadIssue> issues;
  };

  [[nodiscard]] NativePluginLoadResult load_native_plugin_binary(const std::filesystem::path& path, const MobagenHostApiV1& host);

}  // namespace mobagen::plugins
