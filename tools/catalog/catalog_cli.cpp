#include "catalog_cli.hpp"

#include "catalog_publisher.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace mobagen::tools::catalog_cli {
  namespace {

    std::optional<modules::TargetPlatform> parse_target(std::string_view value) {
      if (value == "windows") return modules::TargetPlatform::Windows;
      if (value == "linux") return modules::TargetPlatform::Linux;
      if (value == "macos") return modules::TargetPlatform::MacOS;
      if (value == "web") return modules::TargetPlatform::Web;
      if (value == "android") return modules::TargetPlatform::Android;
      if (value == "ios") return modules::TargetPlatform::IOS;
      return std::nullopt;
    }

    void usage(std::ostream& error) {
      error << "usage: MobagenCatalog publish <output-directory> --base-url <https-url> "
               "--target <platform> --plugin <binary> [--plugin <binary> ...]\n";
    }

  }  // namespace

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    if (arguments.size() < 8 || arguments[0] != "publish" || arguments[1].empty()) {
      usage(error);
      return 2;
    }
    NativeCatalogPublishOptions options{.output_root = arguments[1]};
    bool base_seen = false;
    bool target_seen = false;
    for (std::size_t index = 2; index < arguments.size(); ++index) {
      const auto option = arguments[index];
      if (option != "--base-url" && option != "--target" && option != "--plugin") {
        usage(error);
        return 2;
      }
      if (++index == arguments.size() || arguments[index].empty()) {
        usage(error);
        return 2;
      }
      const auto value = arguments[index];
      if (option == "--base-url") {
        if (base_seen) {
          usage(error);
          return 2;
        }
        base_seen = true;
        options.base_url = value;
      } else if (option == "--target") {
        const auto target = parse_target(value);
        if (target_seen || !target.has_value()) {
          usage(error);
          return 2;
        }
        target_seen = true;
        options.target = *target;
      } else {
        options.plugin_binaries.emplace_back(value);
      }
    }
    if (!base_seen || !target_seen || options.plugin_binaries.empty()) {
      usage(error);
      return 2;
    }

    const auto published = publish_native_module_catalog(options);
    if (!published.ok()) {
      for (const auto& issue : published.issues) {
        error << "publish failed\t" << issue.path.generic_string() << '\t' << issue.message << '\n';
      }
      return 3;
    }
    output << "catalog\t" << published.catalog_path->generic_string() << '\n';
    for (const auto& provider : published.providers) {
      output << "provider\t" << provider.provider.id << '\t' << provider.artifacts.front().hash << '\n';
    }
    return 0;
  }

}  // namespace mobagen::tools::catalog_cli
