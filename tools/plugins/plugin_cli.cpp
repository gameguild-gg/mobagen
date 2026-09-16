#include "plugin_cli.hpp"

#include "plugins/plugin_host.hpp"
#include "plugins/plugin_loader.hpp"
#include "plugins/plugin_package.hpp"
#include "plugins/plugin_store.hpp"
#include "plugins/wasm_plugin_loader.hpp"
#include "plugins/wasm_plugin_store.hpp"
#if defined(MOBAGEN_PLUGIN_CLI_HAS_WAMR)
#  include "plugins/wamr_backend.hpp"
#endif

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mobagen::plugins::cli {
  namespace {

    void print_usage(std::ostream& stream) {
      stream << "usage:\n"
                "  MobagenPlugins verify <package.plugin>\n"
                "  MobagenPlugins install <store-root> <package.plugin>\n"
                "  MobagenPlugins list <store-root>\n"
                "  MobagenPlugins remove <store-root> <provider-id>\n";
    }

    [[nodiscard]] std::string version_string(const modules::SemanticVersion& version) {
      return std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' + std::to_string(version.patch);
    }

    template <typename PluginLoadResult> void print_load_failure(std::string_view operation, const PluginLoadResult& result, std::ostream& error) {
      error << operation << " failed";
      if (!result.issues.empty()) {
        error << ": " << result.issues.front().message;
        if (result.issues.front().system_error) {
          error << ": " << result.issues.front().system_error.message();
        }
      }
      error << '\n';
    }

    template <typename PluginStoreResult> void print_store_failure(std::string_view operation, const PluginStoreResult& result, std::ostream& error) {
      error << operation << " failed";
      if (!result.issues.empty()) {
        error << ": " << result.issues.front().message;
        if (result.issues.front().system_error) {
          error << ": " << result.issues.front().system_error.message();
        } else if (!result.issues.front().load_issues.empty()) {
          error << ": " << result.issues.front().load_issues.front().message;
        }
      }
      error << '\n';
    }

    void print_inspection_failure(std::string_view operation, const PluginPackageInspectionResult& result, std::ostream& error) {
      error << operation << " failed";
      if (result.issue.has_value()) {
        error << ": " << result.issue->message;
        if (result.issue->system_error) error << ": " << result.issue->system_error.message();
      }
      error << '\n';
    }

    void print_backend_unavailable(std::string_view operation, std::ostream& error) {
      error << operation << " failed: portable WASM backend is unavailable in this build\n";
    }

    template <typename BackendProvider>
    int verify(std::string_view package_text, std::ostream& output, std::ostream& error, BackendProvider&& backend_provider) {
      const auto package = std::filesystem::path{package_text};
      const auto inspection = inspect_plugin_package(package);
      if (!inspection.ok()) {
        print_inspection_failure("verify", inspection, error);
        return 3;
      }
      if (*inspection.kind == PluginPackageKind::PortableWasm) {
        auto* backend = backend_provider();
        if (backend == nullptr) {
          print_backend_unavailable("verify", error);
          return 3;
        }
        const auto loaded = load_portable_wasm_plugin_package(package, *backend);
        if (!loaded.plugin.has_value()) {
          print_load_failure("verify", loaded, error);
          return 3;
        }
        const auto& provider = loaded.plugin->provider();
        output << "verified\t" << provider.id << '\t' << version_string(provider.version) << '\n';
        return 0;
      }
      PluginHost host;
      const auto loaded = load_native_plugin_package(package, host.api());
      if (!loaded.plugin.has_value()) {
        print_load_failure("verify", loaded, error);
        return 3;
      }
      const auto& provider = loaded.plugin->contract().provider;
      output << "verified\t" << provider.id << '\t' << version_string(provider.version) << '\n';
      return 0;
    }

    template <typename BackendProvider> int install(std::string_view store_text, std::string_view package_text, std::ostream& output,
                                                    std::ostream& error, BackendProvider&& backend_provider) {
      const auto package = std::filesystem::path{package_text};
      const auto inspection = inspect_plugin_package(package);
      if (!inspection.ok()) {
        print_inspection_failure("install", inspection, error);
        return 3;
      }
      if (*inspection.kind == PluginPackageKind::PortableWasm) {
        auto* backend = backend_provider();
        if (backend == nullptr) {
          print_backend_unavailable("install", error);
          return 3;
        }
        const PortableWasmPluginStore store{std::filesystem::path{store_text}};
        const auto installed = store.install(package, *backend);
        if (!installed.ok()) {
          print_store_failure("install", installed, error);
          return 3;
        }
        output << "installed\t" << installed.provider_id << '\t' << version_string(installed.version) << '\t' << installed.package.generic_string()
               << '\n';
        return 0;
      }
      PluginHost host;
      const NativePluginStore store{std::filesystem::path{store_text}};
      const auto installed = store.install(package, host);
      if (!installed.ok()) {
        print_store_failure("install", installed, error);
        return 3;
      }
      output << "installed\t" << installed.provider_id << '\t' << version_string(installed.version) << '\t' << installed.package.generic_string()
             << '\n';
      return 0;
    }

    int remove(std::string_view store_text, std::string_view provider_id, std::ostream& output, std::ostream& error) {
      const NativePluginStore store{std::filesystem::path{store_text}};
      const auto removed = store.remove(provider_id);
      if (!removed.ok()) {
        print_store_failure("remove", removed, error);
        return 3;
      }
      output << "removed\t" << removed.provider_id << '\t' << removed.package.generic_string() << '\n';
      return 0;
    }

    struct ListedPlugin {
      modules::ProviderDescriptor provider;
      std::filesystem::path package;
      PluginPackageKind kind{};
    };

    std::string_view package_kind_name(PluginPackageKind kind) noexcept { return kind == PluginPackageKind::Native ? "native" : "wasm"; }

    template <typename BackendProvider>
    int list(std::string_view store_text, std::ostream& output, std::ostream& error, BackendProvider&& backend_provider) {
      if (store_text.empty()) {
        error << "list failed: plugin store root must name a directory\n";
        return 3;
      }
      std::error_code system_error;
      const auto root = std::filesystem::absolute(std::filesystem::path{store_text}, system_error).lexically_normal();
      if (system_error) {
        error << "list failed: plugin store root could not be resolved: " << system_error.message() << '\n';
        return 3;
      }
      const auto root_status = std::filesystem::symlink_status(root, system_error);
      if (system_error || !std::filesystem::is_directory(root_status) || std::filesystem::is_symlink(root_status)) {
        error << "list failed: plugin store root must be an existing real directory, not a file or symbolic link";
        if (system_error) error << ": " << system_error.message();
        error << '\n';
        return 3;
      }

      std::filesystem::directory_iterator iterator{root, system_error};
      const std::filesystem::directory_iterator end;
      if (system_error) {
        error << "list failed: plugin store could not be enumerated: " << system_error.message() << '\n';
        return 3;
      }
      std::vector<std::filesystem::path> packages;
      while (iterator != end) {
        const auto filename = iterator->path().filename().string();
        if (!filename.starts_with('.') && iterator->path().extension() == ".plugin") {
          if (packages.size() == max_native_plugin_store_packages) {
            error << "list failed: installed plugin package count exceeds limit\n";
            return 3;
          }
          packages.push_back(iterator->path());
        }
        iterator.increment(system_error);
        if (system_error) {
          error << "list failed: plugin store enumeration could not continue: " << system_error.message() << '\n';
          return 3;
        }
      }
      std::ranges::sort(packages, [](const auto& left, const auto& right) { return left.generic_string() < right.generic_string(); });

      PluginHost host;
      std::vector<ListedPlugin> entries;
      entries.reserve(packages.size());
      for (const auto& package : packages) {
        const auto inspection = inspect_plugin_package(package);
        if (!inspection.ok()) {
          print_inspection_failure("list", inspection, error);
          return 3;
        }

        modules::ProviderDescriptor provider;
        if (*inspection.kind == PluginPackageKind::Native) {
          const auto loaded = load_native_plugin_package(package, host.api());
          if (!loaded.plugin.has_value()) {
            print_load_failure("list", loaded, error);
            return 3;
          }
          provider = loaded.plugin->contract().provider;
        } else {
          auto* backend = backend_provider();
          if (backend == nullptr) {
            print_backend_unavailable("list", error);
            return 3;
          }
          const auto loaded = load_portable_wasm_plugin_package(package, *backend);
          if (!loaded.plugin.has_value()) {
            print_load_failure("list", loaded, error);
            return 3;
          }
          provider = loaded.plugin->provider();
        }
        if (package.filename() != std::filesystem::path{provider.id + ".plugin"}) {
          error << "list failed: installed plugin package filename does not match its provider ID\n";
          return 3;
        }
        entries.push_back({std::move(provider), package, *inspection.kind});
      }
      for (const auto& entry : entries) {
        output << "plugin\t" << entry.provider.id << '\t' << version_string(entry.provider.version) << '\t' << package_kind_name(entry.kind) << '\t'
               << entry.package.generic_string() << '\n';
      }
      output << "plugins\t" << entries.size() << '\n';
      return 0;
    }

    int run_with_services(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error, PluginCliServices services,
                          bool use_bundled_portable_backend) {
#if defined(MOBAGEN_PLUGIN_CLI_HAS_WAMR)
      std::optional<WamrBackend> bundled_backend;
#endif
      const auto backend_provider = [&]() -> PortableWasmBackend* {
        if (services.portable_backend != nullptr) return services.portable_backend;
#if defined(MOBAGEN_PLUGIN_CLI_HAS_WAMR)
        if (use_bundled_portable_backend) {
          if (!bundled_backend.has_value()) bundled_backend.emplace();
          return &*bundled_backend;
        }
#else
        static_cast<void>(use_bundled_portable_backend);
#endif
        return nullptr;
      };

      try {
        if (arguments.size() == 2 && arguments[0] == "verify") {
          return verify(arguments[1], output, error, backend_provider);
        }
        if (arguments.size() == 3 && arguments[0] == "install") {
          return install(arguments[1], arguments[2], output, error, backend_provider);
        }
        if (arguments.size() == 2 && arguments[0] == "list") {
          return list(arguments[1], output, error, backend_provider);
        }
        if (arguments.size() == 3 && arguments[0] == "remove") {
          return remove(arguments[1], arguments[2], output, error);
        }
        if (arguments.size() == 1 && (arguments[0] == "help" || arguments[0] == "--help")) {
          print_usage(output);
          return 0;
        }
        print_usage(error);
        return 2;
      } catch (const std::exception& exception) {
        error << "plugin command failed: " << exception.what() << '\n';
        return 3;
      } catch (...) {
        error << "plugin command failed: unknown error\n";
        return 3;
      }
    }

  }  // namespace

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    return run_with_services(arguments, output, error, {}, true);
  }

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error, PluginCliServices services) {
    return run_with_services(arguments, output, error, services, false);
  }

}  // namespace mobagen::plugins::cli
