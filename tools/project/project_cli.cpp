#include "project_cli.hpp"

#include "native/project_runtime.hpp"

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

    enum class ProjectCommand : std::uint8_t { Resolve, Verify, Explain };

    struct NameBinding {
      std::string left;
      std::string right;
    };

    struct ParsedCommand {
      ProjectCommand command{};
      std::filesystem::path manifest;
      modules::ResolverOptions resolver;
      modules::SemanticVersion sdk_version{0, 0, 1};
    };

    struct ParseResult {
      std::optional<ParsedCommand> command;
      std::string error;
    };

    void print_usage(std::ostream& stream) {
      stream << "usage:\n"
                "  MobagenProject resolve <mobagen.yaml> --profile <name> --alias <alias=capability>\n"
                "      [--alias <alias=capability> ...] [--default <capability=provider> ...] [--sdk <major.minor.patch>]\n"
                "  MobagenProject verify <mobagen.yaml> --profile <name> --alias <alias=capability>\n"
                "      [--alias <alias=capability> ...] [--default <capability=provider> ...] [--sdk <major.minor.patch>]\n"
                "  MobagenProject explain <mobagen.yaml> --profile <name> --alias <alias=capability>\n"
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

    void write_sorted_relations(std::ostream& output, std::string_view relation, std::string_view provider_id,
                                const std::vector<std::string>& values) {
      auto sorted = values;
      std::ranges::sort(sorted);
      for (const auto& value : sorted) output << relation << '\t' << provider_id << '\t' << value << '\n';
    }

    ParseResult parse(std::span<const std::string_view> arguments) {
      ParseResult result;
      if (arguments.size() > max_project_cli_arguments) {
        result.error = "argument count exceeds limit";
        return result;
      }
      if (arguments.size() < 2 || (arguments[0] != "resolve" && arguments[0] != "verify" && arguments[0] != "explain") || arguments[1].empty()) {
        result.error = "expected resolve, verify, or explain and a mobagen.yaml path";
        return result;
      }

      ParsedCommand parsed{
          .command = arguments[0] == "resolve"  ? ProjectCommand::Resolve
                     : arguments[0] == "verify" ? ProjectCommand::Verify
                                                : ProjectCommand::Explain,
          .manifest = std::filesystem::path{arguments[1]},
          .resolver = {.target = native_target()},
      };
      std::vector<NameBinding> defaults;
      bool profile_seen = false;
      bool sdk_seen = false;
      for (std::size_t index = 2; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (option != "--profile" && option != "--alias" && option != "--default" && option != "--sdk") {
          result.error = "unknown option: " + std::string{option};
          return result;
        }
        if (++index == arguments.size()) {
          result.error = std::string{option} + " requires a value";
          return result;
        }
        const auto value = arguments[index];
        if (option == "--profile") {
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

    void print_project_failure(std::string_view operation, const NativeProjectLockResult& result, std::ostream& error) {
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

    int resolve(const ParsedCommand& command, std::ostream& output, std::ostream& error) {
      auto generated = resolve_native_project_lock(command.manifest, command.resolver, command.sdk_version);
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

    int verify(const ParsedCommand& command, std::ostream& output, std::ostream& error) {
      auto generated = resolve_native_project_lock(command.manifest, command.resolver, command.sdk_version);
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

    int explain(const ParsedCommand& command, std::ostream& output, std::ostream& error) {
      auto generated = resolve_native_project_lock(command.manifest, command.resolver, command.sdk_version);
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
        write_sorted_relations(output, "permission", provider->id, provider->permissions);
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
             << "dependencies\t" << preview.resolution.dependencies().size() << '\n';
      return 0;
    }

  }  // namespace

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    try {
      if (arguments.size() == 1 && (arguments[0] == "help" || arguments[0] == "--help")) {
        print_usage(output);
        return 0;
      }
      auto parsed = parse(arguments);
      if (!parsed.command.has_value()) {
        if (!parsed.error.empty()) error << "invalid project command: " << parsed.error << '\n';
        print_usage(error);
        return 2;
      }
      switch (parsed.command->command) {
        case ProjectCommand::Resolve:
          return resolve(*parsed.command, output, error);
        case ProjectCommand::Verify:
          return verify(*parsed.command, output, error);
        case ProjectCommand::Explain:
          return explain(*parsed.command, output, error);
      }
      return 3;
    } catch (const std::exception& exception) {
      error << "project command failed: " << exception.what() << '\n';
      return 3;
    } catch (...) {
      error << "project command failed: unknown error\n";
      return 3;
    }
  }

}  // namespace mobagen::compositions::cli
