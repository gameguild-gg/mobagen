#include "project_runtime.hpp"

#include "project_support.hpp"

#include <cstddef>
#include <string_view>
#include <system_error>
#include <utility>

namespace mobagen::compositions {
  namespace {

    template <typename Result> void add_issue(Result& result, NativeProjectIssue issue) { result.issues.push_back(std::move(issue)); }

    const plugins::NativePlugin* find_catalog_plugin(const plugins::NativePluginCatalog& catalog, std::string_view provider_id) {
      for (std::size_t index = 0; index < catalog.plugin_count(); ++index) {
        const auto* plugin = catalog.plugin(index);
        if (plugin != nullptr && plugin->contract().provider.id == provider_id) {
          return plugin;
        }
      }
      return nullptr;
    }

    template <typename Result> bool capture_lock_metadata(const plugins::NativePluginCatalog& catalog, const modules::ModuleResolution& resolution,
                                                          const std::filesystem::path& project_root, const modules::ResolverOptions& options,
                                                          modules::LockfileMetadata& metadata, Result& result) {
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
        const auto hash = detail::hash_project_plugin_binary(plugin->path());
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

  struct NativeProjectBuilder {
    struct Result {
      std::unique_ptr<NativeProjectRuntime> runtime;
      std::filesystem::path lockfile_path;
      std::optional<std::string> lockfile_contents;
      std::vector<NativeProjectIssue> issues;

      [[nodiscard]] bool ok() const noexcept { return runtime != nullptr && lockfile_contents.has_value() && issues.empty(); }
    };

    static Result prepare(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                          std::span<const modules::ProviderDescriptor> builtin_providers, modules::SemanticVersion sdk_version) {
      Result result;
      auto source = detail::read_project_manifest_bounded(manifest_path);
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
      const auto manifest_hash = detail::hash_project_manifest(*source.contents);
      if (!manifest_hash.has_value()) {
        add_issue(result, {.code = NativeProjectIssueCode::LockMetadata, .message = "mobagen.yaml could not be fingerprinted for lock metadata"});
        return result;
      }
      runtime->lockfile_metadata_.manifest_hash = *manifest_hash;
      auto catalog
          = plugins::discover_native_plugin_catalog(runtime->product_, source.absolute_path.parent_path(), runtime->host_, builtin_providers);
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
      auto lockfile = runtime->lockfile(sdk_version);
      if (!lockfile.ok()) {
        add_issue(result, {.code = NativeProjectIssueCode::LockMetadata,
                           .message = "selected native plugins could not be represented in mobagen.lock",
                           .lockfile_issues = std::move(lockfile.issues)});
        return result;
      }

      result.runtime = std::move(runtime);
      result.lockfile_path = project_root / "mobagen.lock";
      result.lockfile_contents = std::move(lockfile.contents);
      return result;
    }
  };

  plugins::ResolvedNativePluginActionResult NativeProjectRuntime::stop() {
    return activation_ == nullptr ? plugins::ResolvedNativePluginActionResult{} : activation_->stop();
  }

  modules::LockfileSerializeResult NativeProjectRuntime::lockfile(modules::SemanticVersion sdk_version) const {
    auto metadata = lockfile_metadata_;
    metadata.sdk = sdk_version;
    return modules::serialize_lockfile(catalog_->registry(), *resolution_, metadata);
  }

  NativeProjectLockResult resolve_native_project_lock(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                                      modules::SemanticVersion sdk_version,
                                                      std::span<const modules::ProviderDescriptor> builtin_providers) {
    auto prepared = NativeProjectBuilder::prepare(manifest_path, std::move(options), builtin_providers, sdk_version);
    std::optional<NativeProjectPreview> preview;
    if (prepared.ok()) {
      preview.emplace(prepared.runtime->product(), prepared.runtime->registry(), prepared.runtime->resolution());
    }
    NativeProjectLockResult result{
        .lockfile_path = std::move(prepared.lockfile_path),
        .contents = std::move(prepared.lockfile_contents),
        .preview = std::move(preview),
        .issues = std::move(prepared.issues),
    };
    return result;
  }

  NativeProjectResult load_native_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                          std::span<const modules::ProviderDescriptor> builtin_providers, NativeProjectLockOptions lock_options) {
    auto prepared = NativeProjectBuilder::prepare(manifest_path, std::move(options), builtin_providers, lock_options.sdk_version);
    NativeProjectResult result{.issues = std::move(prepared.issues)};
    if (!prepared.ok()) return result;
    auto runtime = std::move(prepared.runtime);
    auto lockfile_contents = std::move(*prepared.lockfile_contents);
    auto lockfile_path = std::move(prepared.lockfile_path);
    if (lock_options.policy == NativeProjectLockPolicy::Frozen) {
      auto existing = modules::read_lockfile_bounded(lockfile_path);
      if (!existing.ok()) {
        add_issue(result, {.code = NativeProjectIssueCode::LockRead,
                           .message = "frozen native project requires a readable mobagen.lock",
                           .lockfile_read_issue = std::move(existing.issue)});
        return result;
      }
      if (*existing.contents != lockfile_contents) {
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
      auto written = modules::write_lockfile_atomic(lockfile_path, lockfile_contents);
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
