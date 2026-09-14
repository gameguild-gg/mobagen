#include "wasm_plugin_loader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::plugins {
  namespace {

    constexpr std::array wasm_magic{std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}};
    constexpr std::array wasm_version_1{std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

    void add_issue(PortableWasmPluginLoadResult& result, PortableWasmPluginLoadIssueCode code, const std::filesystem::path& path, std::string message,
                   std::error_code system_error = {}, std::vector<WasmPluginQueryIssue> query_issues = {}) {
      result.issues.push_back({code, path, system_error, std::move(message), std::move(query_issues)});
    }

  }  // namespace

  PortableWasmInstantiationResult PortableWasmInstantiationResult::success(std::unique_ptr<PortableWasmInstance> instance) {
    return {std::move(instance), std::nullopt};
  }

  PortableWasmInstantiationResult PortableWasmInstantiationResult::failure(std::string error) { return {nullptr, std::move(error)}; }

  PortableWasmPluginLoadResult load_portable_wasm_plugin_binary(const std::filesystem::path& path, PortableWasmBackend& backend) {
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

    PortableWasmInstantiationResult instantiated;
    try {
      instantiated = backend.instantiate(binary);
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

  PortableWasmPluginLoadResult load_portable_wasm_plugin_package(const std::filesystem::path& package, PortableWasmBackend& backend) {
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

    std::size_t entry_count = 0;
    bool contains_only_binary = true;
    std::filesystem::directory_iterator entry{absolute, error};
    const std::filesystem::directory_iterator end;
    while (!error && entry != end) {
      ++entry_count;
      contains_only_binary = contains_only_binary && entry->path().filename() == binary_filename;
      entry.increment(error);
    }
    if (error || entry_count != 1 || !contains_only_binary) {
      add_issue(result, PortableWasmPluginLoadIssueCode::InvalidPackage, absolute,
                "portable plugin package must contain exactly one canonical plugin.wasm binary", error);
      return result;
    }

    return load_portable_wasm_plugin_binary(binary, backend);
  }

}  // namespace mobagen::plugins
