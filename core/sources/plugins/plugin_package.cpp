#include "plugin_package.hpp"

#include "plugin_loader.hpp"
#include "wasm_plugin_loader.hpp"

#include <utility>

namespace mobagen::plugins {
  namespace {

    PluginPackageInspectionResult failure(PluginPackageInspectionIssueCode code, std::filesystem::path path, std::string message,
                                          std::error_code system_error = {}) {
      return {.issue = PluginPackageInspectionIssue{code, std::move(path), system_error, std::move(message)}};
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
      return failure(PluginPackageInspectionIssueCode::InvalidContents, absolute, "plugin package must contain one canonical plugin binary");
    }
    const auto binary = iterator->path();
    iterator.increment(error);
    if (error) {
      return failure(PluginPackageInspectionIssueCode::InspectionFailure, absolute, "plugin package enumeration failed", error);
    }
    if (iterator != end) {
      return failure(PluginPackageInspectionIssueCode::InvalidContents, absolute, "plugin package must contain exactly one canonical plugin binary");
    }

    const auto binary_status = std::filesystem::symlink_status(binary, error);
    if (error) {
      return failure(PluginPackageInspectionIssueCode::InspectionFailure, binary, "plugin package binary could not be inspected", error);
    }
    if (!std::filesystem::is_regular_file(binary_status) || std::filesystem::is_symlink(binary_status)) {
      return failure(PluginPackageInspectionIssueCode::InvalidContents, binary,
                     "plugin package binary must be a real regular file, not a symbolic link");
    }

    if (binary.filename() == portable_wasm_plugin_binary_filename()) {
      return {.kind = PluginPackageKind::PortableWasm};
    }
    if (binary.filename() == native_plugin_binary_filename()) {
      return {.kind = PluginPackageKind::Native};
    }
    return failure(PluginPackageInspectionIssueCode::InvalidContents, binary,
                   "plugin package does not contain a canonical native or portable WASM binary");
  }

}  // namespace mobagen::plugins
