#include "project_runtime.hpp"

#include "project_support.hpp"

#include <cstddef>
#include <string_view>
#include <system_error>
#include <utility>

namespace mobagen::compositions {
  namespace {

    template <typename Result> void add_issue(Result& result, PortableProjectIssue issue) { result.issues.push_back(std::move(issue)); }

    const plugins::LoadedPortableWasmPlugin* find_catalog_plugin(const plugins::PortableWasmPluginCatalog& catalog, std::string_view provider_id) {
      for (std::size_t index = 0; index < catalog.plugin_count(); ++index) {
        const auto* plugin = catalog.plugin(index);
        if (plugin != nullptr && plugin->provider().id == provider_id) return plugin;
      }
      return nullptr;
    }

    template <typename Result> bool capture_lock_metadata(const plugins::PortableWasmPluginCatalog& catalog,
                                                          const modules::ModuleResolution& resolution, const std::filesystem::path& project_root,
                                                          const modules::ResolverOptions& options, modules::LockfileMetadata& metadata,
                                                          Result& result) {
      metadata.target = options.target;
      metadata.profile = options.profile;
      for (const auto provider_index : resolution.lifecycle_order()) {
        const auto* provider = catalog.registry().provider(provider_index);
        if (provider == nullptr) {
          add_issue(result, {.code = PortableProjectIssueCode::LockMetadata, .message = "resolved provider is unavailable for lock metadata"});
          return false;
        }
        const auto* plugin = find_catalog_plugin(catalog, provider->id);
        if (plugin == nullptr) continue;

        const auto package = plugin->path().parent_path().lexically_relative(project_root).lexically_normal().generic_string();
        const auto hash = detail::hash_project_plugin_binary(plugin->path(), plugins::max_portable_wasm_plugin_binary_bytes);
        if (!hash.ok()) {
          add_issue(result, {.code = PortableProjectIssueCode::LockMetadata,
                             .message = "could not fingerprint selected portable WASM plugin " + provider->id + ": " + hash.error});
          return false;
        }
        metadata.plugins.push_back({.provider = provider->id,
                                    .version = provider->version,
                                    .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
                                    .package = package,
                                    .hash = std::move(*hash.hash)});
      }
      return true;
    }

  }  // namespace

  struct PortableProjectBuilder {
    struct Result {
      std::unique_ptr<PortableProjectRuntime> runtime;
      std::filesystem::path lockfile_path;
      std::optional<std::string> lockfile_contents;
      std::vector<PortableProjectIssue> issues;

      [[nodiscard]] bool ok() const noexcept { return runtime != nullptr && lockfile_contents.has_value() && issues.empty(); }
    };

    static Result prepare(const std::filesystem::path& manifest_path, modules::ResolverOptions options, plugins::PortableWasmBackend& backend,
                          std::span<const modules::ProviderDescriptor> builtin_providers, modules::SemanticVersion sdk_version) {
      Result result;
      auto source = detail::read_project_manifest_bounded(manifest_path);
      if (!source.ok()) {
        add_issue(result, {.code = PortableProjectIssueCode::ReadManifest, .message = std::move(source.error)});
        return result;
      }

      auto parsed = modules::parse_product_manifest(*source.contents, source.absolute_path.generic_string());
      if (!parsed.ok()) {
        add_issue(
            result,
            {.code = PortableProjectIssueCode::ParseManifest, .message = "mobagen.yaml is invalid", .manifest_errors = std::move(parsed.errors)});
        return result;
      }

      auto runtime = std::unique_ptr<PortableProjectRuntime>(new PortableProjectRuntime(std::move(*parsed.descriptor)));
      auto catalog
          = plugins::discover_portable_wasm_plugin_catalog(runtime->product_, source.absolute_path.parent_path(), backend, builtin_providers);
      if (!catalog.ok()) {
        add_issue(result, {.code = PortableProjectIssueCode::Catalog,
                           .message = "portable WASM plugin catalog could not be created",
                           .catalog_issues = std::move(catalog.issues)});
        return result;
      }
      runtime->catalog_ = std::move(catalog.catalog);

      auto resolution = modules::resolve_modules(runtime->product_, runtime->catalog_->registry(), options);
      if (!resolution.ok()) {
        add_issue(result, {.code = PortableProjectIssueCode::Resolution,
                           .message = "portable module graph could not be resolved",
                           .resolution_issues = std::move(resolution.issues)});
        return result;
      }
      runtime->resolution_ = std::move(*resolution.resolution);

      std::error_code root_error;
      const auto project_root = std::filesystem::weakly_canonical(source.absolute_path.parent_path(), root_error);
      if (root_error) {
        add_issue(result,
                  {.code = PortableProjectIssueCode::LockMetadata, .message = "project root could not be canonicalized for portable lock metadata"});
        return result;
      }
      if (!capture_lock_metadata(*runtime->catalog_, *runtime->resolution_, project_root, options, runtime->lockfile_metadata_, result)) {
        return result;
      }
      auto lockfile = runtime->lockfile(sdk_version);
      if (!lockfile.ok()) {
        add_issue(result, {.code = PortableProjectIssueCode::LockMetadata,
                           .message = "selected portable WASM plugins could not be represented in mobagen.lock",
                           .lockfile_issues = std::move(lockfile.issues)});
        return result;
      }

      result.runtime = std::move(runtime);
      result.lockfile_path = project_root / "mobagen.lock";
      result.lockfile_contents = std::move(lockfile.contents);
      return result;
    }
  };

  modules::LockfileSerializeResult PortableProjectRuntime::lockfile(modules::SemanticVersion sdk_version) const {
    auto metadata = lockfile_metadata_;
    metadata.sdk = sdk_version;
    return modules::serialize_lockfile(catalog_->registry(), *resolution_, metadata);
  }

  plugins::PortableWasmPluginActivation* PortableProjectRuntime::plugin(std::size_t index) noexcept {
    return activation_ == nullptr ? nullptr : activation_->plugin(index);
  }

  const plugins::PortableWasmPluginActivation* PortableProjectRuntime::plugin(std::size_t index) const noexcept {
    return activation_ == nullptr ? nullptr : activation_->plugin(index);
  }

  plugins::ResolvedPortableWasmPluginActionResult PortableProjectRuntime::stop() {
    return activation_ == nullptr ? plugins::ResolvedPortableWasmPluginActionResult{} : activation_->stop();
  }

  PortableProjectLockResult resolve_portable_project_lock(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                                          plugins::PortableWasmBackend& backend, modules::SemanticVersion sdk_version,
                                                          std::span<const modules::ProviderDescriptor> builtin_providers) {
    auto prepared = PortableProjectBuilder::prepare(manifest_path, std::move(options), backend, builtin_providers, sdk_version);
    std::optional<PortableProjectPreview> preview;
    if (prepared.ok()) {
      preview.emplace(prepared.runtime->product(), prepared.runtime->registry(), prepared.runtime->resolution());
    }
    return {
        .lockfile_path = std::move(prepared.lockfile_path),
        .contents = std::move(prepared.lockfile_contents),
        .preview = std::move(preview),
        .issues = std::move(prepared.issues),
    };
  }

  PortableProjectResult load_portable_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                              plugins::PortableWasmBackend& backend, std::span<const modules::ProviderDescriptor> builtin_providers,
                                              PortableProjectLockOptions lock_options) {
    auto prepared = PortableProjectBuilder::prepare(manifest_path, std::move(options), backend, builtin_providers, lock_options.sdk_version);
    PortableProjectResult result{.issues = std::move(prepared.issues)};
    if (!prepared.ok()) return result;
    auto runtime = std::move(prepared.runtime);
    auto lockfile_contents = std::move(*prepared.lockfile_contents);
    auto lockfile_path = std::move(prepared.lockfile_path);
    if (lock_options.policy == PortableProjectLockPolicy::Frozen) {
      auto existing = modules::read_lockfile_bounded(lockfile_path);
      if (!existing.ok()) {
        add_issue(result, {.code = PortableProjectIssueCode::LockRead,
                           .message = "frozen portable project requires a readable mobagen.lock",
                           .lockfile_read_issue = std::move(existing.issue)});
        return result;
      }
      if (*existing.contents != lockfile_contents) {
        add_issue(result,
                  {.code = PortableProjectIssueCode::LockMismatch, .message = "mobagen.lock does not exactly match the resolved portable project"});
        return result;
      }
    }

    auto activation = plugins::activate_resolved_portable_wasm_plugins(*runtime->catalog_, *runtime->resolution_);
    if (!activation.ok()) {
      add_issue(result, {.code = PortableProjectIssueCode::Activation,
                         .message = "resolved portable WASM plugins could not be activated",
                         .activation_issues = std::move(activation.issues)});
      return result;
    }
    runtime->activation_ = std::move(activation.activation);
    if (lock_options.policy == PortableProjectLockPolicy::Update) {
      auto written = modules::write_lockfile_atomic(lockfile_path, lockfile_contents);
      if (!written.ok()) {
        add_issue(result, {.code = PortableProjectIssueCode::LockWrite,
                           .message = "mobagen.lock could not be updated after portable WASM plugin activation",
                           .lockfile_write_issue = std::move(written.issue)});
        return result;
      }
    }
    runtime->catalog_->discard_plugins();
    result.runtime = std::move(runtime);
    return result;
  }

}  // namespace mobagen::compositions
