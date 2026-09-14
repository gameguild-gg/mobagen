#include "plugin_cli.hpp"

#include "plugins/plugin_host.hpp"
#include "plugins/plugin_loader.hpp"
#include "plugins/plugin_store.hpp"

#include <exception>
#include <filesystem>
#include <ostream>
#include <string>

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

    void print_load_failure(std::string_view operation, const NativePluginLoadResult& result, std::ostream& error) {
      error << operation << " failed";
      if (!result.issues.empty()) {
        error << ": " << result.issues.front().message;
        if (result.issues.front().system_error) {
          error << ": " << result.issues.front().system_error.message();
        }
      }
      error << '\n';
    }

    void print_store_failure(std::string_view operation, const NativePluginStoreActionResult& result, std::ostream& error) {
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

    void print_store_failure(std::string_view operation, const NativePluginStoreListResult& result, std::ostream& error) {
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

    int verify(std::string_view package_text, std::ostream& output, std::ostream& error) {
      PluginHost host;
      const auto loaded = load_native_plugin_package(std::filesystem::path{package_text}, host.api());
      if (!loaded.plugin.has_value()) {
        print_load_failure("verify", loaded, error);
        return 3;
      }
      const auto& provider = loaded.plugin->contract().provider;
      output << "verified\t" << provider.id << '\t' << version_string(provider.version) << '\n';
      return 0;
    }

    int install(std::string_view store_text, std::string_view package_text, std::ostream& output, std::ostream& error) {
      PluginHost host;
      const NativePluginStore store{std::filesystem::path{store_text}};
      const auto installed = store.install(std::filesystem::path{package_text}, host);
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

    int list(std::string_view store_text, std::ostream& output, std::ostream& error) {
      PluginHost host;
      const NativePluginStore store{std::filesystem::path{store_text}};
      const auto inventory = store.list(host);
      if (!inventory.ok()) {
        print_store_failure("list", inventory, error);
        return 3;
      }
      for (const auto& entry : inventory.entries) {
        output << "plugin\t" << entry.provider.id << '\t' << version_string(entry.provider.version) << '\t' << entry.package.generic_string() << '\n';
      }
      output << "plugins\t" << inventory.entries.size() << '\n';
      return 0;
    }

  }  // namespace

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    try {
      if (arguments.size() == 2 && arguments[0] == "verify") {
        return verify(arguments[1], output, error);
      }
      if (arguments.size() == 3 && arguments[0] == "install") {
        return install(arguments[1], arguments[2], output, error);
      }
      if (arguments.size() == 2 && arguments[0] == "list") {
        return list(arguments[1], output, error);
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

}  // namespace mobagen::plugins::cli
