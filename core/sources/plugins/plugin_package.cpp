#include "plugin_package.hpp"

#include "plugin_loader.hpp"
#include "wasm_plugin_loader.hpp"

#include <array>
#include <utility>

namespace mobagen::plugins {
  namespace {

    PluginPackageInspectionResult failure(PluginPackageInspectionIssueCode code, std::filesystem::path path, std::string message,
                                          std::error_code system_error = {}) {
      return {.issue = PluginPackageInspectionIssue{code, std::move(path), system_error, std::move(message)}};
    }

    /* True when `filename` is one of the strict package contents: the plugin
     * binary plus the optional v2 payloads (todo 21 packaging). */
    bool is_portable_wasm_package_entry(const std::filesystem::path& filename) {
      return filename == portable_wasm_plugin_binary_filename() || filename == portable_wasm_plugin_aot_filename()
             || filename == portable_wasm_plugin_manifest_filename();
    }

  }  // namespace

  PluginPackageInspectionResult inspect_plugin_package(const std::filesystem::path& package) {
    if (package.empty() || package.extension() != ".plugin") {
      return failure(PluginPackageInspectionIssueCode::InvalidPath, package, "plugin package must be a directory whose name ends in .plugin");
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(package, error).lexically_normal();
    if (error) {
      return failure(PluginPackageInspectionIssueCode::InspectionFailure, package, "plugin package path could not be resolved", error);
    }
    const auto package_status = std::filesystem::symlink_status(absolute, error);
    if (error) {
      return failure(PluginPackageInspectionIssueCode::InspectionFailure, absolute, "plugin package could not be inspected", error);
    }
    if (!std::filesystem::is_directory(package_status) || std::filesystem::is_symlink(package_status)) {
      return failure(PluginPackageInspectionIssueCode::InvalidPath, absolute, "plugin package must be a real directory, not a file or symbolic link");
    }

    std::filesystem::directory_iterator iterator{absolute, error};
    const std::filesystem::directory_iterator end;
    if (error) {
      return failure(PluginPackageInspectionIssueCode::InspectionFailure, absolute, "plugin package contents could not be enumerated", error);
    }
    if (iterator == end) {
      return failure(PluginPackageInspectionIssueCode::InvalidContents, absolute, "plugin package must contain a canonical plugin binary");
    }

    /* v2 (todo 21): a portable WASM package is plugin.wasm (required) plus
     * the optional plugin.aot and module.manifest payloads — the same
     * whitelist the package loader enforces. Native packages stay
     * single-binary (the native tier narrows; todo 24 removes it). */
    bool has_wasm = false;
    std::optional<std::filesystem::path> native_binary;
    for (; iterator != end; iterator.increment(error)) {
      if (error) {
        return failure(PluginPackageInspectionIssueCode::InspectionFailure, absolute, "plugin package enumeration failed", error);
      }
      const auto filename = iterator->path().filename();
      if (is_portable_wasm_package_entry(filename)) {
        const auto status = std::filesystem::symlink_status(iterator->path(), error);
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
          return failure(PluginPackageInspectionIssueCode::InvalidContents, iterator->path(),
                         "plugin package payload must be a real regular file, not a symbolic link", error);
        }
        has_wasm = has_wasm || filename == portable_wasm_plugin_binary_filename();
        continue;
      }
      if (filename == native_plugin_binary_filename()) {
        const auto status = std::filesystem::symlink_status(iterator->path(), error);
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
          return failure(PluginPackageInspectionIssueCode::InvalidContents, iterator->path(),
                         "plugin package binary must be a real regular file, not a symbolic link", error);
        }
        native_binary = iterator->path();
        continue;
      }
      return failure(PluginPackageInspectionIssueCode::InvalidContents, iterator->path(),
                     "plugin package contains a file outside the canonical package contents");
    }

    if (has_wasm) return {.kind = PluginPackageKind::PortableWasm};
    if (native_binary.has_value()) return {.kind = PluginPackageKind::Native};
    return failure(PluginPackageInspectionIssueCode::InvalidContents, absolute,
                   "plugin package does not contain a canonical native or portable WASM binary");
  }

}  // namespace mobagen::plugins
