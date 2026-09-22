#include "wasm_plugin_loader.hpp"

#include "modules/module_manifest.hpp"

#include <mobagen/module/module_abi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::plugins {
  namespace {

    constexpr std::string_view quiesce_export_symbol = MOBAGEN_MODULE_THREAD_QUIESCE_EXPORT_V1;
    constexpr std::array wasm_magic{std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}};
    constexpr std::array wasm_version_1{std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

    void add_issue(PortableWasmPluginLoadResult& result, PortableWasmPluginLoadIssueCode code, const std::filesystem::path& path, std::string message,
                   std::error_code system_error = {}, std::vector<WasmPluginQueryIssue> query_issues = {},
                   std::optional<PortableWasmAotIssueCode> aot_issue = {}) {
      result.issues.push_back({code, path, system_error, std::move(message), std::move(query_issues), aot_issue});
    }

  }  // namespace

  PortableWasmInstantiationResult PortableWasmInstantiationResult::success(std::unique_ptr<PortableWasmInstance> instance) {
    return {std::move(instance), std::nullopt};
  }

  PortableWasmInstantiationResult PortableWasmInstantiationResult::failure(std::string error) { return {nullptr, std::move(error)}; }

  PortableWasmPluginLoadResult load_portable_wasm_plugin_binary(const std::filesystem::path& path, PortableWasmBackend& backend,
                                                                WasmHostServices host_services) {
    PortableWasmPluginLoadResult result;
    if (path.empty()) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidPath, path, "portable WASM plugin path must name a file");
      return result;
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    if (error) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidPath, path, "portable WASM plugin path could not be resolved", error);
      return result;
    }
    const auto status = std::filesystem::symlink_status(absolute, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OpenFailed, absolute,
                "portable WASM plugin must be a readable regular file, not a symbolic link", error);
      return result;
    }

    const auto file_size = std::filesystem::file_size(absolute, error);
    if (error) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OpenFailed, absolute, "portable WASM plugin size is unavailable", error);
      return result;
    }
    if (file_size > max_portable_wasm_plugin_binary_bytes) {
      add_issue(result, PortableWasmPluginLoadIssueCode::SizeLimit, absolute, "portable WASM plugin exceeds the 64 MiB binary limit");
      return result;
    }
    if (file_size < wasm_magic.size() + wasm_version_1.size()) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidBinary, absolute, "portable WASM plugin header is truncated");
      return result;
    }

    std::vector<std::byte> binary;
    try {
      binary.resize(static_cast<std::size_t>(file_size));
    } catch (const std::bad_alloc&) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OutOfMemory, absolute, "portable WASM plugin buffer allocation failed");
      return result;
    }

    std::ifstream input(absolute, std::ios::binary);
    if (!input.is_open()) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OpenFailed, absolute, "portable WASM plugin could not be opened");
      return result;
    }
    input.read(reinterpret_cast<char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    if (input.gcount() != static_cast<std::streamsize>(binary.size())) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OpenFailed, absolute, "portable WASM plugin changed or became unreadable while loading");
      return result;
    }
    char trailing{};
    if (input.get(trailing) || !input.eof()) {
      add_issue(result, PortableWasmPluginLoadIssueCode::SizeLimit, absolute, "portable WASM plugin changed or exceeded its limit while loading");
      return result;
    }

    if (!std::ranges::equal(wasm_magic, std::span<const std::byte>{binary}.first(wasm_magic.size()))) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidBinary, absolute, "portable WASM plugin has an invalid WebAssembly magic header");
      return result;
    }
    if (!std::ranges::equal(wasm_version_1, std::span<const std::byte>{binary}.subspan(wasm_magic.size(), wasm_version_1.size()))) {
      add_issue(result, PortableWasmPluginLoadIssueCode::UnsupportedVersion, absolute,
                "portable WASM plugin does not use WebAssembly binary version 1");
      return result;
    }

    std::shared_ptr<WasmHostImports> host_imports;
    try {
      host_imports = std::make_shared<WasmHostImports>(host_services);
    } catch (const std::bad_alloc&) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OutOfMemory, absolute, "portable WASM host imports allocation failed");
      return result;
    }

    PortableWasmInstantiationResult instantiated;
    try {
      instantiated = backend.instantiate(binary, host_imports);
    } catch (const std::bad_alloc&) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OutOfMemory, absolute, "portable WASM backend ran out of memory");
      return result;
    } catch (const std::exception& exception) {
      add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, absolute, std::string{"portable WASM backend threw: "} + exception.what());
      return result;
    } catch (...) {
      add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, absolute, "portable WASM backend threw");
      return result;
    }
    if (!instantiated.ok()) {
      add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, absolute,
                instantiated.error.has_value() ? std::move(*instantiated.error) : "portable WASM backend returned no instance");
      return result;
    }
    if (instantiated.instance->host_imports() != host_imports.get()) {
      add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, absolute,
                "portable WASM backend returned an instance that does not retain its injected host imports");
      return result;
    }

    auto queried = query_portable_wasm_plugin(*instantiated.instance);
    if (!queried.ok()) {
      add_issue(result, PortableWasmPluginLoadIssueCode::QueryFailed, absolute, "portable WASM plugin descriptor query failed", {},
                std::move(queried.issues));
      return result;
    }

    result.plugin = LoadedPortableWasmPlugin{absolute, std::move(instantiated.instance), std::move(*queried.provider)};
    return result;
  }

  std::filesystem::path portable_wasm_plugin_binary_filename() { return "plugin.wasm"; }

  std::filesystem::path portable_wasm_plugin_aot_filename() { return "plugin.aot"; }

  std::filesystem::path portable_wasm_plugin_manifest_filename() { return "module.manifest"; }

  std::string_view portable_wasm_aot_issue_name(PortableWasmAotIssueCode code) noexcept {
    switch (code) {
      case PortableWasmAotIssueCode::AotUnsupportedPlatform:
        return "aot-unsupported-platform";
      case PortableWasmAotIssueCode::AotVersionMismatch:
        return "aot-version-mismatch";
      case PortableWasmAotIssueCode::AotInvalidBinary:
        return "aot-invalid-binary";
    }
    return "aot-unknown";
  }

  void PortableWasmPluginLoadResult::adopt_loaded_plugin(std::filesystem::path path, std::unique_ptr<PortableWasmInstance> instance,
                                                         modules::ProviderDescriptor provider) {
    plugin = LoadedPortableWasmPlugin{std::move(path), std::move(instance), std::move(provider)};
  }

  namespace {

    std::vector<std::byte> read_payload_file(const std::filesystem::path& path, PortableWasmPluginLoadResult& result, bool& ok) {
      std::vector<std::byte> bytes;
      ok = false;
      std::error_code error;
      const auto size = std::filesystem::file_size(path, error);
      if (error || size > max_portable_wasm_plugin_binary_bytes) {
        add_issue(result, PortableWasmPluginLoadIssueCode::SizeLimit, path,
                  "portable plugin payload size is unavailable or exceeds the 64 MiB binary limit", error);
        return bytes;
      }
      std::ifstream input(path, std::ios::binary);
      if (!input.is_open()) {
        add_issue(result, PortableWasmPluginLoadIssueCode::OpenFailed, path, "portable plugin payload could not be opened");
        return bytes;
      }
      bytes.resize(static_cast<std::size_t>(size));
      input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        add_issue(result, PortableWasmPluginLoadIssueCode::OpenFailed, path, "portable plugin payload changed while loading");
        return bytes;
      }
      ok = true;
      return bytes;
    }

    void finish_instantiated_result(PortableWasmPluginLoadResult& result, PortableWasmInstantiationResult instantiated,
                                    const std::filesystem::path& path, std::shared_ptr<WasmHostImports>& host_imports) {
      if (!instantiated.ok()) {
        add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, path,
                  instantiated.error.has_value() ? std::move(*instantiated.error) : "portable WASM backend returned no instance");
        return;
      }
      if (instantiated.instance->host_imports() != host_imports.get()) {
        add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, path,
                  "portable WASM backend returned an instance that does not retain its injected host imports");
        return;
      }

      auto queried = query_portable_wasm_plugin(*instantiated.instance);
      if (!queried.ok()) {
        add_issue(result, PortableWasmPluginLoadIssueCode::QueryFailed, path, "portable WASM plugin descriptor query failed", {},
                  std::move(queried.issues));
        return;
      }

      result.adopt_loaded_plugin(path, std::move(instantiated.instance), std::move(*queried.provider));
    }

    struct ManifestCheck {
      std::optional<modules::ModuleManifest> manifest;
      bool present{};
    };

    ManifestCheck validate_present_manifest(const std::filesystem::path& package, PortableWasmPluginLoadResult& result) {
      const auto manifest_path = package / portable_wasm_plugin_manifest_filename();
      std::error_code error;
      if (!std::filesystem::is_regular_file(manifest_path, error) || error) return {};
      std::ifstream input(manifest_path, std::ios::binary);
      if (!input.is_open()) {
        add_issue(result, PortableWasmPluginLoadIssueCode::ManifestInvalid, manifest_path, "module.manifest could not be opened");
        return {.present = true};
      }
      std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
      auto parsed = modules::parse_module_manifest(source, manifest_path.string());
      if (!parsed.ok()) {
        auto message = std::string{"module.manifest is invalid"};
        if (!parsed.errors.empty()) message += ": " + parsed.errors.front().message;
        add_issue(result, PortableWasmPluginLoadIssueCode::ManifestInvalid, manifest_path, std::move(message));
        return {.present = true};
      }
      const auto& manifest = *parsed.manifest;
      if (manifest.threads == modules::ModuleThreadsPolicy::Managed
          && std::ranges::find(manifest.exports, std::string_view{quiesce_export_symbol}, &modules::ModuleManifestExport::name)
              == manifest.exports.end()) {
        add_issue(result, PortableWasmPluginLoadIssueCode::MissingExport, package,
                  std::string{"module.manifest declares managed threads but does not export "} + std::string{quiesce_export_symbol});
        return {.present = true};
      }
      return {.manifest = std::move(parsed.manifest), .present = true};
    }

  }  // namespace

  PortableWasmPluginLoadResult verify_portable_wasm_module_contract(const std::filesystem::path& package,
                                                                    const PortableWasmModuleContract& contract) {
    PortableWasmPluginLoadResult result;
    const auto check = validate_present_manifest(package, result);
    if (!check.present) {
      if (!contract.signature.empty()) {
        add_issue(result, PortableWasmPluginLoadIssueCode::MissingManifest, package / portable_wasm_plugin_manifest_filename(),
                  "contract lockfile requires a module.manifest in the plugin package");
      }
      return result;
    }
    if (!check.manifest.has_value()) return result;
    const auto& actual = *check.manifest;
    if (actual.api_version != contract.api_version) {
      add_issue(result, PortableWasmPluginLoadIssueCode::ApiVersionMismatch, package,
                "module.manifest api version " + std::to_string(actual.api_version) + " does not match the locked api version "
                    + std::to_string(contract.api_version));
    }
    if (actual.abi_version != contract.abi_version) {
      add_issue(result, PortableWasmPluginLoadIssueCode::AbiVersionMismatch, package,
                "module.manifest abi version " + std::to_string(actual.abi_version) + " does not match the locked abi version "
                    + std::to_string(contract.abi_version));
    }
    if (actual.threads != contract.threads) {
      add_issue(result, PortableWasmPluginLoadIssueCode::ThreadsPolicyMismatch, package,
                std::string{"module.manifest threads policy "} + std::string{modules::module_threads_policy_name(actual.threads)}
                    + " does not match the locked policy " + std::string{modules::module_threads_policy_name(contract.threads)});
    }
    if (actual.shared_memory != contract.shared_memory) {
      add_issue(result, PortableWasmPluginLoadIssueCode::SharedMemoryMismatch, package,
                std::string{"module.manifest shared-memory is "} + (actual.shared_memory ? "true" : "false") + " but the lock says "
                    + (contract.shared_memory ? "true" : "false"));
    }
    if (!contract.signature.empty()) {
      const auto digest = modules::module_manifest_signature(actual);
      if (digest != contract.signature) {
        add_issue(result, PortableWasmPluginLoadIssueCode::SignatureMismatch, package,
                  "module.manifest export-table digest does not match the locked signature");
      }
    }
    return result;
  }

  PortableWasmPluginLoadResult load_portable_wasm_plugin_package(const std::filesystem::path& package, PortableWasmBackend& backend,
                                                                 WasmHostServices host_services) {
    PortableWasmPluginLoadResult result;
    if (package.empty() || package.extension() != ".plugin") {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidPackage, package,
                "portable plugin package must be a directory whose name ends in .plugin");
      return result;
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(package, error).lexically_normal();
    if (error) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidPackage, package, "portable plugin package path could not be resolved", error);
      return result;
    }
    const auto package_status = std::filesystem::symlink_status(absolute, error);
    if (error || !std::filesystem::is_directory(package_status) || std::filesystem::is_symlink(package_status)) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidPackage, absolute,
                "portable plugin package must be a real directory, not a file or symbolic link", error);
      return result;
    }

    const auto binary_filename = portable_wasm_plugin_binary_filename();
    const auto binary = absolute / binary_filename;
    const auto binary_status = std::filesystem::symlink_status(binary, error);
    if (error || !std::filesystem::is_regular_file(binary_status) || std::filesystem::is_symlink(binary_status)) {
      add_issue(result, PortableWasmPluginLoadIssueCode::MissingPackageBinary, binary,
                "portable plugin package does not contain its canonical plugin.wasm binary", error);
      return result;
    }

    /* v2 package whitelist: plugin.wasm (required) plus optional plugin.aot and module.manifest. */
    bool contains_unlisted_entry = false;
    std::filesystem::directory_iterator entry{absolute, error};
    const std::filesystem::directory_iterator end;
    while (!error && entry != end) {
      const auto filename = entry->path().filename();
      contains_unlisted_entry
          = contains_unlisted_entry || (filename != binary_filename && filename != portable_wasm_plugin_aot_filename()
                                        && filename != portable_wasm_plugin_manifest_filename());
      entry.increment(error);
    }
    if (error || contains_unlisted_entry) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidPackage, absolute,
                "portable plugin package may only contain plugin.wasm, plugin.aot, and module.manifest", error);
      return result;
    }

    std::shared_ptr<WasmHostImports> host_imports;
    try {
      host_imports = std::make_shared<WasmHostImports>(host_services);
    } catch (const std::bad_alloc&) {
      add_issue(result, PortableWasmPluginLoadIssueCode::OutOfMemory, absolute, "portable WASM host imports allocation failed");
      return result;
    }

    /* A present manifest is validated eagerly, before any payload is read (todo 11). */
    const auto manifest_check = validate_present_manifest(absolute, result);
    if (manifest_check.present && !manifest_check.manifest.has_value()) return result;

    const auto aot = absolute / portable_wasm_plugin_aot_filename();
    std::error_code aot_error;
    const auto aot_is_regular = std::filesystem::is_regular_file(aot, aot_error) && !aot_error;
    /* Payload selection stays inside the backend (todo 7): the loader only
     * gathers payloads and the manifest toolchain version, then hands both to
     * the backend's optional AOT side interface. Backends that do not
     * implement it (browser backend, test fakes) always use plugin.wasm. */
    auto* aot_aware = aot_is_regular ? dynamic_cast<AotAwarePortableWasmBackend*>(&backend) : nullptr;
    if (aot_aware != nullptr) {
      bool wasm_ok = false;
      const auto wasm_bytes = read_payload_file(binary, result, wasm_ok);
      if (!wasm_ok) return result;
      bool aot_ok = false;
      const auto aot_bytes = read_payload_file(aot, result, aot_ok);
      if (!aot_ok) return result;

      std::string toolchain_version;
      if (manifest_check.manifest.has_value() && manifest_check.manifest->toolchain.has_value()) {
        toolchain_version = manifest_check.manifest->toolchain->version;
      }

      PortableWasmAotSelection selection;
      try {
        selection = aot_aware->instantiate_prefer_aot(wasm_bytes, aot_bytes, toolchain_version, host_imports);
      } catch (const std::bad_alloc&) {
        add_issue(result, PortableWasmPluginLoadIssueCode::OutOfMemory, absolute, "portable WASM backend ran out of memory");
        return result;
      } catch (const std::exception& exception) {
        add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, absolute, std::string{"portable WASM backend threw: "} + exception.what());
        return result;
      } catch (...) {
        add_issue(result, PortableWasmPluginLoadIssueCode::BackendFailure, absolute, "portable WASM backend threw");
        return result;
      }
      if (!selection.result.ok()) {
        auto message = selection.result.error.has_value() ? std::move(*selection.result.error) : std::string{"portable WASM backend returned no instance"};
        if (selection.issue.has_value()) {
          message = "portable plugin package AOT payload rejected (" + std::string{portable_wasm_aot_issue_name(*selection.issue)} + "): " + message;
        }
        add_issue(result, PortableWasmPluginLoadIssueCode::AotRejected, aot, std::move(message), {}, {}, selection.issue);
        return result;
      }
      finish_instantiated_result(result, std::move(selection.result), binary, host_imports);
      return result;
    }

    /* Interpreter path: no plugin.aot payload, or the backend ignores AOT. */
    return load_portable_wasm_plugin_binary(binary, backend, host_services);
  }

}  // namespace mobagen::plugins
