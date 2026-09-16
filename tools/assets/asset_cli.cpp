#include "asset_cli.hpp"

#include "assets/asset_cache.hpp"
#include "assets/asset_id.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <ostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mobagen::assets::cli {
  namespace {

    constexpr std::size_t max_scanned_files = 1'000'000;

    struct ScannedFile {
      std::string relative_path;
      std::uintmax_t size{0};
    };

    void print_usage(std::ostream& stream) {
      stream << "usage:\n"
                "  MobagenAssets scan <source-root>\n"
                "  MobagenAssets import <cache-root> <source-file>\n"
                "  MobagenAssets verify <cache-root> <sha256:id>\n";
    }

    [[nodiscard]] std::string_view status_name(AssetCacheStatus status) {
      switch (status) {
        case AssetCacheStatus::stored:
          return "stored";
        case AssetCacheStatus::already_present:
          return "already present";
        case AssetCacheStatus::loaded:
          return "loaded";
        case AssetCacheStatus::not_found:
          return "not found";
        case AssetCacheStatus::invalid_root:
          return "invalid root";
        case AssetCacheStatus::too_large:
          return "too large";
        case AssetCacheStatus::integrity_error:
          return "integrity error";
        case AssetCacheStatus::source_changed:
          return "source changed during import";
        case AssetCacheStatus::io_error:
          return "I/O error";
      }
      return "unknown error";
    }

    int scan(std::string_view root_text, std::ostream& output, std::ostream& error_stream) {
      const std::filesystem::path root{root_text};
      std::error_code error;
      if (!std::filesystem::is_directory(root, error)) {
        error_stream << "scan failed: source root is not a directory";
        if (error) {
          error_stream << ": " << error.message();
        }
        error_stream << '\n';
        return 3;
      }

      std::vector<ScannedFile> files;
      std::filesystem::recursive_directory_iterator iterator(root, error);
      const std::filesystem::recursive_directory_iterator end;
      if (error) {
        error_stream << "scan failed: " << error.message() << '\n';
        return 3;
      }
      while (iterator != end) {
        const auto status = iterator->symlink_status(error);
        if (error) {
          error_stream << "scan failed: " << error.message() << '\n';
          return 3;
        }
        if (std::filesystem::is_regular_file(status)) {
          if (files.size() == max_scanned_files) {
            error_stream << "scan failed: file count exceeds limit\n";
            return 3;
          }
          const auto size = iterator->file_size(error);
          if (error) {
            error_stream << "scan failed: " << error.message() << '\n';
            return 3;
          }
          const auto relative = std::filesystem::relative(iterator->path(), root, error);
          if (error) {
            error_stream << "scan failed: " << error.message() << '\n';
            return 3;
          }
          files.push_back({relative.generic_string(), size});
        }
        iterator.increment(error);
        if (error) {
          error_stream << "scan failed: " << error.message() << '\n';
          return 3;
        }
      }

      std::sort(files.begin(), files.end(), [](const ScannedFile& lhs, const ScannedFile& rhs) { return lhs.relative_path < rhs.relative_path; });
      for (const auto& file : files) {
        output << "file\t" << file.size << '\t' << file.relative_path << '\n';
      }
      output << "files\t" << files.size() << '\n';
      return 0;
    }

    int import_file(std::string_view cache_root, std::string_view source, std::ostream& output, std::ostream& error_stream) {
      const AssetCache cache{std::filesystem::path{cache_root}};
      const auto result = cache.store_file(std::filesystem::path{source});
      if (!result.ok() || !result.id.has_value()) {
        error_stream << "import failed: " << status_name(result.status);
        if (result.system_error) {
          error_stream << ": " << result.system_error.message();
        }
        error_stream << '\n';
        return 3;
      }
      output << "imported\t" << to_string(*result.id) << '\n';
      return 0;
    }

    int verify(std::string_view cache_root, std::string_view id_text, std::ostream& output, std::ostream& error_stream) {
      const auto id = parse_asset_id(id_text);
      if (!id.has_value()) {
        error_stream << "invalid asset id: expected canonical sha256:<64 lowercase hex>\n";
        return 2;
      }
      const AssetCache cache{std::filesystem::path{cache_root}};
      const auto result = cache.load(*id);
      if (!result.ok()) {
        error_stream << "verify failed: " << status_name(result.status);
        if (result.system_error) {
          error_stream << ": " << result.system_error.message();
        }
        error_stream << '\n';
        return 3;
      }
      output << "verified\t" << id_text << '\t' << result.bytes.size() << '\n';
      return 0;
    }

  }  // namespace

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    try {
      if (arguments.size() == 2 && arguments[0] == "scan") {
        return scan(arguments[1], output, error);
      }
      if (arguments.size() == 3 && arguments[0] == "import") {
        return import_file(arguments[1], arguments[2], output, error);
      }
      if (arguments.size() == 3 && arguments[0] == "verify") {
        return verify(arguments[1], arguments[2], output, error);
      }
      if (arguments.size() == 1 && (arguments[0] == "help" || arguments[0] == "--help")) {
        print_usage(output);
        return 0;
      }
      print_usage(error);
      return 2;
    } catch (const std::exception& exception) {
      error << "asset command failed: " << exception.what() << '\n';
      return 3;
    } catch (...) {
      error << "asset command failed: unknown error\n";
      return 3;
    }
  }

}  // namespace mobagen::assets::cli
