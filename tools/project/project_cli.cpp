#include "project_cli.hpp"

#include "modules/lockfile.hpp"
#include "native/project_runtime.hpp"
#include "portable/project_runtime.hpp"
#include "project_bootstrap.hpp"
#include "project_support.hpp"
#if defined(MOBAGEN_PROJECT_CLI_HAS_CURL)
#  include "http/curl_client.hpp"
#endif
#if defined(MOBAGEN_PROJECT_CLI_HAS_WAMR)
#  include "plugins/wamr_backend.hpp"
#endif
#include <mobagen/version.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mobagen::compositions::cli {
  namespace {

    constexpr std::size_t max_project_cli_arguments = 4096;

    enum class ProjectCommand : std::uint8_t { Bootstrap, Sync, Resolve, Verify, Explain };

    struct NameBinding {
      std::string left;
      std::string right;
    };

    struct ParsedCommand {
      ProjectCommand command{};
      std::filesystem::path manifest;
      std::optional<std::filesystem::path> cache_root;
      modules::ResolverOptions resolver;
      modules::SemanticVersion sdk_version{MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR, MOBAGEN_SDK_VERSION_PATCH};
    };

    struct ParseResult {
      std::optional<ParsedCommand> command;
      std::string error;
    };

    struct InitCommand {
      std::filesystem::path directory;
      std::string name;
      std::string source;
    };

    struct InitParseResult {
      std::optional<InitCommand> command;
      std::string error;
    };

    struct ProjectRouteResult {
      std::optional<modules::LinkageMode> linkage;
      std::string error;
      std::vector<modules::ManifestError> manifest_errors;
    };

    void print_usage(std::ostream& stream) {
      stream << "usage:\n"
                "  MobagenProject init <directory> --name <slug> --source <https-catalog-url>\n"
                "      [--template recommended]\n"
                "  MobagenProject bootstrap <mobagen.yaml> --profile <name>\n"
                "      [--alias <alias=capability> ...] [--default <capability=provider> ...] [--cache <directory>]\n"
                "  MobagenProject sync <mobagen.yaml> --profile <name>\n"
                "      [--alias <alias=capability> ...] [--default <capability=provider> ...] [--cache <directory>]\n"
                "  MobagenProject resolve <mobagen.yaml> --profile <name>\n"
                "      [--alias <alias=capability> ...] [--default <capability=provider> ...] [--sdk <major.minor.patch>]\n"
                "  MobagenProject verify <mobagen.yaml> --profile <name>\n"
                "      [--alias <alias=capability> ...] [--default <capability=provider> ...] [--sdk <major.minor.patch>]\n"
                "  MobagenProject explain <mobagen.yaml> --profile <name>\n"
                "      [--alias <alias=capability> ...] [--default <capability=provider> ...] [--sdk <major.minor.patch>]\n";
    }

    modules::TargetPlatform native_target() noexcept {
#ifdef _WIN32
      return modules::TargetPlatform::Windows;
#elif defined(__APPLE__)
      return modules::TargetPlatform::MacOS;
#else
      return modules::TargetPlatform::Linux;
#endif
    }

    std::optional<NameBinding> parse_binding(std::string_view text) {
      const auto separator = text.find('=');
      if (separator == std::string_view::npos || separator == 0 || separator + 1 == text.size()
          || text.find('=', separator + 1) != std::string_view::npos) {
        return std::nullopt;
      }
      return NameBinding{std::string{text.substr(0, separator)}, std::string{text.substr(separator + 1)}};
    }

    bool parse_version_component(std::string_view text, std::uint32_t& output) {
      if (text.empty()) return false;
      const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
      return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    std::optional<modules::SemanticVersion> parse_version(std::string_view text) {
      const auto first = text.find('.');
      if (first == std::string_view::npos) return std::nullopt;
      const auto second = text.find('.', first + 1);
      if (second == std::string_view::npos || text.find('.', second + 1) != std::string_view::npos) return std::nullopt;
      modules::SemanticVersion version;
      if (!parse_version_component(text.substr(0, first), version.major)
          || !parse_version_component(text.substr(first + 1, second - first - 1), version.minor)
          || !parse_version_component(text.substr(second + 1), version.patch)) {
        return std::nullopt;
      }
      return version;
    }

    std::string version_string(modules::SemanticVersion version) {
      return std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' + std::to_string(version.patch);
    }

    std::string_view linkage_name(modules::LinkageMode linkage) noexcept {
      switch (linkage) {
        case modules::LinkageMode::Static:
          return "static";
        case modules::LinkageMode::Dynamic:
          return "dynamic";
        case modules::LinkageMode::Wasm:
          return "wasm";
        case modules::LinkageMode::Process:
          return "process";
      }
      return "unknown";
    }

    std::string yaml_quote(std::string_view value) {
      std::string quoted;
      quoted.reserve(value.size() + 2);
      quoted.push_back('"');
      for (const auto character : value) {
        if (character == '"' || character == '\\') quoted.push_back('\\');
        quoted.push_back(character);
      }
      quoted.push_back('"');
      return quoted;
    }

    std::string recommended_manifest(const InitCommand& command) {
      return "schema: 1\n"
             "name: " + command.name + "\n"
             "sources:\n"
             "  official:\n"
             "    url: " + yaml_quote(command.source) + "\n"
             "modules:\n"
             "  assets:\n"
             "    capability: assets.store.v1\n"
             "    use: mobagen.assets.default\n"
             "  render:\n"
             "    capability: render.backend.v1\n"
             "    use: mobagen.render.webgpu\n"
             "  window:\n"
             "    capability: window.surface.v1\n"
             "    use: mobagen.window.sdl3\n"
             "plugins: []\n"
             "profiles:\n"
             "  development:\n"
             "    linkage: dynamic\n"
             "    editor: true\n"
             "    permissions:\n"
             "      - filesystem-read\n"
             "      - gpu\n"
             "      - windowing\n"
             "  release:\n"
             "    linkage: dynamic\n"
             "    editor: false\n"
             "    permissions:\n"
             "      - filesystem-read\n"
             "      - gpu\n"
             "      - windowing\n";
    }

    void write_sorted_relations(std::ostream& output, std::string_view relation, std::string_view provider_id,
                                const std::vector<std::string>& values) {
      auto sorted = values;
      std::ranges::sort(sorted);
      for (const auto& value : sorted) output << relation << '\t' << provider_id << '\t' << value << '\n';
    }

    InitParseResult parse_init(std::span<const std::string_view> arguments) {
      InitParseResult result;
      if (arguments.size() > max_project_cli_arguments) {
        result.error = "argument count exceeds limit";
        return result;
      }
      if (arguments.size() < 2 || arguments[0] != "init" || arguments[1].empty()) {
        result.error = "expected init and a new project directory";
        return result;
      }
      InitCommand command{.directory = std::filesystem::path{arguments[1]}};
      bool name_seen = false;
      bool source_seen = false;
      bool template_seen = false;
      for (std::size_t index = 2; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (option != "--name" && option != "--source" && option != "--template") {
          result.error = "unknown init option: " + std::string{option};
          return result;
        }
        if (++index == arguments.size()) {
          result.error = std::string{option} + " requires a value";
          return result;
        }
        const auto value = arguments[index];
        if (option == "--name") {
          if (name_seen || !modules::is_slug(value)) {
            result.error = "--name must be one unique lowercase slug";
            return result;
          }
          name_seen = true;
          command.name = value;
        } else if (option == "--source") {
          if (source_seen || !modules::is_secure_https_url(value)) {
            result.error = "--source must be one secure HTTPS catalog URL";
            return result;
          }
          source_seen = true;
          command.source = value;
        } else {
          if (template_seen || value != "recommended") {
            result.error = "--template currently supports only recommended";
            return result;
          }
          template_seen = true;
        }
      }
      if (!name_seen || !source_seen) {
        result.error = "init requires --name and --source";
        return result;
      }
      result.command = std::move(command);
      return result;
    }

    int initialize(const InitCommand& command, std::ostream& output, std::ostream& error) {
      const auto contents = recommended_manifest(command);
      const auto parsed = modules::parse_product_manifest(contents, "generated mobagen.yaml");
      if (!parsed.ok()) {
        error << "init failed: internal recommended template is invalid\n";
        return 3;
      }

      std::error_code path_error;
      const auto directory = std::filesystem::absolute(command.directory, path_error).lexically_normal();
      if (path_error || directory.filename().empty()) {
        error << "init failed: project directory could not be resolved\n";
        return 3;
      }
      const auto status = std::filesystem::symlink_status(directory, path_error);
      if (path_error && status.type() != std::filesystem::file_type::not_found) {
        error << "init failed: project directory could not be inspected: " << path_error.message() << '\n';
        return 3;
      }
      if (std::filesystem::exists(status)) {
        error << "init failed: project directory already exists\n";
        return 3;
      }
      path_error.clear();
      if (!std::filesystem::create_directories(directory, path_error) || path_error) {
        error << "init failed: project directory could not be created";
        if (path_error) error << ": " << path_error.message();
        error << '\n';
        return 3;
      }

      const auto manifest_path = directory / "mobagen.yaml";
      const auto written = modules::write_lockfile_atomic(manifest_path, contents);
      if (!written.ok()) {
        std::error_code ignored;
        std::filesystem::remove(directory, ignored);
        error << "init failed: mobagen.yaml could not be written";
        if (written.issue.has_value()) error << ": " << written.issue->message;
        error << '\n';
        return 3;
      }
      output << "initialized\t" << manifest_path.generic_string() << '\n'
             << "template\trecommended\n"
             << "next\tMobagenProject bootstrap\t" << manifest_path.generic_string() << "\t--profile\tdevelopment\n";
      return 0;
    }

    ParseResult parse(std::span<const std::string_view> arguments) {
      ParseResult result;
      if (arguments.size() > max_project_cli_arguments) {
        result.error = "argument count exceeds limit";
        return result;
      }
      if (arguments.size() < 2
          || (arguments[0] != "bootstrap" && arguments[0] != "sync" && arguments[0] != "resolve" && arguments[0] != "verify"
              && arguments[0] != "explain")
          || arguments[1].empty()) {
        result.error = "expected bootstrap, sync, resolve, verify, or explain and a mobagen.yaml path";
        return result;
      }

      ParsedCommand parsed{
          .command = arguments[0] == "bootstrap" ? ProjectCommand::Bootstrap
                     : arguments[0] == "sync"    ? ProjectCommand::Sync
                     : arguments[0] == "resolve" ? ProjectCommand::Resolve
                     : arguments[0] == "verify"  ? ProjectCommand::Verify
                                                 : ProjectCommand::Explain,
          .manifest = std::filesystem::path{arguments[1]},
          .resolver = {.target = native_target()},
      };
      std::vector<NameBinding> defaults;
      bool cache_seen = false;
      bool profile_seen = false;
      bool sdk_seen = false;
      for (std::size_t index = 2; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (option != "--profile" && option != "--alias" && option != "--default" && option != "--sdk" && option != "--cache") {
          result.error = "unknown option: " + std::string{option};
          return result;
        }
        if (++index == arguments.size()) {
          result.error = std::string{option} + " requires a value";
          return result;
        }
        const auto value = arguments[index];
        if (option == "--cache") {
          if ((parsed.command != ProjectCommand::Sync && parsed.command != ProjectCommand::Bootstrap) || cache_seen || value.empty()) {
            result.error = "--cache is valid exactly once for bootstrap or sync with a non-empty directory";
            return result;
          }
          cache_seen = true;
          parsed.cache_root = std::filesystem::path{value};
        } else if (option == "--profile") {
          if (profile_seen || value.empty()) {
            result.error = "--profile must appear exactly once with a non-empty value";
            return result;
          }
          profile_seen = true;
          parsed.resolver.profile = value;
        } else if (option == "--sdk") {
          const auto version = parse_version(value);
          if (sdk_seen || !version.has_value()) {
            result.error = "--sdk must be a unique major.minor.patch version";
            return result;
          }
          sdk_seen = true;
          parsed.sdk_version = *version;
        } else {
          const auto binding = parse_binding(value);
          if (!binding.has_value()) {
            result.error = std::string{option} + " requires a name=value binding";
            return result;
          }
          if (option == "--alias") {
            if (parsed.resolver.aliases.size() == modules::max_manifest_collection_entries) {
              result.error = "alias count exceeds limit";
              return result;
            }
            parsed.resolver.aliases.push_back({std::move(binding->left), std::move(binding->right)});
          } else {
            if (defaults.size() == modules::max_manifest_collection_entries) {
              result.error = "default provider count exceeds limit";
              return result;
            }
            defaults.push_back(std::move(*binding));
          }
        }
      }
      if (!profile_seen) {
        result.error = "--profile is required";
        return result;
      }
      for (auto& binding : defaults) {
        parsed.resolver.defaults.push_back({parsed.resolver.target, parsed.resolver.profile, std::move(binding.left), std::move(binding.right)});
      }
      result.command = std::move(parsed);
      return result;
    }

    ProjectRouteResult select_project_linkage(const ParsedCommand& command) {
      ProjectRouteResult result;
      auto source = detail::read_project_manifest_bounded(command.manifest);
      if (!source.ok()) {
        result.error = std::move(source.error);
        return result;
      }
      auto parsed = modules::parse_product_manifest(*source.contents, source.absolute_path.generic_string());
      if (!parsed.ok()) {
        result.error = "mobagen.yaml is invalid";
        result.manifest_errors = std::move(parsed.errors);
        return result;
      }
      const auto profile = std::ranges::find(parsed.descriptor->profiles, command.resolver.profile, &modules::ProfileDescriptor::name);
      if (profile == parsed.descriptor->profiles.end()) {
        result.error = "selected profile '" + command.resolver.profile + "' is not declared by mobagen.yaml";
        return result;
      }
      result.linkage = profile->linkage;
      return result;
    }

    void print_route_failure(std::string_view operation, const ProjectRouteResult& result, std::ostream& error) {
      error << operation << " failed: " << result.error;
      if (!result.manifest_errors.empty()) error << ": " << result.manifest_errors.front().message;
      error << '\n';
    }

    template <typename ProjectLockResult>
    void print_project_failure(std::string_view operation, const ProjectLockResult& result, std::ostream& error) {
      error << operation << " failed";
      if (!result.issues.empty()) {
        const auto& issue = result.issues.front();
        error << ": " << issue.message;
        if (!issue.manifest_errors.empty()) {
          error << ": " << issue.manifest_errors.front().message;
        } else if (!issue.catalog_issues.empty()) {
          error << ": " << issue.catalog_issues.front().message;
        } else if (!issue.resolution_issues.empty()) {
          error << ": " << issue.resolution_issues.front().message;
        } else if (!issue.lockfile_issues.empty()) {
          error << ": " << issue.lockfile_issues.front().message;
        }
      }
      error << '\n';
    }

    template <typename ProjectLockResult> int resolve(const ParsedCommand&, ProjectLockResult generated, std::ostream& output, std::ostream& error) {
      if (!generated.ok()) {
        print_project_failure("resolve", generated, error);
        return 3;
      }
      const auto written = modules::write_lockfile_atomic(generated.lockfile_path, *generated.contents);
      if (!written.ok()) {
        error << "resolve failed: mobagen.lock could not be written";
        if (written.issue.has_value()) error << ": " << written.issue->message;
        error << '\n';
        return 3;
      }
      output << "resolved\t" << generated.lockfile_path.generic_string() << '\n';
      return 0;
    }

    template <typename ProjectLockResult> int verify(const ParsedCommand&, ProjectLockResult generated, std::ostream& output, std::ostream& error) {
      if (!generated.ok()) {
        print_project_failure("verify", generated, error);
        return 3;
      }
      const auto existing = modules::read_lockfile_bounded(generated.lockfile_path);
      if (!existing.ok()) {
        error << "verify failed: mobagen.lock could not be read";
        if (existing.issue.has_value()) error << ": " << existing.issue->message;
        error << '\n';
        return 3;
      }
      if (*existing.contents != *generated.contents) {
        error << "verify failed: mobagen.lock differs from the resolved project\n";
        return 3;
      }
      output << "verified\t" << generated.lockfile_path.generic_string() << '\n';
      return 0;
    }

    template <typename ProjectLockResult>
    int explain(const ParsedCommand& command, ProjectLockResult generated, std::ostream& output, std::ostream& error) {
      if (!generated.ok()) {
        print_project_failure("explain", generated, error);
        return 3;
      }
      const auto& preview = *generated.preview;
      std::vector<bool> selected(preview.registry.provider_count());
      for (const auto provider_index : preview.resolution.lifecycle_order()) {
        if (provider_index.value >= selected.size()) {
          error << "explain failed: resolved provider index is outside the registry\n";
          return 3;
        }
        selected[provider_index.value] = true;
      }

      output << "project\t" << preview.product.name << '\n';
      const auto profile = std::ranges::find(preview.product.profiles, command.resolver.profile, &modules::ProfileDescriptor::name);
      if (profile == preview.product.profiles.end()) {
        error << "explain failed: resolved profile is unavailable\n";
        return 3;
      }
      output << "profile\t" << profile->name << '\n';
      write_sorted_relations(output, "grant", profile->name, profile->permissions);
      for (std::size_t index = 0; index < preview.registry.provider_count(); ++index) {
        const auto* provider = preview.registry.provider(modules::ProviderIndex{static_cast<std::uint32_t>(index)});
        if (provider == nullptr) {
          error << "explain failed: registry provider is unavailable\n";
          return 3;
        }
        output << "provider\t" << provider->id << '\t' << version_string(provider->version) << '\t' << (selected[index] ? "selected" : "available")
               << '\n';
        write_sorted_relations(output, "provides", provider->id, provider->provides);
        write_sorted_relations(output, "requires", provider->id, provider->required);
        write_sorted_relations(output, "optional", provider->id, provider->optional);
        write_sorted_relations(output, "conflicts", provider->id, provider->conflicts);
        if (!provider->configuration_schema.empty()) {
          output << "config-schema\t" << provider->id << '\t' << provider->configuration_schema << '\n';
        }
        write_sorted_relations(output, "permission", provider->id, provider->permissions);
      }
      for (const auto& configuration : preview.resolution.configurations()) {
        const auto* provider = preview.registry.provider(configuration.provider);
        if (provider == nullptr) {
          error << "explain failed: configured provider is outside the registry\n";
          return 3;
        }
        output << "configuration\t" << provider->id << '\t' << configuration.schema << '\t' << configuration.data.size() << '\n';
      }
      for (const auto& selection : preview.resolution.selections()) {
        const auto* provider = preview.registry.provider(selection.provider);
        const auto capability = preview.registry.capability_name(selection.capability);
        if (provider == nullptr || capability.empty()) {
          error << "explain failed: resolved selection is outside the registry\n";
          return 3;
        }
        output << "selection\t" << capability << '\t' << provider->id << '\t' << linkage_name(selection.linkage) << '\t' << selection.reason << '\n';
      }
      for (const auto& dependency : preview.resolution.dependencies()) {
        const auto* provider = preview.registry.provider(dependency.dependency);
        const auto* dependent = preview.registry.provider(dependency.dependent);
        const auto capability = preview.registry.capability_name(dependency.capability);
        if (provider == nullptr || dependent == nullptr || capability.empty()) {
          error << "explain failed: resolved dependency is outside the registry\n";
          return 3;
        }
        output << "dependency\t" << provider->id << '\t' << dependent->id << '\t' << capability << '\n';
      }
      output << "providers\t" << preview.registry.provider_count() << '\n'
             << "selections\t" << preview.resolution.selections().size() << '\n'
             << "dependencies\t" << preview.resolution.dependencies().size() << '\n'
             << "configurations\t" << preview.resolution.configurations().size() << '\n';
      return 0;
    }

    template <typename ProjectLockResult>
    int execute(const ParsedCommand& command, ProjectLockResult generated, std::ostream& output, std::ostream& error) {
      switch (command.command) {
        case ProjectCommand::Resolve:
          return resolve(command, std::move(generated), output, error);
        case ProjectCommand::Verify:
          return verify(command, std::move(generated), output, error);
        case ProjectCommand::Explain:
          return explain(command, std::move(generated), output, error);
        case ProjectCommand::Bootstrap:
        case ProjectCommand::Sync:
          break;
      }
      return 3;
    }

    int print_bootstrap(std::string_view operation, const ProjectBootstrapResult& bootstrapped, std::ostream& output, std::ostream& error) {
      if (!bootstrapped.ok()) {
        error << operation << " failed";
        if (!bootstrapped.issues.empty()) {
          error << ": " << bootstrapped.issues.front().message;
        }
        error << '\n';
        return 3;
      }
      if (bootstrapped.state == ProjectBootstrapState::Ready) {
        output << "ready\t" << bootstrapped.product_name << '\t' << bootstrapped.profile << '\n' << "plugins\t" << bootstrapped.plugin_count << '\n';
        return 0;
      }

      output << "catalogs-synced\t" << bootstrapped.product_name << '\t' << bootstrapped.profile << '\n';
      for (const auto& selection : bootstrapped.selections) {
        output << "artifact\t" << selection.provider_id << '\t' << version_string(selection.version) << '\t'
               << linkage_name(selection.artifact.linkage) << '\t' << selection.artifact.abi_version << '\t' << selection.artifact.size << '\t'
               << selection.artifact.hash << '\t' << selection.artifact.url << '\n';
      }
      for (const auto& artifact : bootstrapped.cached_artifacts) {
        output << "cache\t" << artifact.provider_id << '\t' << (artifact.downloaded ? "downloaded" : "present") << '\t'
               << artifact.cache_path.generic_string() << '\n';
      }
      for (const auto& artifact : bootstrapped.installed_artifacts) {
        output << "plugin\t" << artifact.provider_id << '\t' << (artifact.installed ? "installed" : "present") << '\t'
               << artifact.package_path.generic_string() << '\n';
      }
      output << "lock\t" << bootstrapped.lockfile_path.generic_string() << '\n' << "selected\t" << bootstrapped.selected_count << '\n';
      return 0;
    }

    int run_with_services(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error, ProjectCliServices services,
                          bool use_bundled_backends) {
      try {
        if (arguments.size() == 1 && (arguments[0] == "help" || arguments[0] == "--help")) {
          print_usage(output);
          return 0;
        }
        if (!arguments.empty() && arguments[0] == "init") {
          auto initialized = parse_init(arguments);
          if (!initialized.command.has_value()) {
            if (!initialized.error.empty()) {
              error << "invalid init command: " << initialized.error << '\n';
            }
            print_usage(error);
            return 2;
          }
          return initialize(*initialized.command, output, error);
        }
        auto parsed = parse(arguments);
        if (!parsed.command.has_value()) {
          if (!parsed.error.empty()) error << "invalid project command: " << parsed.error << '\n';
          print_usage(error);
          return 2;
        }
        auto route = select_project_linkage(*parsed.command);
        if (!route.linkage.has_value()) {
          print_route_failure(arguments.front(), route, error);
          return 3;
        }
        if (parsed.command->command == ProjectCommand::Bootstrap || parsed.command->command == ProjectCommand::Sync) {
          ProjectBootstrapOptions options{
              .resolver = parsed.command->resolver,
              .sdk_version = parsed.command->sdk_version,
              .cache_root = parsed.command->cache_root,
              .mode = parsed.command->command == ProjectCommand::Bootstrap ? ProjectBootstrapMode::Ensure : ProjectBootstrapMode::Refresh,
              .validation = ProjectBootstrapValidation::Full,
          };
          if (services.http_client != nullptr) {
            const auto bootstrapped = bootstrap_project(parsed.command->manifest, options, services.http_client);
            return print_bootstrap(arguments.front(), bootstrapped, output, error);
          }
#if defined(MOBAGEN_PROJECT_CLI_HAS_CURL)
          if (use_bundled_backends) {
            http::CurlClient client;
            const auto bootstrapped = bootstrap_project(parsed.command->manifest, std::move(options), &client);
            return print_bootstrap(arguments.front(), bootstrapped, output, error);
          }
#else
          static_cast<void>(use_bundled_backends);
#endif
          const auto bootstrapped = bootstrap_project(parsed.command->manifest, std::move(options));
          return print_bootstrap(arguments.front(), bootstrapped, output, error);
        }
        if (*route.linkage == modules::LinkageMode::Wasm) {
#if defined(MOBAGEN_PROJECT_CLI_HAS_WAMR)
          std::optional<plugins::WamrBackend> bundled_backend;
          if (services.portable_backend == nullptr && use_bundled_backends) {
            bundled_backend.emplace();
            services.portable_backend = &*bundled_backend;
          }
#else
          static_cast<void>(use_bundled_backends);
#endif
          if (services.portable_backend == nullptr) {
            error << arguments.front() << " failed: portable WASM backend is unavailable in this build\n";
            return 3;
          }
          auto generated = resolve_portable_project_lock(parsed.command->manifest, parsed.command->resolver, *services.portable_backend,
                                                         parsed.command->sdk_version);
          return execute(*parsed.command, std::move(generated), output, error);
        }
        if (*route.linkage == modules::LinkageMode::Process) {
          error << arguments.front() << " failed: process plugin linkage is not implemented\n";
          return 3;
        }
        auto generated = resolve_native_project_lock(parsed.command->manifest, parsed.command->resolver, parsed.command->sdk_version);
        return execute(*parsed.command, std::move(generated), output, error);
      } catch (const std::exception& exception) {
        error << "project command failed: " << exception.what() << '\n';
        return 3;
      } catch (...) {
        error << "project command failed: unknown error\n";
        return 3;
      }
    }

  }  // namespace

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    return run_with_services(arguments, output, error, {}, true);
  }

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error, ProjectCliServices services) {
    return run_with_services(arguments, output, error, services, false);
  }

}  // namespace mobagen::compositions::cli
