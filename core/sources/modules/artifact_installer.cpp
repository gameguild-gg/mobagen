#include "artifact_installer.hpp"

#include "assets/asset_id.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <string_view>
#include <utility>

namespace mobagen::modules {
  namespace {

    constexpr std::size_t hash_buffer_size = 64 * 1024;
    constexpr std::size_t unique_path_attempts = 128;
    std::atomic_uint64_t unique_path_sequence{0};

    struct StagedArtifact {
      InstalledModuleArtifact artifact;
      std::filesystem::path staging;
      std::filesystem::path backup;
      bool destination_exists{};
      bool backup_moved{};
      bool committed{};
    };

    ArtifactInstallResult failure(ArtifactInstallIssueCode code, std::string provider_id,
                                  std::filesystem::path path, std::string message,
                                  std::error_code system_error = {}) {
      ArtifactInstallResult result;
      result.issues.push_back({code, std::move(provider_id), std::move(path), system_error,
                               std::move(message)});
      return result;
    }

    bool verify_file(const std::filesystem::path& path, const assets::AssetId& expected_id,
                     std::uint64_t expected_size, std::error_code& error) {
      const auto status = std::filesystem::symlink_status(path, error);
      if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        return false;
      }
      const auto size = std::filesystem::file_size(path, error);
      if (error || size != expected_size) return false;

      std::ifstream stream(path, std::ios::binary);
      if (!stream.good()) {
        error = std::make_error_code(std::errc::io_error);
        return false;
      }
      assets::Sha256Hasher hasher;
      std::array<std::byte, hash_buffer_size> buffer{};
      std::uint64_t total = 0;
      while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto read = stream.gcount();
        if (read > 0) {
          const auto count = static_cast<std::size_t>(read);
          total += count;
          if (total > expected_size || !hasher.update(std::span<const std::byte>{buffer.data(), count})) {
            return false;
          }
        }
      }
      if (stream.bad() || total != expected_size) {
        error = stream.bad() ? std::make_error_code(std::errc::io_error) : std::error_code{};
        return false;
      }
      const auto actual_id = hasher.finish();
      return actual_id.has_value() && *actual_id == expected_id;
    }

    bool package_matches(const StagedArtifact& staged, std::error_code& error) {
      const auto& package = staged.artifact.package_path;
      const auto status = std::filesystem::symlink_status(package, error);
      if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        return false;
      }
      std::filesystem::directory_iterator iterator{package, error};
      const std::filesystem::directory_iterator end;
      if (error || iterator == end || iterator->path() != staged.artifact.binary_path) return false;
      iterator.increment(error);
      if (error || iterator != end) return false;
      return verify_file(staged.artifact.binary_path, staged.artifact.id, staged.artifact.size, error);
    }

    std::filesystem::path create_staging_directory(const std::filesystem::path& root,
                                                   std::string_view provider_id,
                                                   std::error_code& error) {
      const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
      for (std::size_t attempt = 0; attempt < unique_path_attempts; ++attempt) {
        auto path = root / ("." + std::string{provider_id} + ".plugin.tmp-"
                            + std::to_string(nonce) + '-'
                            + std::to_string(unique_path_sequence.fetch_add(1, std::memory_order_relaxed)));
        if (std::filesystem::create_directory(path, error)) return path;
        if (error) return {};
      }
      error = std::make_error_code(std::errc::file_exists);
      return {};
    }

    std::filesystem::path available_backup_path(const std::filesystem::path& root,
                                                std::string_view provider_id,
                                                std::error_code& error) {
      const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
      for (std::size_t attempt = 0; attempt < unique_path_attempts; ++attempt) {
        auto path = root / ("." + std::string{provider_id} + ".plugin.bak-"
                            + std::to_string(nonce) + '-'
                            + std::to_string(unique_path_sequence.fetch_add(1, std::memory_order_relaxed)));
        const auto exists = std::filesystem::exists(path, error);
        if (error) return {};
        if (!exists) return path;
      }
      error = std::make_error_code(std::errc::file_exists);
      return {};
    }

    void remove_path(const std::filesystem::path& path) noexcept {
      if (path.empty()) return;
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }

    void cleanup_staging(std::span<const StagedArtifact> staged) noexcept {
      for (const auto& entry : staged) remove_path(entry.staging);
    }

    bool rollback(std::span<StagedArtifact> staged) noexcept {
      bool restored = true;
      for (auto iterator = staged.rbegin(); iterator != staged.rend(); ++iterator) {
        auto& entry = *iterator;
        if (entry.committed) remove_path(entry.artifact.package_path);
        if (entry.backup_moved) {
          std::error_code error;
          std::filesystem::rename(entry.backup, entry.artifact.package_path, error);
          restored = restored && !error;
        }
      }
      cleanup_staging(staged);
      return restored;
    }

  }  // namespace

  std::filesystem::path module_plugin_binary_filename(LinkageMode linkage) {
    if (linkage == LinkageMode::Wasm) return "plugin.wasm";
    if (linkage != LinkageMode::Dynamic) return {};
#ifdef _WIN32
    return "plugin.dll";
#elif defined(__APPLE__)
    return "plugin.dylib";
#else
    return "plugin.so";
#endif
  }

  ArtifactInstallResult materialize_module_plugins(std::span<const CachedModuleArtifact> artifacts,
                                                   const std::filesystem::path& install_root) {
    if (install_root.empty()) {
      return failure(ArtifactInstallIssueCode::InvalidRoot, {}, install_root,
                     "plugin install root must not be empty");
    }

    std::error_code error;
    const auto root = std::filesystem::absolute(install_root, error).lexically_normal();
    if (error) {
      return failure(ArtifactInstallIssueCode::InvalidRoot, {}, install_root,
                     "plugin install root could not be resolved", error);
    }

    std::set<std::string, std::less<>> providers;
    for (const auto& artifact : artifacts) {
      if (!is_provider_id(artifact.provider_id) || artifact.size == 0 || artifact.cache_path.empty()
          || !providers.insert(artifact.provider_id).second) {
        return failure(ArtifactInstallIssueCode::InvalidArtifact, artifact.provider_id,
                       artifact.cache_path, "cached plugin artifact metadata is invalid or duplicated");
      }
      if (module_plugin_binary_filename(artifact.linkage).empty()) {
        return failure(ArtifactInstallIssueCode::UnsupportedLinkage, artifact.provider_id,
                       artifact.cache_path,
                       "only dynamic and WASM artifacts can become runtime .plugin packages");
      }
      error.clear();
      if (!verify_file(artifact.cache_path, artifact.id, artifact.size, error)) {
        return failure(ArtifactInstallIssueCode::SourceInvalid, artifact.provider_id,
                       artifact.cache_path, "cached plugin artifact failed size or SHA-256 verification",
                       error);
      }
    }
    if (artifacts.empty()) return {};

    const auto root_exists = std::filesystem::exists(root, error);
    if (error) {
      return failure(ArtifactInstallIssueCode::InvalidRoot, {}, root,
                     "plugin install root could not be inspected", error);
    }
    if (root_exists) {
      const auto status = std::filesystem::symlink_status(root, error);
      if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        return failure(ArtifactInstallIssueCode::InvalidRoot, {}, root,
                       "plugin install root must be a real directory, not a file or symbolic link",
                       error);
      }
    } else {
      std::filesystem::create_directories(root, error);
      if (error) {
        return failure(ArtifactInstallIssueCode::InvalidRoot, {}, root,
                       "plugin install root could not be created", error);
      }
    }

    std::vector<StagedArtifact> staged;
    staged.reserve(artifacts.size());
    for (const auto& source : artifacts) {
      const auto binary_name = module_plugin_binary_filename(source.linkage);
      auto staging = create_staging_directory(root, source.provider_id, error);
      if (error || staging.empty()) {
        cleanup_staging(staged);
        return failure(ArtifactInstallIssueCode::StageFailed, source.provider_id, root,
                       "could not create a private staging package", error);
      }
      const auto destination = root / (source.provider_id + ".plugin");
      const auto staging_binary = staging / binary_name;
      std::filesystem::copy_file(source.cache_path, staging_binary,
                                 std::filesystem::copy_options::none, error);
      if (error || !verify_file(staging_binary, source.id, source.size, error)) {
        remove_path(staging);
        cleanup_staging(staged);
        return failure(ArtifactInstallIssueCode::StageFailed, source.provider_id, staging_binary,
                       "staged plugin artifact failed copy or integrity verification", error);
      }
      staged.push_back({
          .artifact = {
              .provider_id = source.provider_id,
              .version = source.version,
              .id = source.id,
              .size = source.size,
              .package_path = destination,
              .binary_path = destination / binary_name,
          },
          .staging = std::move(staging),
      });
    }

    for (auto& entry : staged) {
      error.clear();
      entry.destination_exists = std::filesystem::exists(entry.artifact.package_path, error);
      if (error) {
        cleanup_staging(staged);
        return failure(ArtifactInstallIssueCode::CommitFailed, entry.artifact.provider_id,
                       entry.artifact.package_path, "installed plugin package could not be inspected",
                       error);
      }
      if (entry.destination_exists && package_matches(entry, error)) {
        remove_path(entry.staging);
        entry.staging.clear();
        continue;
      }
      if (entry.destination_exists) {
        error.clear();
        entry.backup = available_backup_path(root, entry.artifact.provider_id, error);
        if (error || entry.backup.empty()) {
          cleanup_staging(staged);
          return failure(ArtifactInstallIssueCode::CommitFailed, entry.artifact.provider_id,
                         entry.artifact.package_path, "could not reserve plugin package backup path",
                         error);
        }
      }
    }

    for (auto& entry : staged) {
      if (entry.staging.empty()) continue;
      if (entry.destination_exists) {
        std::filesystem::rename(entry.artifact.package_path, entry.backup, error);
        if (error) {
          const auto restored = rollback(staged);
          return failure(restored ? ArtifactInstallIssueCode::CommitFailed
                                  : ArtifactInstallIssueCode::RollbackFailed,
                         entry.artifact.provider_id, entry.artifact.package_path,
                         restored ? "could not move the active plugin package into backup"
                                  : "plugin package commit failed and rollback was incomplete",
                         error);
        }
        entry.backup_moved = true;
      }
      std::filesystem::rename(entry.staging, entry.artifact.package_path, error);
      if (error) {
        const auto restored = rollback(staged);
        return failure(restored ? ArtifactInstallIssueCode::CommitFailed
                                : ArtifactInstallIssueCode::RollbackFailed,
                       entry.artifact.provider_id, entry.artifact.package_path,
                       restored ? "could not commit the staged plugin package"
                                : "plugin package commit failed and rollback was incomplete",
                       error);
      }
      entry.staging.clear();
      entry.committed = true;
      entry.artifact.installed = true;
    }

    for (const auto& entry : staged) {
      if (!entry.backup_moved) continue;
      std::filesystem::remove_all(entry.backup, error);
      if (error) {
        return failure(ArtifactInstallIssueCode::CleanupFailed, entry.artifact.provider_id,
                       entry.backup, "updated plugin package backup could not be removed", error);
      }
    }

    ArtifactInstallResult result;
    result.artifacts.reserve(staged.size());
    for (auto& entry : staged) result.artifacts.push_back(std::move(entry.artifact));
    return result;
  }

}  // namespace mobagen::modules
