#include "project_runtime.hpp"

#include "assets/asset_id.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace mobagen::compositions {
  namespace {

    constexpr std::uintmax_t max_locked_plugin_binary_bytes = std::uintmax_t{1} << 30U;

    struct ManifestReadResult {
      std::optional<std::string> contents;
      std::filesystem::path absolute_path;
      std::string error;
    };

    struct PluginHashResult {
      std::optional<std::string> hash;
      std::string error;
    };

    ManifestReadResult read_manifest(const std::filesystem::path& path) {
      ManifestReadResult result;
      std::error_code error;
      result.absolute_path = std::filesystem::absolute(path, error);
      if (error) {
        result.error = "mobagen.yaml path could not be resolved";
        return result;
      }
      const auto status = std::filesystem::symlink_status(result.absolute_path, error);
      if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        result.error = "mobagen.yaml must be a readable regular file, not a symbolic link";
        return result;
      }
      const auto size = std::filesystem::file_size(result.absolute_path, error);
      if (error || size > modules::max_product_manifest_bytes || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        result.error = "mobagen.yaml exceeds the 1 MiB input limit or its size is unavailable";
        return result;
      }

      std::ifstream stream(result.absolute_path, std::ios::binary);
      if (!stream.is_open()) {
        result.error = "mobagen.yaml could not be opened";
        return result;
      }
      std::string contents(static_cast<std::size_t>(size), '\0');
      if (!contents.empty()) {
        stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (stream.gcount() != static_cast<std::streamsize>(contents.size())) {
          result.error = "mobagen.yaml changed or became unreadable while loading";
          return result;
        }
      }
      char trailing = 0;
      stream.read(&trailing, 1);
      if (stream.gcount() != 0 || stream.bad()) {
        result.error = "mobagen.yaml changed or became unreadable while loading";
        return result;
      }
      result.contents = std::move(contents);
      return result;
    }

    void add_issue(NativeProjectResult& result, NativeProjectIssue issue) { result.issues.push_back(std::move(issue)); }

    PluginHashResult hash_plugin_binary(const std::filesystem::path& path) {
      PluginHashResult result;
      std::error_code error;
      const auto status = std::filesystem::symlink_status(path, error);
      if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        result.error = "plugin binary is not a real regular file";
        return result;
      }
      const auto expected_size = std::filesystem::file_size(path, error);
      const auto expected_write_time = std::filesystem::last_write_time(path, error);
      if (error || expected_size > max_locked_plugin_binary_bytes) {
        result.error = "plugin binary size or modification time is unavailable, or exceeds 1 GiB";
        return result;
      }

      std::ifstream stream(path, std::ios::binary);
      if (!stream.is_open()) {
        result.error = "plugin binary could not be opened for hashing";
        return result;
      }
      assets::Sha256Hasher hasher;
      std::array<std::byte, 64 * 1024> buffer{};
      std::uintmax_t total = 0;
      while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto read = stream.gcount();
        if (read > 0) {
          total += static_cast<std::uintmax_t>(read);
          if (total > expected_size || !hasher.update(std::span<const std::byte>{buffer.data(), static_cast<std::size_t>(read)})) {
            result.error = "plugin binary changed or exceeded the hashing limit while being read";
            return result;
          }
        }
      }
      if (stream.bad() || total != expected_size) {
        result.error = "plugin binary changed or became unreadable while being hashed";
        return result;
      }

      const auto actual_size = std::filesystem::file_size(path, error);
      const auto actual_write_time = std::filesystem::last_write_time(path, error);
      if (error || actual_size != expected_size || actual_write_time != expected_write_time) {
        result.error = "plugin binary changed while being hashed";
        return result;
      }
      const auto digest = hasher.finish();
      if (!digest.has_value()) {
        result.error = "plugin binary could not be hashed";
        return result;
      }
      result.hash = assets::to_string(*digest);
      return result;
    }

    const plugins::NativePlugin* find_catalog_plugin(const plugins::NativePluginCatalog& catalog, std::string_view provider_id) {
      for (std::size_t index = 0; index < catalog.plugin_count(); ++index) {
        const auto* plugin = catalog.plugin(index);
        if (plugin != nullptr && plugin->contract().provider.id == provider_id) {
          return plugin;
        }
      }
      return nullptr;
    }

    bool capture_lock_metadata(const plugins::NativePluginCatalog& catalog, const modules::ModuleResolution& resolution,
                               const std::filesystem::path& project_root, const modules::ResolverOptions& options,
                               modules::LockfileMetadata& metadata, NativeProjectResult& result) {
      metadata.target = options.target;
      metadata.profile = options.profile;
      for (const auto provider_index : resolution.lifecycle_order()) {
        const auto* provider = catalog.registry().provider(provider_index);
        if (provider == nullptr) {
          add_issue(result, {.code = NativeProjectIssueCode::LockMetadata, .message = "resolved provider is unavailable for lock metadata"});
          return false;
        }
        const auto* plugin = find_catalog_plugin(catalog, provider->id);
        if (plugin == nullptr) {
          continue;
        }

        const auto package = plugin->path().parent_path().lexically_relative(project_root).lexically_normal().generic_string();
        const auto hash = hash_plugin_binary(plugin->path());
        if (!hash.hash.has_value()) {
          add_issue(result, {.code = NativeProjectIssueCode::LockMetadata,
                             .message = "could not fingerprint selected plugin " + provider->id + ": " + hash.error});
          return false;
        }
        metadata.plugins.push_back({.provider = provider->id,
                                    .version = provider->version,
                                    .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
                                    .package = package,
                                    .hash = std::move(*hash.hash)});
      }
      return true;
    }

  }  // namespace

  plugins::ResolvedNativePluginActionResult NativeProjectRuntime::stop() {
    return activation_ == nullptr ? plugins::ResolvedNativePluginActionResult{} : activation_->stop();
  }

  modules::LockfileSerializeResult NativeProjectRuntime::lockfile(modules::SemanticVersion sdk_version) const {
    auto metadata = lockfile_metadata_;
    metadata.sdk = sdk_version;
    return modules::serialize_lockfile(catalog_->registry(), *resolution_, metadata);
  }

  NativeProjectResult load_native_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                          std::span<const modules::ProviderDescriptor> builtin_providers, NativeProjectLockOptions lock_options) {
    NativeProjectResult result;
    auto source = read_manifest(manifest_path);
    if (!source.contents.has_value()) {
      add_issue(result, {.code = NativeProjectIssueCode::ReadManifest, .message = std::move(source.error)});
      return result;
    }

    auto parsed = modules::parse_product_manifest(*source.contents, source.absolute_path.generic_string());
    if (!parsed.ok()) {
      add_issue(result,
                {.code = NativeProjectIssueCode::ParseManifest, .message = "mobagen.yaml is invalid", .manifest_errors = std::move(parsed.errors)});
      return result;
    }

    auto runtime = std::unique_ptr<NativeProjectRuntime>(new NativeProjectRuntime(std::move(*parsed.descriptor)));
    auto catalog = plugins::discover_native_plugin_catalog(runtime->product_, source.absolute_path.parent_path(), runtime->host_, builtin_providers);
    if (!catalog.ok()) {
      add_issue(result, {.code = NativeProjectIssueCode::Catalog,
                         .message = "native plugin catalog could not be created",
                         .catalog_issues = std::move(catalog.issues)});
      return result;
    }
    runtime->catalog_ = std::move(catalog.catalog);

    auto resolution = modules::resolve_modules(runtime->product_, runtime->catalog_->registry(), options);
    if (!resolution.ok()) {
      add_issue(result, {.code = NativeProjectIssueCode::Resolution,
                         .message = "module graph could not be resolved",
                         .resolution_issues = std::move(resolution.issues)});
      return result;
    }
    runtime->resolution_ = std::move(*resolution.resolution);

    std::error_code root_error;
    const auto project_root = std::filesystem::weakly_canonical(source.absolute_path.parent_path(), root_error);
    if (root_error) {
      add_issue(result, {.code = NativeProjectIssueCode::LockMetadata, .message = "project root could not be canonicalized for lock metadata"});
      return result;
    }
    if (!capture_lock_metadata(*runtime->catalog_, *runtime->resolution_, project_root, options, runtime->lockfile_metadata_, result)) {
      return result;
    }
    auto lockfile = runtime->lockfile(lock_options.sdk_version);
    if (!lockfile.ok()) {
      add_issue(result, {.code = NativeProjectIssueCode::LockMetadata,
                         .message = "selected native plugins could not be represented in mobagen.lock",
                         .lockfile_issues = std::move(lockfile.issues)});
      return result;
    }
    const auto lockfile_path = project_root / "mobagen.lock";
    if (lock_options.policy == NativeProjectLockPolicy::Frozen) {
      auto existing = modules::read_lockfile_bounded(lockfile_path);
      if (!existing.ok()) {
        add_issue(result, {.code = NativeProjectIssueCode::LockRead,
                           .message = "frozen native project requires a readable mobagen.lock",
                           .lockfile_read_issue = std::move(existing.issue)});
        return result;
      }
      if (*existing.contents != *lockfile.contents) {
        add_issue(result,
                  {.code = NativeProjectIssueCode::LockMismatch, .message = "mobagen.lock does not exactly match the resolved native project"});
        return result;
      }
    }

    auto activation = plugins::activate_resolved_native_plugins(*runtime->catalog_, *runtime->resolution_, runtime->host_);
    if (!activation.ok()) {
      add_issue(result, {.code = NativeProjectIssueCode::Activation,
                         .message = "resolved native plugins could not be activated",
                         .activation_issues = std::move(activation.issues)});
      return result;
    }
    runtime->activation_ = std::move(activation.activation);
    if (lock_options.policy == NativeProjectLockPolicy::Update) {
      auto written = modules::write_lockfile_atomic(lockfile_path, *lockfile.contents);
      if (!written.ok()) {
        add_issue(result, {.code = NativeProjectIssueCode::LockWrite,
                           .message = "mobagen.lock could not be updated after native plugin activation",
                           .lockfile_write_issue = std::move(written.issue)});
        return result;
      }
    }
    runtime->catalog_->discard_plugins();
    result.runtime = std::move(runtime);
    return result;
  }

}  // namespace mobagen::compositions
