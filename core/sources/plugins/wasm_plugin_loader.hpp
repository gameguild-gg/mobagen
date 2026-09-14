#pragma once

#include "wasm_host_imports.hpp"
#include "wasm_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mobagen::plugins {

  inline constexpr std::size_t max_portable_wasm_plugin_binary_bytes = 64U * 1024U * 1024U;

  struct PortableWasmInstantiationResult {
    std::unique_ptr<PortableWasmInstance> instance;
    std::optional<std::string> error;

    [[nodiscard]] bool ok() const noexcept { return instance != nullptr && !error.has_value(); }
    [[nodiscard]] static PortableWasmInstantiationResult success(std::unique_ptr<PortableWasmInstance> instance);
    [[nodiscard]] static PortableWasmInstantiationResult failure(std::string error);
  };

  class PortableWasmBackend {
  public:
    PortableWasmBackend() = default;
    PortableWasmBackend(const PortableWasmBackend&) = delete;
    PortableWasmBackend& operator=(const PortableWasmBackend&) = delete;
    PortableWasmBackend(PortableWasmBackend&&) = delete;
    PortableWasmBackend& operator=(PortableWasmBackend&&) = delete;
    virtual ~PortableWasmBackend() = default;

    /* The backend must synchronously consume or compile the borrowed binary and retain host_imports in the returned instance. */
    [[nodiscard]] virtual PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary,
                                                                      std::shared_ptr<WasmHostImports> host_imports) = 0;
  };

  class LoadedPortableWasmPlugin {
  public:
    LoadedPortableWasmPlugin() = default;
    LoadedPortableWasmPlugin(const LoadedPortableWasmPlugin&) = delete;
    LoadedPortableWasmPlugin& operator=(const LoadedPortableWasmPlugin&) = delete;
    LoadedPortableWasmPlugin(LoadedPortableWasmPlugin&&) noexcept = default;
    LoadedPortableWasmPlugin& operator=(LoadedPortableWasmPlugin&&) noexcept = default;

    [[nodiscard]] bool loaded() const noexcept { return instance_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] const modules::ProviderDescriptor& provider() const noexcept { return provider_; }
    /* Transfers the queried instance into activation; loaded() becomes false. */
    [[nodiscard]] std::unique_ptr<PortableWasmInstance> take_instance() noexcept { return std::move(instance_); }

  private:
    friend struct PortableWasmPluginLoadResult;
    friend PortableWasmPluginLoadResult load_portable_wasm_plugin_binary(const std::filesystem::path&, PortableWasmBackend&, WasmHostServices);
    friend PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(LoadedPortableWasmPlugin, std::span<const std::byte>);
    friend PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(LoadedPortableWasmPlugin,
                                                                                    std::shared_ptr<const modules::CapabilityRegistry>,
                                                                                    std::span<const std::byte>);

    LoadedPortableWasmPlugin(std::filesystem::path path, std::unique_ptr<PortableWasmInstance> instance, modules::ProviderDescriptor provider)
        : path_(std::move(path)), instance_(std::move(instance)), provider_(std::move(provider)) {}

    std::filesystem::path path_;
    std::unique_ptr<PortableWasmInstance> instance_;
    modules::ProviderDescriptor provider_;
  };

  enum class PortableWasmPluginLoadIssueCode : std::uint8_t {
    InvalidPath,
    OpenFailed,
    SizeLimit,
    InvalidBinary,
    UnsupportedVersion,
    BackendFailure,
    QueryFailed,
    OutOfMemory,
    InvalidPackage,
    MissingPackageBinary,
  };

  struct PortableWasmPluginLoadIssue {
    PortableWasmPluginLoadIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
    std::vector<WasmPluginQueryIssue> query_issues;
  };

  struct PortableWasmPluginLoadResult {
    std::optional<LoadedPortableWasmPlugin> plugin;
    std::vector<PortableWasmPluginLoadIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return plugin.has_value() && issues.empty(); }
  };

  [[nodiscard]] PortableWasmPluginLoadResult load_portable_wasm_plugin_binary(const std::filesystem::path& path, PortableWasmBackend& backend,
                                                                               WasmHostServices host_services = {});
  [[nodiscard]] std::filesystem::path portable_wasm_plugin_binary_filename();
  [[nodiscard]] PortableWasmPluginLoadResult load_portable_wasm_plugin_package(const std::filesystem::path& package, PortableWasmBackend& backend,
                                                                                WasmHostServices host_services = {});
  [[nodiscard]] PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(LoadedPortableWasmPlugin plugin,
                                                                                        std::span<const std::byte> configuration = {});
  /* The registry and permission grants must come from a successful module resolution over this plugin's catalog. */
  [[nodiscard]] PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(
      LoadedPortableWasmPlugin plugin, std::shared_ptr<const modules::CapabilityRegistry> resolved_registry,
      std::span<const std::byte> configuration = {});

}  // namespace mobagen::plugins
