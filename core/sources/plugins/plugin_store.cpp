#include "plugin_store.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::plugins {
  namespace {

    enum class StoreRootMode : std::uint8_t { Create, Existing };

    template <typename Result> void add_issue(Result& result, NativePluginStoreIssueCode code, const std::filesystem::path& path, std::string message,
                                              std::error_code system_error = {}, std::vector<NativePluginLoadIssue> load_issues = {}) {
      result.issues.push_back({code, path, system_error, std::move(message), std::move(load_issues)});
    }

    template <typename Result>
    [[nodiscard]] bool resolve_store_root(const std::filesystem::path& configured, StoreRootMode mode, std::filesystem::path& root, Result& result) {
      if (configured.empty()) {
        add_issue(result, NativePluginStoreIssueCode::InvalidRoot, configured, "plugin store root must name a directory");
        return false;
      }

      std::error_code error;
      root = std::filesystem::absolute(configured, error).lexically_normal();
      if (error) {
        add_issue(result, NativePluginStoreIssueCode::InvalidRoot, configured, "could not resolve plugin store root", error);
        return false;
      }

      auto status = std::filesystem::symlink_status(root, error);
      if (error && status.type() != std::filesystem::file_type::not_found) {
        add_issue(result, NativePluginStoreIssueCode::InvalidRoot, root, "could not inspect plugin store root", error);
        return false;
      }
      error.clear();

      if (status.type() == std::filesystem::file_type::not_found) {
        if (mode == StoreRootMode::Existing) {
          add_issue(result, NativePluginStoreIssueCode::NotFound, root, "plugin store does not exist");
          return false;
        }
        std::filesystem::create_directories(root, error);
        if (error) {
          add_issue(result, NativePluginStoreIssueCode::InvalidRoot, root, "could not create plugin store root", error);
          return false;
        }
        status = std::filesystem::symlink_status(root, error);
      }

      if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        add_issue(result, NativePluginStoreIssueCode::InvalidRoot, root, "plugin store root must be a real directory, not a file or symbolic link",
                  error);
        return false;
      }
      return true;
    }

    [[nodiscard]] std::filesystem::path unique_transaction_path(const std::filesystem::path& root, std::string_view provider_id,
                                                                std::string_view operation) {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      return root
             / ("." + std::string{provider_id} + "." + std::string{operation} + "-" + std::to_string(ticks) + "-"
                + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".plugin");
    }

    void cleanup_directory(const std::filesystem::path& path, NativePluginStoreActionResult& result) {
      std::error_code error;
      std::filesystem::remove_all(path, error);
      if (error) {
        add_issue(result, NativePluginStoreIssueCode::CleanupDeferred, path, "transaction residue could not be removed", error);
      }
    }

    [[nodiscard]] bool inspect_destination(const std::filesystem::path& destination, bool& exists, NativePluginStoreActionResult& result) {
      std::error_code error;
      const auto status = std::filesystem::symlink_status(destination, error);
      if (error && status.type() != std::filesystem::file_type::not_found) {
        add_issue(result, NativePluginStoreIssueCode::CommitFailed, destination, "could not inspect installed plugin package", error);
        return false;
      }
      exists = status.type() != std::filesystem::file_type::not_found;
      if (!exists) {
        return true;
      }
      if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        add_issue(result, NativePluginStoreIssueCode::CommitFailed, destination,
                  "installed plugin package must be a real directory, not a file or symbolic link");
        return false;
      }
      return true;
    }

  }  // namespace

  NativePluginStoreActionResult NativePluginStore::install(const std::filesystem::path& source, PluginHost& host) const {
    NativePluginStoreActionResult result;
    auto source_plugin = load_native_plugin_package(source, host.api());
    if (!source_plugin.plugin.has_value()) {
      add_issue(result, NativePluginStoreIssueCode::InvalidSource, source, "source is not a valid native plugin package", {},
                std::move(source_plugin.issues));
      return result;
    }

    result.provider_id = source_plugin.plugin->contract().provider.id;
    result.version = source_plugin.plugin->contract().provider.version;
    const auto source_binary = source_plugin.plugin->path();
    source_plugin.plugin.reset();

    std::filesystem::path root;
    if (!resolve_store_root(root_, StoreRootMode::Create, root, result)) {
      return result;
    }

    result.package = root / (result.provider_id + ".plugin");
    bool destination_exists = false;
    if (!inspect_destination(result.package, destination_exists, result)) {
      return result;
    }

    const auto staging = unique_transaction_path(root, result.provider_id, "install");
    std::error_code error;
    if (!std::filesystem::create_directory(staging, error) || error) {
      add_issue(result, NativePluginStoreIssueCode::StageFailed, staging, "could not create plugin staging directory", error);
      return result;
    }

    const auto staged_binary = staging / native_plugin_binary_filename();
    if (!std::filesystem::copy_file(source_binary, staged_binary, std::filesystem::copy_options::none, error) || error) {
      add_issue(result, NativePluginStoreIssueCode::StageFailed, staged_binary, "could not copy plugin binary into staging", error);
      cleanup_directory(staging, result);
      return result;
    }

    auto staged_plugin = load_native_plugin_package(staging, host.api());
    if (!staged_plugin.plugin.has_value()) {
      add_issue(result, NativePluginStoreIssueCode::StageFailed, staging, "staged plugin package failed validation", {},
                std::move(staged_plugin.issues));
      cleanup_directory(staging, result);
      return result;
    }
    const auto& staged_provider = staged_plugin.plugin->contract().provider;
    if (staged_provider.id != result.provider_id || staged_provider.version != result.version) {
      add_issue(result, NativePluginStoreIssueCode::StageFailed, staging, "staged plugin identity changed while it was copied");
      staged_plugin.plugin.reset();
      cleanup_directory(staging, result);
      return result;
    }
    staged_plugin.plugin.reset();

    std::filesystem::path backup;
    if (destination_exists) {
      backup = unique_transaction_path(root, result.provider_id, "backup");
      std::filesystem::rename(result.package, backup, error);
      if (error) {
        add_issue(result, NativePluginStoreIssueCode::CommitFailed, result.package, "could not move installed plugin package into backup", error);
        cleanup_directory(staging, result);
        return result;
      }
    }

    error.clear();
    std::filesystem::rename(staging, result.package, error);
    if (error) {
      add_issue(result, NativePluginStoreIssueCode::CommitFailed, result.package, "could not commit staged plugin package", error);
      if (destination_exists) {
        std::error_code rollback_error;
        std::filesystem::rename(backup, result.package, rollback_error);
        if (rollback_error) {
          add_issue(result, NativePluginStoreIssueCode::RollbackFailed, backup, "could not restore the previous installed plugin package",
                    rollback_error);
        }
      }
      cleanup_directory(staging, result);
      return result;
    }

    result.changed = true;
    result.committed = true;
    if (destination_exists) {
      cleanup_directory(backup, result);
    }
    return result;
  }

  NativePluginStoreActionResult NativePluginStore::remove(std::string_view provider_id) const {
    NativePluginStoreActionResult result;
    result.provider_id = std::string{provider_id};
    if (!modules::is_provider_id(provider_id)) {
      add_issue(result, NativePluginStoreIssueCode::InvalidProvider, {}, "plugin provider ID is invalid");
      return result;
    }

    std::filesystem::path root;
    if (!resolve_store_root(root_, StoreRootMode::Existing, root, result)) {
      return result;
    }

    result.package = root / (result.provider_id + ".plugin");
    bool destination_exists = false;
    if (!inspect_destination(result.package, destination_exists, result)) {
      return result;
    }
    if (!destination_exists) {
      add_issue(result, NativePluginStoreIssueCode::NotFound, result.package, "installed plugin package does not exist");
      return result;
    }

    const auto tombstone = unique_transaction_path(root, result.provider_id, "remove");
    std::error_code error;
    std::filesystem::rename(result.package, tombstone, error);
    if (error) {
      add_issue(result, NativePluginStoreIssueCode::CommitFailed, result.package, "could not remove plugin package from its active location", error);
      return result;
    }

    result.changed = true;
    result.committed = true;
    cleanup_directory(tombstone, result);
    return result;
  }

  NativePluginStoreListResult NativePluginStore::list(PluginHost& host) const {
    NativePluginStoreListResult result;
    std::filesystem::path root;
    if (!resolve_store_root(root_, StoreRootMode::Existing, root, result)) {
      return result;
    }

    std::error_code error;
    std::filesystem::directory_iterator iterator(root, error);
    const std::filesystem::directory_iterator end;
    if (error) {
      add_issue(result, NativePluginStoreIssueCode::EnumerationFailed, root, "could not enumerate plugin store", error);
      return result;
    }

    std::vector<std::filesystem::path> packages;
    while (iterator != end) {
      const auto filename = iterator->path().filename().string();
      if (!filename.starts_with('.') && iterator->path().extension() == ".plugin") {
        if (packages.size() == max_native_plugin_store_packages) {
          add_issue(result, NativePluginStoreIssueCode::LimitExceeded, root, "installed plugin package count exceeds limit");
          return result;
        }
        packages.push_back(iterator->path());
      }
      iterator.increment(error);
      if (error) {
        add_issue(result, NativePluginStoreIssueCode::EnumerationFailed, root, "could not continue enumerating plugin store", error);
        return result;
      }
    }
    std::ranges::sort(packages, [](const auto& left, const auto& right) { return left.generic_string() < right.generic_string(); });

    for (const auto& package : packages) {
      auto loaded = load_native_plugin_package(package, host.api());
      if (!loaded.plugin.has_value()) {
        add_issue(result, NativePluginStoreIssueCode::InvalidSource, package, "installed plugin package is invalid", {}, std::move(loaded.issues));
        continue;
      }
      const auto& provider = loaded.plugin->contract().provider;
      if (package.filename() != std::filesystem::path{provider.id + ".plugin"}) {
        add_issue(result, NativePluginStoreIssueCode::InvalidProvider, package, "installed plugin package filename does not match its provider ID");
        continue;
      }
      result.entries.push_back({provider.id, provider.version, package});
    }
    return result;
  }

}  // namespace mobagen::plugins
