#include "project_runtime.hpp"

#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace mobagen::compositions {
  namespace {

    struct ManifestReadResult {
      std::optional<std::string> contents;
      std::filesystem::path absolute_path;
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

  }  // namespace

  plugins::ResolvedNativePluginActionResult NativeProjectRuntime::stop() {
    return activation_ == nullptr ? plugins::ResolvedNativePluginActionResult{} : activation_->stop();
  }

  NativeProjectResult load_native_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                          std::span<const modules::ProviderDescriptor> builtin_providers) {
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

    auto activation = plugins::activate_resolved_native_plugins(*runtime->catalog_, *runtime->resolution_, runtime->host_);
    if (!activation.ok()) {
      add_issue(result, {.code = NativeProjectIssueCode::Activation,
                         .message = "resolved native plugins could not be activated",
                         .activation_issues = std::move(activation.issues)});
      return result;
    }
    runtime->activation_ = std::move(activation.activation);
    runtime->catalog_->discard_plugins();
    result.runtime = std::move(runtime);
    return result;
  }

}  // namespace mobagen::compositions
