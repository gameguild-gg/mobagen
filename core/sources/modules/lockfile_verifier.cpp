#include "lockfile_verifier.hpp"

#include "artifact_fetcher.hpp"
#include "artifact_installer.hpp"
#include "assets/asset_id.hpp"
#include "catalog.hpp"
#include "plugins/plugin_package.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace mobagen::modules {
  namespace {

    constexpr std::size_t hash_buffer_size = 64 * 1024;

    LockfileInspectionResult inspection_failure(LockfileVerificationIssueCode code, std::string provider_id, std::filesystem::path path,
                                                std::string message, std::error_code system_error = {}) {
      LockfileInspectionResult result;
      result.issues.push_back({code, std::move(provider_id), std::move(path), system_error, std::move(message)});
      return result;
    }

    bool is_portable_package_path(std::string_view value) {
      if (value.empty() || value.contains('\\')) return false;
      const std::filesystem::path path{value};
      if (path.is_absolute() || path.has_root_path() || path.extension() != ".plugin" || path.lexically_normal().generic_string() != value) {
        return false;
      }
      for (const auto& component : path) {
        const auto text = component.generic_string();
        if (text.empty() || text == "." || text == ".."
            || !std::ranges::all_of(text, [](unsigned char value_char) { return value_char >= 0x20U && value_char != 0x7fU; })) {
          return false;
        }
      }
      return true;
    }

    std::optional<LinkageMode> locked_linkage(const LockfileDocument& document, const PluginLockEntry& plugin) {
      std::optional<LinkageMode> linkage;
      for (const auto& selection : document.resolved) {
        if (selection.provider != plugin.provider) continue;
        if (selection.version != plugin.version || (selection.linkage != LinkageMode::Dynamic && selection.linkage != LinkageMode::Wasm)
            || (linkage.has_value() && *linkage != selection.linkage)) {
          return std::nullopt;
        }
        linkage = selection.linkage;
      }
      return linkage;
    }

    std::optional<LockfileVerificationIssue> inspect_path_components(const std::filesystem::path& root, const PluginLockEntry& plugin,
                                                                     std::filesystem::path& package) {
      if (!is_portable_package_path(plugin.package)) {
        return LockfileVerificationIssue{LockfileVerificationIssueCode::InvalidPackagePath,
                                         plugin.provider,
                                         {},
                                         {},
                                         "locked plugin package is not a portable relative .plugin path"};
      }
      package = (root / std::filesystem::path{plugin.package}).lexically_normal();
      auto current = root;
      for (const auto& component : std::filesystem::path{plugin.package}) {
        current /= component;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error) {
          return LockfileVerificationIssue{LockfileVerificationIssueCode::ReadFailed, plugin.provider, current, error,
                                           "locked plugin package path could not be inspected"};
        }
        if (!std::filesystem::exists(status)) {
          return LockfileVerificationIssue{
              LockfileVerificationIssueCode::MissingPackage, plugin.provider, current, {}, "locked plugin package is missing"};
        }
        if (std::filesystem::is_symlink(status) || (current != package && !std::filesystem::is_directory(status))) {
          return LockfileVerificationIssue{LockfileVerificationIssueCode::InvalidPackagePath,
                                           plugin.provider,
                                           current,
                                           {},
                                           "locked plugin package path must contain only real directories"};
        }
      }
      return std::nullopt;
    }

    std::optional<LockfileVerificationIssue> verify_binary(const PluginLockEntry& plugin, const std::filesystem::path& binary,
                                                           std::uint64_t& verified_size) {
      const auto expected = assets::parse_asset_id(plugin.hash);
      if (!expected.has_value()) {
        return LockfileVerificationIssue{LockfileVerificationIssueCode::HashMismatch, plugin.provider, binary, {}, "locked plugin hash is invalid"};
      }

      std::error_code error;
      const auto size = std::filesystem::file_size(binary, error);
      if (error) {
        return LockfileVerificationIssue{LockfileVerificationIssueCode::ReadFailed, plugin.provider, binary, error,
                                         "locked plugin binary size could not be read"};
      }
      if (size == 0 || size > max_module_artifact_bytes) {
        return LockfileVerificationIssue{LockfileVerificationIssueCode::PackageTooLarge,
                                         plugin.provider,
                                         binary,
                                         {},
                                         "locked plugin binary is empty or exceeds the artifact size limit"};
      }

      std::ifstream stream(binary, std::ios::binary);
      if (!stream.good()) {
        return LockfileVerificationIssue{LockfileVerificationIssueCode::ReadFailed, plugin.provider, binary,
                                         std::make_error_code(std::errc::io_error), "locked plugin binary could not be opened"};
      }
      assets::Sha256Hasher hasher;
      std::array<std::byte, hash_buffer_size> buffer{};
      std::uint64_t total = 0;
      while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(stream.gcount());
        if (count == 0) continue;
        total += count;
        if (total > max_module_artifact_bytes || !hasher.update(std::span<const std::byte>{buffer.data(), count})) {
          return LockfileVerificationIssue{LockfileVerificationIssueCode::PackageTooLarge,
                                           plugin.provider,
                                           binary,
                                           {},
                                           "locked plugin binary changed or exceeded the artifact size limit while reading"};
        }
      }
      if (stream.bad()) {
        return LockfileVerificationIssue{LockfileVerificationIssueCode::ReadFailed, plugin.provider, binary,
                                         std::make_error_code(std::errc::io_error), "locked plugin binary could not be read completely"};
      }
      if (total != size) {
        return LockfileVerificationIssue{
            LockfileVerificationIssueCode::Changed, plugin.provider, binary, {}, "locked plugin binary changed while it was being verified"};
      }
      const auto actual = hasher.finish();
      if (!actual.has_value() || *actual != *expected) {
        return LockfileVerificationIssue{
            LockfileVerificationIssueCode::HashMismatch, plugin.provider, binary, {}, "locked plugin binary does not match its SHA-256"};
      }
      verified_size = size;
      return std::nullopt;
    }

  }  // namespace

  LockfileInspectionResult inspect_locked_project(const LockfileDocument& document, const std::filesystem::path& project_root,
                                                  const LockfileVerificationContext& context) {
    if (document.metadata.schema != lockfile_schema_version || document.metadata.sdk != context.sdk || document.metadata.target != context.target
        || document.metadata.profile != context.profile || document.metadata.manifest_hash != context.manifest_hash) {
      return inspection_failure(LockfileVerificationIssueCode::MetadataMismatch, {}, {},
                                "mobagen.lock metadata does not match the requested project runtime");
    }

    std::error_code error;
    const auto root = std::filesystem::absolute(project_root, error).lexically_normal();
    if (error) {
      return inspection_failure(LockfileVerificationIssueCode::InvalidRoot, {}, project_root, "project root could not be resolved", error);
    }
    const auto root_status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(root_status) || std::filesystem::is_symlink(root_status)) {
      return inspection_failure(LockfileVerificationIssueCode::InvalidRoot, {}, root, "project root must be a real directory", error);
    }

    std::set<std::string, std::less<>> providers;
    std::vector<StagedLockedPlugin> staged;
    staged.reserve(document.metadata.plugins.size());
    for (const auto& plugin : document.metadata.plugins) {
      if (!providers.insert(plugin.provider).second) {
        return inspection_failure(LockfileVerificationIssueCode::InvalidResolution, plugin.provider, {},
                                  "mobagen.lock contains a duplicate plugin provider");
      }
      const auto linkage = locked_linkage(document, plugin);
      if (!linkage.has_value()) {
        return inspection_failure(LockfileVerificationIssueCode::InvalidResolution, plugin.provider, {},
                                  "locked plugin is not selected with one matching runtime linkage and version");
      }
      if (plugin.abi_version != runtime_plugin_abi_version(*linkage)) {
        return inspection_failure(LockfileVerificationIssueCode::UnsupportedAbi, plugin.provider, {},
                                  "locked plugin ABI is not supported by this runtime");
      }

      std::filesystem::path package;
      if (auto issue = inspect_path_components(root, plugin, package); issue.has_value()) {
        LockfileInspectionResult result;
        result.issues.push_back(std::move(*issue));
        return result;
      }
      const auto inspected = plugins::inspect_plugin_package(package);
      if (!inspected.ok()) {
        return inspection_failure(LockfileVerificationIssueCode::InvalidPackage, plugin.provider, package,
                                  inspected.issue.has_value() ? inspected.issue->message : "locked plugin package is invalid",
                                  inspected.issue.has_value() ? inspected.issue->system_error : std::error_code{});
      }
      const auto expected_kind = *linkage == LinkageMode::Wasm ? plugins::PluginPackageKind::PortableWasm : plugins::PluginPackageKind::Native;
      if (*inspected.kind != expected_kind) {
        return inspection_failure(LockfileVerificationIssueCode::InvalidPackage, plugin.provider, package,
                                  "locked plugin package kind does not match its resolved linkage");
      }

      const auto binary = package / module_plugin_binary_filename(*linkage);
      staged.push_back({
          .provider_id = plugin.provider,
          .version = plugin.version,
          .linkage = *linkage,
          .abi_version = plugin.abi_version,
          .expected_hash = plugin.hash,
          .package_path = std::move(package),
          .binary_path = binary,
      });
    }

    return {.plugins = std::move(staged)};
  }

  LockfileVerificationResult verify_locked_project(const LockfileDocument& document, const std::filesystem::path& project_root,
                                                   const LockfileVerificationContext& context) {
    auto inspected = inspect_locked_project(document, project_root, context);
    if (!inspected.ok()) {
      return {.issues = std::move(inspected.issues)};
    }

    std::vector<VerifiedLockedPlugin> verified;
    verified.reserve(inspected.plugins.size());
    for (auto& plugin : inspected.plugins) {
      const PluginLockEntry expected{
          .provider = plugin.provider_id,
          .hash = plugin.expected_hash,
      };
      std::uint64_t size = 0;
      if (auto issue = verify_binary(expected, plugin.binary_path, size); issue.has_value()) {
        LockfileVerificationResult result;
        result.issues.push_back(std::move(*issue));
        return result;
      }
      verified.push_back({
          .provider_id = std::move(plugin.provider_id),
          .version = plugin.version,
          .linkage = plugin.linkage,
          .abi_version = plugin.abi_version,
          .size = size,
          .package_path = std::move(plugin.package_path),
          .binary_path = std::move(plugin.binary_path),
      });
    }
    return {.plugins = std::move(verified)};
  }

}  // namespace mobagen::modules
