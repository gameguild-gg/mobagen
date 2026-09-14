#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "assets/asset_cache.hpp"
#include "modules/artifact_fetcher.hpp"
#include "modules/catalog_index.hpp"
#include "modules/resolver.hpp"

namespace {

  class TemporaryArtifactCache {
  public:
    TemporaryArtifactCache() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-module-artifacts-" + std::to_string(ticks) + "-"
                 + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryArtifactCache() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  class StreamingArtifactClient final : public mobagen::http::Client {
  public:
    mobagen::http::GetResult get(const mobagen::http::GetRequest&) override {
      ++buffered_calls;
      return {.error = mobagen::http::Error{mobagen::http::ErrorCode::Transfer, "buffered GET is forbidden"}};
    }

    mobagen::http::StreamGetResult get_stream(const mobagen::http::GetRequest& request,
                                              mobagen::http::BodySink sink) override {
      ++stream_calls;
      requests.push_back(request);
      if (transport_error.has_value()) return {.error = transport_error};
      if (status != 200) return {.response = mobagen::http::StreamResponse{status, 0}};
      const auto middle = body.size() / 2;
      if (middle != 0 && !sink.write(sink.context, std::span{body}.first(middle))) {
        return {.error = mobagen::http::Error{mobagen::http::ErrorCode::SinkRejected, "sink rejected first chunk"}};
      }
      const auto remaining = std::span{body}.subspan(middle);
      if (!remaining.empty() && !sink.write(sink.context, remaining)) {
        return {.error = mobagen::http::Error{mobagen::http::ErrorCode::SinkRejected, "sink rejected second chunk"}};
      }
      return {.response = mobagen::http::StreamResponse{status, body.size()}};
    }

    std::vector<std::byte> body;
    std::optional<mobagen::http::Error> transport_error;
    std::uint16_t status{200};
    std::size_t buffered_calls{};
    std::size_t stream_calls{};
    std::vector<mobagen::http::GetRequest> requests;
  };

  struct TestArtifactPlan {
    std::unique_ptr<mobagen::modules::ModuleCatalogIndex> catalog;
    mobagen::modules::ModuleResolution resolution;
  };

  TestArtifactPlan make_plan(std::string hash, std::uint64_t size, std::uint32_t abi_version = 1) {
    using namespace mobagen::modules;
    ModuleCatalogDescriptor catalog{
        .providers = {{
            .provider = {
                .id = "mobagen.runtime.remote",
                .version = {2, 1, 0},
                .provides = {"runtime.tick.v1"},
                .targets = {TargetPlatform::Windows},
                .linkages = {LinkageMode::Dynamic},
                .reload = ReloadPolicy::Restart,
            },
            .artifacts = {{
                .target = TargetPlatform::Windows,
                .linkage = LinkageMode::Dynamic,
                .abi_version = abi_version,
                .url = "https://plugins.mobagen.dev/mobagen.runtime.remote/2.1.0/windows.plugin",
                .size = size,
                .hash = std::move(hash),
            }},
        }},
    };
    auto indexed = build_module_catalog_index(std::span{&catalog, 1}, TargetPlatform::Windows,
                                              LinkageMode::Dynamic);
    REQUIRE(indexed.ok());
    ProductDescriptor product{
        .name = "artifact-fetch",
        .modules = {{.alias = "runtime", .provider = "default"}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Dynamic, .editor = false}},
    };
    ResolverOptions options{
        .target = TargetPlatform::Windows,
        .profile = "release",
        .aliases = {{.alias = "runtime", .capability = "runtime.tick.v1"}},
        .defaults = {{TargetPlatform::Windows, "release", "runtime.tick.v1", "mobagen.runtime.remote"}},
    };
    auto resolved = resolve_modules(product, indexed.index->registry(), options);
    REQUIRE(resolved.ok());
    return {std::move(indexed.index), std::move(*resolved.resolution)};
  }

  std::vector<std::byte> artifact_bytes(std::string_view text) {
    const auto source = std::as_bytes(std::span{text.data(), text.size()});
    return {source.begin(), source.end()};
  }

}  // namespace

TEST_CASE("Module artifact fetcher: selected plugins stream once into the content cache") {
  using namespace mobagen;
  TemporaryArtifactCache directory;
  assets::AssetCache cache(directory.path(), 1024);
  StreamingArtifactClient client;
  client.body = artifact_bytes("native-plugin-binary");
  const auto id = assets::sha256(client.body);
  REQUIRE(id.has_value());
  auto plan = make_plan(assets::to_string(*id), client.body.size());

  const auto fetched = modules::fetch_module_artifacts(*plan.catalog, plan.resolution, client, cache);

  REQUIRE(fetched.ok());
  REQUIRE(fetched.artifacts.size() == 1);
  CHECK(fetched.artifacts.front().provider_id == "mobagen.runtime.remote");
  CHECK(fetched.artifacts.front().abi_version == 1);
  CHECK(fetched.artifacts.front().downloaded);
  CHECK(fetched.artifacts.front().cache_path == cache.path_for(*id));
  CHECK(client.buffered_calls == 0);
  CHECK(client.stream_calls == 1);
  REQUIRE(client.requests.size() == 1);
  CHECK(client.requests.front().max_response_bytes == client.body.size());

  const auto cached = modules::fetch_module_artifacts(*plan.catalog, plan.resolution, client, cache);
  REQUIRE(cached.ok());
  REQUIRE(cached.artifacts.size() == 1);
  CHECK_FALSE(cached.artifacts.front().downloaded);
  CHECK(client.stream_calls == 1);

  auto unrelated = make_plan(assets::to_string(*id), client.body.size());
  const auto invalid = modules::fetch_module_artifacts(
      *plan.catalog, unrelated.resolution, client, cache
  );
  CHECK_FALSE(invalid.ok());
  REQUIRE(invalid.issues.size() == 1);
  CHECK(invalid.issues.front().code == modules::ArtifactFetchIssueCode::InvalidPlan);
  CHECK(client.stream_calls == 1);
}

TEST_CASE("Module artifact fetcher: hash and HTTP failures never publish an artifact") {
  using namespace mobagen;
  TemporaryArtifactCache directory;
  assets::AssetCache cache(directory.path(), 1024);
  StreamingArtifactClient client;
  client.body = artifact_bytes("tampered-plugin-data");
  const auto expected_bytes = artifact_bytes("expected-plugin-data");
  const auto expected = assets::sha256(expected_bytes);
  REQUIRE(expected.has_value());
  auto plan = make_plan(assets::to_string(*expected), client.body.size());

  const auto mismatched = modules::fetch_module_artifacts(*plan.catalog, plan.resolution, client, cache);

  CHECK_FALSE(mismatched.ok());
  CHECK(mismatched.artifacts.empty());
  REQUIRE(mismatched.issues.size() == 1);
  CHECK(mismatched.issues.front().code == modules::ArtifactFetchIssueCode::IntegrityMismatch);
  CHECK(cache.load(*expected).status == assets::AssetCacheStatus::not_found);

  client.status = 503;
  const auto unavailable = modules::fetch_module_artifacts(*plan.catalog, plan.resolution, client, cache);
  CHECK_FALSE(unavailable.ok());
  REQUIRE(unavailable.issues.size() == 1);
  CHECK(unavailable.issues.front().code == modules::ArtifactFetchIssueCode::HttpStatus);
  CHECK(cache.load(*expected).status == assets::AssetCacheStatus::not_found);

  client.status = 200;
  client.body = artifact_bytes("short");
  const auto truncated = modules::fetch_module_artifacts(*plan.catalog, plan.resolution, client, cache);
  CHECK_FALSE(truncated.ok());
  REQUIRE(truncated.issues.size() == 1);
  CHECK(truncated.issues.front().code == modules::ArtifactFetchIssueCode::SizeMismatch);

  client.transport_error = http::Error{http::ErrorCode::Timeout, "request timed out"};
  const auto transport = modules::fetch_module_artifacts(*plan.catalog, plan.resolution, client, cache);
  CHECK_FALSE(transport.ok());
  REQUIRE(transport.issues.size() == 1);
  CHECK(transport.issues.front().code == modules::ArtifactFetchIssueCode::Transport);
  CHECK(cache.load(*expected).status == assets::AssetCacheStatus::not_found);
}

TEST_CASE("Module artifact fetcher: incompatible plugin ABI fails before artifact HTTP") {
  using namespace mobagen;
  TemporaryArtifactCache directory;
  assets::AssetCache cache(directory.path(), 1024);
  StreamingArtifactClient client;
  client.body = artifact_bytes("future-plugin-abi");
  const auto id = assets::sha256(client.body);
  REQUIRE(id.has_value());
  auto plan = make_plan(assets::to_string(*id), client.body.size(), 2);

  const auto rejected = modules::fetch_module_artifacts(*plan.catalog, plan.resolution, client, cache);

  CHECK_FALSE(rejected.ok());
  REQUIRE(rejected.issues.size() == 1);
  CHECK(rejected.issues.front().code == modules::ArtifactFetchIssueCode::UnsupportedAbi);
  CHECK(client.stream_calls == 0);
  CHECK(modules::runtime_plugin_abi_version(modules::LinkageMode::Dynamic) == 1);
  CHECK(modules::runtime_plugin_abi_version(modules::LinkageMode::Wasm) == 1);
  CHECK(modules::runtime_plugin_abi_version(modules::LinkageMode::Static) == 0);
}
