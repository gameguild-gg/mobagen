#include <doctest/doctest.h>

#include "catalog_publisher.hpp"
#include "project_cli.hpp"
#include "project_startup.hpp"

#include <mobagen/plugin/asset_store_v1.h>

#include "http/client.hpp"
#include "modules/artifact_installer.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#  define NOMINMAX
#  include <Windows.h>
#else
#  include <dlfcn.h>
#endif

namespace {

  class TemporaryRecommendedProject {
  public:
    TemporaryRecommendedProject() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now()
                             .time_since_epoch()
                             .count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-recommended-e2e-" + std::to_string(ticks) + '-'
                 + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryRecommendedProject() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
    }

  private:
    std::filesystem::path path_;
  };

  std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::string contents{
        std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}
    };
    const auto characters = std::as_bytes(std::span{contents});
    return {characters.begin(), characters.end()};
  }

  bool native_library_loaded(const std::filesystem::path& binary) {
#if defined(_WIN32)
    return GetModuleHandleW(binary.wstring().c_str()) != nullptr;
#else
    void* handle = dlopen(binary.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (handle == nullptr) return false;
    dlclose(handle);
    return true;
#endif
  }

  constexpr mobagen::modules::TargetPlatform native_target() noexcept {
#if defined(_WIN32)
    return mobagen::modules::TargetPlatform::Windows;
#elif defined(__APPLE__)
    return mobagen::modules::TargetPlatform::MacOS;
#else
    return mobagen::modules::TargetPlatform::Linux;
#endif
  }

  class PublishedCatalogClient final : public mobagen::http::Client {
  public:
    PublishedCatalogClient(
        std::filesystem::path root, std::string base_url
    )
        : root_(std::move(root)), base_url_(std::move(base_url)) {}

    mobagen::http::GetResult get(
        const mobagen::http::GetRequest& request
    ) override {
      ++catalog_requests;
      if (request.url != base_url_ + "/catalog.yaml") {
        return {.response = mobagen::http::Response{404, {}}};
      }
      auto body = read_bytes(root_ / "catalog.yaml");
      if (body.empty() || body.size() > request.max_response_bytes) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::LimitExceeded,
                    "published catalog exceeds request limit",
                }};
      }
      return {.response = mobagen::http::Response{200, std::move(body)}};
    }

    mobagen::http::StreamGetResult get_stream(
        const mobagen::http::GetRequest& request, mobagen::http::BodySink sink
    ) override {
      ++artifact_requests;
      const auto prefix = base_url_ + '/';
      if (!request.url.starts_with(prefix) || sink.write == nullptr) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::InvalidRequest,
                    "unexpected artifact request",
                }};
      }
      const auto relative = std::filesystem::path{request.url.substr(prefix.size())};
      if (relative.is_absolute()
          || std::ranges::find(relative, std::filesystem::path{".."})
                 != relative.end()) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::InvalidRequest,
                    "unsafe artifact request",
                }};
      }
      auto body = read_bytes(root_ / relative);
      if (body.empty() || body.size() > request.max_response_bytes) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::LimitExceeded,
                    "published artifact exceeds request limit",
                }};
      }
      if (!sink.write(sink.context, body)) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::SinkRejected,
                    "module cache rejected published artifact",
                }};
      }
      return {.response = mobagen::http::StreamResponse{200, body.size()}};
    }

    std::size_t catalog_requests{};
    std::size_t artifact_requests{};

  private:
    std::filesystem::path root_;
    std::string base_url_;
  };

}  // namespace

TEST_CASE("Recommended project: first run installs cold plugins and second run is offline") {
  using namespace mobagen;
  TemporaryRecommendedProject temporary;
  const auto registry = temporary.path() / "registry";
  constexpr std::string_view base_url = "https://registry.mobagen.test";
  const tools::NativeCatalogPublishOptions publish_options{
      .output_root = registry,
      .base_url = std::string{base_url},
      .target = native_target(),
      .plugin_binaries = {
          MOBAGEN_DEFAULT_ASSET_STORE_PLUGIN_PATH,
          MOBAGEN_WINDOW_SDL3_PLUGIN_PATH,
          MOBAGEN_RENDER_WEBGPU_PLUGIN_PATH,
      },
  };
  REQUIRE(tools::publish_native_module_catalog(publish_options).ok());

  const auto project_root = temporary.path() / "game";
  const auto project_text = project_root.string();
  const auto source = std::string{base_url} + "/catalog.yaml";
  const std::vector<std::string_view> init_arguments{
      "init", project_text, "--name", "recommended-e2e", "--source", source,
  };
  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(compositions::cli::run(init_arguments, output, error, {}) == 0);
  REQUIRE(error.str().empty());
  const auto manifest = project_root / "mobagen.yaml";
  const compositions::ProjectBootstrapOptions options{
      .resolver = {.target = native_target(), .profile = "development"},
  };
  PublishedCatalogClient online{registry, std::string{base_url}};

  auto first = compositions::prepare_and_open_project(
      manifest, options, {.http_client = &online}
  );

  if (!first.bootstrap.issues.empty()) INFO(first.bootstrap.issues.front().message);
  if (!first.project.issues.empty()) INFO(first.project.issues.front().message);
  REQUIRE(first.ok());
  CHECK(first.bootstrap.state
        == compositions::ProjectBootstrapState::Synchronized);
  CHECK(first.bootstrap.plugin_count == 3);
  CHECK(first.bootstrap.selected_count == 3);
  CHECK(online.catalog_requests == 1);
  CHECK(online.artifact_requests == 3);
  REQUIRE(std::filesystem::is_regular_file(project_root / "mobagen.lock"));

  const auto plugin_root = project_root / ".mobagen" / "plugins";
  const auto binary_name = modules::module_plugin_binary_filename(
      modules::LinkageMode::Dynamic
  );
  const auto asset_binary = plugin_root / "mobagen.assets.default.plugin"
                            / binary_name;
  const auto window_binary = plugin_root / "mobagen.window.sdl3.plugin"
                             / binary_name;
  const auto render_binary = plugin_root / "mobagen.render.webgpu.plugin"
                             / binary_name;
  REQUIRE(std::filesystem::is_regular_file(asset_binary));
  REQUIRE(std::filesystem::is_regular_file(window_binary));
  REQUIRE(std::filesystem::is_regular_file(render_binary));
  CHECK(first.project.manager->active_count() == 0);
  CHECK_FALSE(native_library_loaded(asset_binary));
  CHECK_FALSE(native_library_loaded(window_binary));
  CHECK_FALSE(native_library_loaded(render_binary));

  auto assets = first.project.manager->acquire(
      MOBAGEN_ASSET_STORE_V1_ID, MOBAGEN_ASSET_STORE_V1_ABI_VERSION
  );
  REQUIRE(assets.ok());
  REQUIRE(assets.endpoint->native.has_value());
  CHECK(first.project.manager->active_count() == 1);
  CHECK(native_library_loaded(asset_binary));
  CHECK_FALSE(native_library_loaded(window_binary));
  CHECK_FALSE(native_library_loaded(render_binary));
  const auto* cached_endpoint = first.project.manager->find_active(
      MOBAGEN_ASSET_STORE_V1_ID, MOBAGEN_ASSET_STORE_V1_ABI_VERSION
  );
  REQUIRE(cached_endpoint != nullptr);
  CHECK(cached_endpoint->native->function_table
        == assets.endpoint->native->function_table);
  REQUIRE(first.project.manager->stop().ok());
  first.project.manager.reset();
  CHECK_FALSE(native_library_loaded(asset_binary));

  const auto lock_before = read_bytes(project_root / "mobagen.lock");
  auto second = compositions::prepare_and_open_project(manifest, options, {});

  if (!second.bootstrap.issues.empty()) INFO(second.bootstrap.issues.front().message);
  if (!second.project.issues.empty()) INFO(second.project.issues.front().message);
  REQUIRE(second.ok());
  CHECK(second.bootstrap.state == compositions::ProjectBootstrapState::Ready);
  CHECK(second.project.manager->active_count() == 0);
  CHECK_FALSE(native_library_loaded(asset_binary));
  CHECK(read_bytes(project_root / "mobagen.lock") == lock_before);
  REQUIRE(second.project.manager
              ->acquire(
                  MOBAGEN_ASSET_STORE_V1_ID,
                  MOBAGEN_ASSET_STORE_V1_ABI_VERSION
              )
              .ok());
  CHECK(second.project.manager->active_count() == 1);
  REQUIRE(second.project.manager->stop().ok());
}
