#include <doctest/doctest.h>

#include "project_cli.hpp"
#include "project_startup.hpp"

#include "http/client.hpp"
#include "modules/artifact_installer.hpp"
#include "support/wasm_plugin_test_support.hpp"

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

namespace {

  class TemporaryRecommendedProject {
  public:
    TemporaryRecommendedProject() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-recommended-e2e-" + std::to_string(ticks) + '-' + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryRecommendedProject() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    const auto characters = std::as_bytes(std::span{contents});
    return {characters.begin(), characters.end()};
  }

  /* Publishes every provider the recommended template selects as a portable
     wasm artifact; bootstrap only hashes and stages these bytes. */
  class PublishedCatalogClient final : public mobagen::http::Client {
  public:
    explicit PublishedCatalogClient(std::string base_url) : base_url_(std::move(base_url)) {}

    mobagen::http::GetResult get(const mobagen::http::GetRequest& request) override {
      ++catalog_requests;
      if (request.url != base_url_ + "/catalog.yaml") {
        return {.response = mobagen::http::Response{404, {}}};
      }
      const auto catalog = build_catalog();
      std::vector<std::byte> body(catalog.size());
      for (std::size_t index = 0; index < catalog.size(); ++index) {
        body[index] = static_cast<std::byte>(catalog[index]);
      }
      return {.response = mobagen::http::Response{200, std::move(body)}};
    }

    mobagen::http::StreamGetResult get_stream(const mobagen::http::GetRequest& request, mobagen::http::BodySink sink) override {
      ++artifact_requests;
      const auto prefix = base_url_ + '/';
      if (!request.url.starts_with(prefix) || sink.write == nullptr) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::InvalidRequest,
                    "unexpected artifact request",
                }};
      }
      const auto relative = std::filesystem::path{request.url.substr(prefix.size())};
      if (relative.is_absolute() || std::ranges::find(relative, std::filesystem::path{".."}) != relative.end()) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::InvalidRequest,
                    "unsafe artifact request",
                }};
      }
      if (!sink.write(sink.context, mobagen::test::valid_wasm_header)) {
        return {.error = mobagen::http::Error{
                    mobagen::http::ErrorCode::SinkRejected,
                    "module cache rejected published artifact",
                }};
      }
      return {.response = mobagen::http::StreamResponse{200, mobagen::test::valid_wasm_header.size()}};
    }

    std::size_t catalog_requests{};
    std::size_t artifact_requests{};

  private:
    std::string build_catalog() {
      const auto digest = mobagen::assets::sha256(mobagen::test::valid_wasm_header);
      REQUIRE(digest.has_value());
      const auto hash = mobagen::assets::to_string(*digest);
      const std::array<std::pair<std::string_view, std::string_view>, 3> providers{{
          {"mobagen.assets.default", "assets.store.v1"},
          {"mobagen.render.webgpu", "render.backend.v1"},
          {"mobagen.window.sdl3", "window.surface.v1"},
      }};
      std::string catalog = "schema: 1\nproviders:\n";
      for (const auto& [provider, capability] : providers) {
        catalog += "  ";
        catalog += provider;
        catalog += ":\n    version: 1.0.0\n    provides: [";
        catalog += capability;
        catalog += "]\n    artifacts:\n      - target: ";
        catalog += mobagen::test::portable_target_name();
        catalog += "\n        linkage: wasm\n        abi: 1\n        url: ";
        catalog += base_url_;
        catalog += '/';
        catalog += provider;
        catalog += "/1.0.0/reference.plugin\n        size: ";
        catalog += std::to_string(mobagen::test::valid_wasm_header.size());
        catalog += "\n        hash: ";
        catalog += hash;
        catalog += "\n";
      }
      return catalog;
    }

    std::string base_url_;
  };

}  // namespace

TEST_CASE("Recommended project: first run installs cold wasm plugins and second run is offline") {
  using namespace mobagen;
  TemporaryRecommendedProject temporary;
  const auto project_root = temporary.path() / "game";
  const auto project_text = project_root.string();
  constexpr std::string_view base_url = "https://registry.mobagen.test";
  const auto source = std::string{base_url} + "/catalog.yaml";
  const std::vector<std::string_view> init_arguments{
      "init", project_text, "--name", "recommended-e2e", "--source", source,
  };
  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(compositions::cli::run(init_arguments, output, error, {}) == 0);
  REQUIRE(error.str().empty());
  const auto manifest = project_root / "mobagen.yaml";

  mobagen::test::FakeWasmBackend backend;
  backend.provider_ids = {"mobagen.assets.default", "mobagen.render.webgpu", "mobagen.window.sdl3"};
  backend.capability_ids = {"assets.store.v1", "render.backend.v1", "window.surface.v1"};
  const compositions::ProjectBootstrapOptions options{
      .resolver = {.target = mobagen::test::portable_target(), .profile = "development"},
  };
  PublishedCatalogClient online{std::string{base_url}};

  auto first = compositions::prepare_and_open_project(manifest, options,
                                                      {.http_client = &online, .modules = {.portable_backend = &backend}});

  if (!first.bootstrap.issues.empty()) INFO(first.bootstrap.issues.front().message);
  if (!first.project.issues.empty()) INFO(first.project.issues.front().message);
  REQUIRE(first.ok());
  CHECK(first.bootstrap.state == compositions::ProjectBootstrapState::Synchronized);
  CHECK(first.bootstrap.plugin_count == 3);
  CHECK(online.catalog_requests == 1);
  CHECK(online.artifact_requests >= 1);
  REQUIRE(std::filesystem::is_regular_file(project_root / "mobagen.lock"));

  const auto lock_contents = mobagen::test::read_text(project_root / "mobagen.lock");
  CHECK(lock_contents.contains("linkage: wasm\n"));
  CHECK(lock_contents.find("linkage: dynamic") == std::string::npos);
  CHECK(first.project.manager->kind() == compositions::ProjectModuleRuntimeKind::Portable);
  CHECK(first.project.manager->active_count() == 0);
  CHECK(backend.calls == 0);

  const auto lock_before = read_bytes(project_root / "mobagen.lock");
  auto second = compositions::prepare_and_open_project(manifest, options, {.modules = {.portable_backend = &backend}});

  if (!second.bootstrap.issues.empty()) INFO(second.bootstrap.issues.front().message);
  if (!second.project.issues.empty()) INFO(second.project.issues.front().message);
  REQUIRE(second.ok());
  CHECK(second.bootstrap.state == compositions::ProjectBootstrapState::Ready);
  CHECK(second.project.manager->active_count() == 0);
  CHECK(read_bytes(project_root / "mobagen.lock") == lock_before);
  REQUIRE(second.project.manager->stop().ok());
}
