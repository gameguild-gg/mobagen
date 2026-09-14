#include "project_runtime.hpp"

#include "project_support.hpp"

#include <utility>

namespace mobagen::compositions {
  namespace {

    void add_issue(PortableProjectResult& result, PortableProjectIssue issue) { result.issues.push_back(std::move(issue)); }

  }  // namespace

  plugins::PortableWasmPluginActivation* PortableProjectRuntime::plugin(std::size_t index) noexcept {
    return activation_ == nullptr ? nullptr : activation_->plugin(index);
  }

  const plugins::PortableWasmPluginActivation* PortableProjectRuntime::plugin(std::size_t index) const noexcept {
    return activation_ == nullptr ? nullptr : activation_->plugin(index);
  }

  plugins::ResolvedPortableWasmPluginActionResult PortableProjectRuntime::stop() {
    return activation_ == nullptr ? plugins::ResolvedPortableWasmPluginActionResult{} : activation_->stop();
  }

  PortableProjectResult load_portable_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                              plugins::PortableWasmBackend& backend, std::span<const modules::ProviderDescriptor> builtin_providers) {
    PortableProjectResult result;
    auto source = detail::read_project_manifest_bounded(manifest_path);
    if (!source.contents.has_value()) {
      add_issue(result, {.code = PortableProjectIssueCode::ReadManifest, .message = std::move(source.error)});
      return result;
    }

    auto parsed = modules::parse_product_manifest(*source.contents, source.absolute_path.generic_string());
    if (!parsed.ok()) {
      add_issue(result,
                {.code = PortableProjectIssueCode::ParseManifest, .message = "mobagen.yaml is invalid", .manifest_errors = std::move(parsed.errors)});
      return result;
    }

    auto runtime = std::unique_ptr<PortableProjectRuntime>(new PortableProjectRuntime(std::move(*parsed.descriptor)));
    auto catalog = plugins::discover_portable_wasm_plugin_catalog(runtime->product_, source.absolute_path.parent_path(), backend, builtin_providers);
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

    auto activation = plugins::activate_resolved_portable_wasm_plugins(*runtime->catalog_, *runtime->resolution_);
    if (!activation.ok()) {
      add_issue(result, {.code = PortableProjectIssueCode::Activation,
                         .message = "resolved portable WASM plugins could not be activated",
                         .activation_issues = std::move(activation.issues)});
      return result;
    }
    runtime->activation_ = std::move(activation.activation);
    runtime->catalog_->discard_plugins();
    result.runtime = std::move(runtime);
    return result;
  }

}  // namespace mobagen::compositions
