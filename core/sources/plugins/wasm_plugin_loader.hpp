#pragma once

#include "modules/module_manifest.hpp"
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

  struct PortableWasmPluginLoadResult;

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
                                                                       std::shared_ptr<WasmHostImports> host_imports)
        = 0;
  };

  /* Issue codes specific to the optional .aot payload of a v2 package. */
  enum class PortableWasmAotIssueCode : std::uint8_t {
    AotUnsupportedPlatform, /* this runtime build has AOT compiled out (e.g. iOS) */
    AotVersionMismatch,     /* manifest toolchain version does not match the runtime's WAMR version */
    AotInvalidBinary,       /* corrupted / truncated / unloadable .aot payload */
  };

  struct PortableWasmAotSelection {
    PortableWasmInstantiationResult result;
    std::optional<PortableWasmAotIssueCode> issue; /* set when the .aot payload was rejected */
    bool used_aot{false};                          /* true when the instantiated module came from plugin.aot */
  };

  /*
   * Optional SIDE interface for backends that can prefer an AOT payload over the
   * interpreter payload. Additive: PortableWasmBackend itself never changes and
   * every backend still satisfies it alone; the v2 package loader down-casts to
   * this interface only when the package carries a plugin.aot payload. Payload
   * selection therefore stays inside each backend (plan todo 7 Must NOT).
   */
  class AotAwarePortableWasmBackend {
  public:
    AotAwarePortableWasmBackend() = default;
    AotAwarePortableWasmBackend(const AotAwarePortableWasmBackend&) = delete;
    AotAwarePortableWasmBackend& operator=(const AotAwarePortableWasmBackend&) = delete;
    AotAwarePortableWasmBackend(AotAwarePortableWasmBackend&&) = delete;
    AotAwarePortableWasmBackend& operator=(AotAwarePortableWasmBackend&&) = delete;
    virtual ~AotAwarePortableWasmBackend() = default;

    /*
     * Prefer the .aot payload when the platform allows it; fall back to the
     * interpreter payload when .aot is absent. `aot_toolchain_version` is the
     * producing wamrc/WAMR version from module.manifest (todo 2's toolchain
     * field, protected by per-file hashing); empty means the manifest did not
     * declare one. A declared-but-mismatched version must be rejected loudly
     * BEFORE instantiation.
     */
    [[nodiscard]] virtual PortableWasmAotSelection instantiate_prefer_aot(std::span<const std::byte> wasm_binary,
                                                                         std::span<const std::byte> aot_binary,
                                                                         std::string_view aot_toolchain_version,
                                                                         std::shared_ptr<WasmHostImports> host_imports)
        = 0;
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
    friend PortableWasmPluginLoadResult load_portable_wasm_plugin_package(const std::filesystem::path&, PortableWasmBackend&, WasmHostServices);
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
    AotRejected,
    MissingManifest,
    ManifestInvalid,
    ApiVersionMismatch,
    AbiVersionMismatch,
    ThreadsPolicyMismatch,
    SharedMemoryMismatch,
    MissingExport,
    SignatureMismatch,
  };

  struct PortableWasmPluginLoadIssue {
    PortableWasmPluginLoadIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
    std::vector<WasmPluginQueryIssue> query_issues;
    std::optional<PortableWasmAotIssueCode> aot_issue; /* set when code == AotRejected */
  };

  struct PortableWasmPluginLoadResult {
    std::optional<LoadedPortableWasmPlugin> plugin;
    std::vector<PortableWasmPluginLoadIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return plugin.has_value() && issues.empty(); }

    /* Adopts an instantiated plugin into this result (private-ctor access via
     * the loader's friendship, exposed for its internal helpers). */
    void adopt_loaded_plugin(std::filesystem::path path, std::unique_ptr<PortableWasmInstance> instance, modules::ProviderDescriptor provider);
  };

  [[nodiscard]] std::string_view portable_wasm_aot_issue_name(PortableWasmAotIssueCode code) noexcept;

  /* Contract a v2 package's module.manifest must satisfy at load time (todo 11).
     An empty `signature` marks a pre-contract lockfile: manifest presence and
     the export-table digest check are skipped, every other field still applies. */
  struct PortableWasmModuleContract {
    std::uint32_t api_version{};
    std::uint32_t abi_version{};
    modules::ModuleThreadsPolicy threads{modules::ModuleThreadsPolicy::None};
    bool shared_memory{false};
    std::string signature;
  };

  [[nodiscard]] PortableWasmPluginLoadResult verify_portable_wasm_module_contract(const std::filesystem::path& package,
                                                                                  const PortableWasmModuleContract& contract);

  [[nodiscard]] PortableWasmPluginLoadResult load_portable_wasm_plugin_binary(const std::filesystem::path& path, PortableWasmBackend& backend,
                                                                              WasmHostServices host_services = {});
  [[nodiscard]] std::filesystem::path portable_wasm_plugin_binary_filename();
  [[nodiscard]] std::filesystem::path portable_wasm_plugin_aot_filename();
  [[nodiscard]] std::filesystem::path portable_wasm_plugin_manifest_filename();
  [[nodiscard]] PortableWasmPluginLoadResult load_portable_wasm_plugin_package(const std::filesystem::path& package, PortableWasmBackend& backend,
                                                                               WasmHostServices host_services = {});
  [[nodiscard]] PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(LoadedPortableWasmPlugin plugin,
                                                                                        std::span<const std::byte> configuration = {});
  /* The registry and permission grants must come from a successful module resolution over this plugin's catalog. */
  [[nodiscard]] PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(
      LoadedPortableWasmPlugin plugin, std::shared_ptr<const modules::CapabilityRegistry> resolved_registry,
      std::span<const std::byte> configuration = {});

}  // namespace mobagen::plugins
