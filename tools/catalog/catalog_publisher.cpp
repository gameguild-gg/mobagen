#include "catalog_publisher.hpp"

#include "assets/asset_id.hpp"
#include "modules/lockfile.hpp"
#include "plugins/plugin_abi.h"
#include "plugins/plugin_host.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <ranges>
#include <span>
#include <sstream>
#include <string_view>

namespace mobagen::tools {
  namespace {

    struct HashedFile {
      std::uint64_t size{};
      std::string hash;
    };

    struct PreparedPlugin {
      std::filesystem::path source;
      modules::PublishedProviderDescriptor published;
    };

    void add_issue(
        NativeCatalogPublishResult& result, NativeCatalogPublishIssueCode code,
        std::filesystem::path path, std::string message,
        std::vector<plugins::NativePluginLoadIssue> plugin_issues = {}
    ) {
      result.issues.push_back({
          code, std::move(path), std::move(message), std::move(plugin_issues)
      });
    }

    std::string_view target_name(modules::TargetPlatform target) noexcept {
      switch (target) {
        case modules::TargetPlatform::Windows:
          return "windows";
        case modules::TargetPlatform::Linux:
          return "linux";
        case modules::TargetPlatform::MacOS:
          return "macos";
        case modules::TargetPlatform::Web:
          return "web";
        case modules::TargetPlatform::Android:
          return "android";
        case modules::TargetPlatform::IOS:
          return "ios";
      }
      return {};
    }

    std::string_view reload_name(modules::ReloadPolicy reload) noexcept {
      switch (reload) {
        case modules::ReloadPolicy::Never:
          return "never";
        case modules::ReloadPolicy::Restart:
          return "restart";
        case modules::ReloadPolicy::SafePoint:
          return "safe-point";
      }
      return {};
    }

    std::string version_name(const modules::SemanticVersion& version) {
      return std::to_string(version.major) + '.' + std::to_string(version.minor)
             + '.' + std::to_string(version.patch);
    }

    std::string yaml_quote(std::string_view value) {
      std::string result;
      result.reserve(value.size() + 2);
      result.push_back('"');
      for (const auto character : value) {
        if (character == '"' || character == '\\') result.push_back('\\');
        result.push_back(character);
      }
      result.push_back('"');
      return result;
    }

    std::optional<HashedFile> hash_file(
        const std::filesystem::path& path, std::uint64_t limit
    ) {
      std::error_code error;
      const auto status = std::filesystem::symlink_status(path, error);
      if (error || !std::filesystem::is_regular_file(status)
          || std::filesystem::is_symlink(status)) {
        return std::nullopt;
      }
      const auto size = std::filesystem::file_size(path, error);
      if (error || size == 0 || size > limit) return std::nullopt;
      const auto modified = std::filesystem::last_write_time(path, error);
      if (error) return std::nullopt;

      std::ifstream stream(path, std::ios::binary);
      if (!stream.is_open()) return std::nullopt;
      assets::Sha256Hasher hasher;
      std::array<std::byte, 64 * 1024> buffer{};
      std::uint64_t consumed = 0;
      while (stream) {
        stream.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size())
        );
        const auto count = stream.gcount();
        if (count < 0) return std::nullopt;
        const auto bytes = std::span{buffer}.first(static_cast<std::size_t>(count));
        if (!hasher.update(bytes)) return std::nullopt;
        consumed += static_cast<std::uint64_t>(count);
      }
      if (!stream.eof() || consumed != size) return std::nullopt;
      const auto final_size = std::filesystem::file_size(path, error);
      if (error || final_size != size
          || std::filesystem::last_write_time(path, error) != modified || error) {
        return std::nullopt;
      }
      const auto id = hasher.finish();
      if (!id.has_value()) return std::nullopt;
      return HashedFile{size, assets::to_string(*id)};
    }

    void write_list(
        std::ostringstream& output, std::string_view name,
        const std::vector<std::string>& values
    ) {
      if (values.empty()) return;
      auto sorted = values;
      std::ranges::sort(sorted);
      output << "    " << name << ":\n";
      for (const auto& value : sorted) output << "      - " << value << '\n';
    }

    std::string serialize_catalog(
        std::span<const modules::PublishedProviderDescriptor> providers
    ) {
      std::ostringstream output;
      output << "schema: " << modules::module_catalog_schema_version
             << "\nproviders:\n";
      for (const auto& published : providers) {
        const auto& provider = published.provider;
        output << "  " << provider.id << ":\n"
               << "    version: " << version_name(provider.version) << '\n';
        write_list(output, "provides", provider.provides);
        write_list(output, "requires", provider.required);
        write_list(output, "optional", provider.optional);
        write_list(output, "conflicts", provider.conflicts);
        output << "    reload: " << reload_name(provider.reload) << '\n';
        if (!provider.configuration_schema.empty()) {
          output << "    configuration-schema: "
                 << provider.configuration_schema << '\n';
        }
        write_list(output, "permissions", provider.permissions);
        output << "    artifacts:\n";
        for (const auto& artifact : published.artifacts) {
          output << "      - target: " << target_name(artifact.target) << '\n'
                 << "        linkage: dynamic\n"
                 << "        abi: " << artifact.abi_version << '\n'
                 << "        url: " << yaml_quote(artifact.url) << '\n'
                 << "        size: " << artifact.size << '\n'
                 << "        hash: " << artifact.hash << '\n';
        }
      }
      return std::move(output).str();
    }

    bool copy_verified_artifact(
        NativeCatalogPublishResult& result, const PreparedPlugin& prepared,
        const std::filesystem::path& destination
    ) {
      const auto& expected = prepared.published.artifacts.front();
      std::error_code error;
      const auto status = std::filesystem::symlink_status(destination, error);
      if (!error && status.type() != std::filesystem::file_type::not_found) {
        const auto existing = hash_file(destination, modules::max_module_artifact_bytes);
        if (existing.has_value() && existing->size == expected.size
            && existing->hash == expected.hash) {
          return true;
        }
        add_issue(
            result, NativeCatalogPublishIssueCode::ArtifactConflict, destination,
            "published artifact path already contains different bytes"
        );
        return false;
      }
      if (error && status.type() != std::filesystem::file_type::not_found) {
        add_issue(
            result, NativeCatalogPublishIssueCode::ArtifactWriteFailed, destination,
            "published artifact path could not be inspected"
        );
        return false;
      }

      std::filesystem::create_directories(destination.parent_path(), error);
      if (error
          || !std::filesystem::copy_file(
              prepared.source, destination, std::filesystem::copy_options::none, error
          )) {
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        add_issue(
            result, NativeCatalogPublishIssueCode::ArtifactWriteFailed, destination,
            "plugin artifact could not be copied into the catalog", {}
        );
        return false;
      }
      const auto copied = hash_file(destination, modules::max_module_artifact_bytes);
      if (!copied.has_value() || copied->size != expected.size
          || copied->hash != expected.hash) {
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        add_issue(
            result, NativeCatalogPublishIssueCode::ArtifactWriteFailed, destination,
            "copied plugin artifact failed integrity verification"
        );
        return false;
      }
      return true;
    }

  }  // namespace

  NativeCatalogPublishResult publish_native_module_catalog(
      const NativeCatalogPublishOptions& options
  ) {
    NativeCatalogPublishResult result;
    auto base_url = options.base_url;
    while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();
    const auto platform = target_name(options.target);
    if (options.output_root.empty() || base_url.empty()
        || !modules::is_secure_https_url(base_url) || platform.empty()
        || options.plugin_binaries.empty()
        || options.plugin_binaries.size() > modules::max_module_catalog_providers) {
      add_issue(
          result, NativeCatalogPublishIssueCode::InvalidOptions, options.output_root,
          "catalog publication requires an output, secure base URL, target, and bounded plugin list"
      );
      return result;
    }

    std::error_code error;
    const auto output_root =
        std::filesystem::absolute(options.output_root, error).lexically_normal();
    if (error || output_root.filename().empty()) {
      add_issue(
          result, NativeCatalogPublishIssueCode::InvalidOutput, options.output_root,
          "catalog output root could not be resolved"
      );
      return result;
    }
    const auto output_status = std::filesystem::symlink_status(output_root, error);
    if (!error && output_status.type() != std::filesystem::file_type::not_found
        && (!std::filesystem::is_directory(output_status)
            || std::filesystem::is_symlink(output_status))) {
      add_issue(
          result, NativeCatalogPublishIssueCode::InvalidOutput, output_root,
          "catalog output root must be a real directory"
      );
      return result;
    }
    if (error && output_status.type() != std::filesystem::file_type::not_found) {
      add_issue(
          result, NativeCatalogPublishIssueCode::InvalidOutput, output_root,
          "catalog output root could not be inspected"
      );
      return result;
    }

    plugins::PluginHost host;
    std::vector<PreparedPlugin> prepared;
    prepared.reserve(options.plugin_binaries.size());
    for (const auto& binary : options.plugin_binaries) {
      auto loaded = plugins::load_native_plugin_binary(binary, host.api());
      if (!loaded.plugin.has_value()) {
        add_issue(
            result, NativeCatalogPublishIssueCode::InvalidPlugin, binary,
            "catalog input is not a valid native plugin", std::move(loaded.issues)
        );
        return result;
      }
      auto provider = loaded.plugin->contract().provider;
      if (provider.targets != std::vector<modules::TargetPlatform>{options.target}) {
        add_issue(
            result, NativeCatalogPublishIssueCode::InvalidPlugin, binary,
            "native plugin target does not match the publication target"
        );
        return result;
      }
      if (std::ranges::any_of(prepared, [&](const auto& item) {
            return item.published.provider.id == provider.id;
          })) {
        add_issue(
            result, NativeCatalogPublishIssueCode::DuplicateProvider, binary,
            "catalog inputs contain the same provider more than once"
        );
        return result;
      }
      const auto hashed = hash_file(binary, modules::max_module_artifact_bytes);
      if (!hashed.has_value()) {
        add_issue(
            result, NativeCatalogPublishIssueCode::ArtifactReadFailed, binary,
            "plugin artifact could not be hashed as a bounded regular file"
        );
        return result;
      }
      const auto version = version_name(provider.version);
      const auto relative = std::filesystem::path{provider.id} / version
                            / (std::string{platform} + ".plugin");
      const auto url = base_url + '/' + relative.generic_string();
      if (!modules::is_secure_plugin_artifact_url(url)) {
        add_issue(
            result, NativeCatalogPublishIssueCode::InvalidOptions, binary,
            "catalog base URL cannot produce a secure .plugin artifact URL"
        );
        return result;
      }
      prepared.push_back({
          .source = std::filesystem::absolute(binary).lexically_normal(),
          .published = {
              .provider = std::move(provider),
              .artifacts = {{
                  .target = options.target,
                  .linkage = modules::LinkageMode::Dynamic,
                  .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
                  .url = url,
                  .size = hashed->size,
                  .hash = hashed->hash,
              }},
          },
      });
    }
    std::ranges::sort(prepared, {}, [](const auto& item) {
      return item.published.provider.id;
    });

    std::filesystem::create_directories(output_root, error);
    if (error) {
      add_issue(
          result, NativeCatalogPublishIssueCode::InvalidOutput, output_root,
          "catalog output root could not be created"
      );
      return result;
    }
    for (const auto& plugin : prepared) {
      const auto& artifact = plugin.published.artifacts.front();
      const auto destination = output_root / plugin.published.provider.id
                               / version_name(plugin.published.provider.version)
                               / (std::string{target_name(artifact.target)} + ".plugin");
      if (!copy_verified_artifact(result, plugin, destination)) return result;
    }

    result.providers.reserve(prepared.size());
    for (auto& plugin : prepared) {
      result.providers.push_back(std::move(plugin.published));
    }
    const auto contents = serialize_catalog(result.providers);
    const auto catalog_path = output_root / "catalog.yaml";
    const auto written = modules::write_lockfile_atomic(catalog_path, contents);
    if (!written.ok()) {
      add_issue(
          result, NativeCatalogPublishIssueCode::CatalogWriteFailed, catalog_path,
          written.issue.has_value() ? written.issue->message
                                    : "catalog could not be committed atomically"
      );
      return result;
    }
    result.catalog_path = catalog_path;
    return result;
  }

}  // namespace mobagen::tools
