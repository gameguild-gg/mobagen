#include <doctest/doctest.h>

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "modules/module_sync_plan.hpp"

namespace {

  constexpr std::string_view catalog = R"yaml(schema: 1
providers:
  mobagen.runtime.tick:
    version: 1.0.0
    provides: [runtime.tick.v1]
    reload: restart
    artifacts:
      - target: windows
        linkage: dynamic
        abi: 1
        url: https://plugins.mobagen.dev/mobagen.runtime.tick/1.0.0/windows.plugin
        size: 4096
        hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
)yaml";

  class RecordingHttpClient final : public mobagen::http::Client {
  public:
    mobagen::http::GetResult get(const mobagen::http::GetRequest& request) override {
      requests.push_back(request);
      std::vector<std::byte> body(catalog.size());
      for (std::size_t index = 0; index < catalog.size(); ++index) body[index] = static_cast<std::byte>(catalog[index]);
      return {.response = mobagen::http::Response{200, std::move(body)}};
    }

    std::vector<mobagen::http::GetRequest> requests;
  };

  mobagen::modules::ProductDescriptor product() {
    using namespace mobagen::modules;
    return {
        .name = "remote-runtime",
        .sources = {{"official", "https://plugins.mobagen.dev/v1/catalog.yaml"}},
        .modules = {{.alias = "runtime", .provider = "default"}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Dynamic, .editor = false}},
    };
  }

  mobagen::modules::ResolverOptions options(std::string provider = "mobagen.runtime.tick") {
    using namespace mobagen::modules;
    return {
        .target = TargetPlatform::Windows,
        .profile = "release",
        .aliases = {{.alias = "runtime", .capability = "runtime.tick.v1"}},
        .defaults = {{TargetPlatform::Windows, "release", "runtime.tick.v1", std::move(provider)}},
    };
  }

}  // namespace

TEST_CASE("Module sync plan: resolves catalog metadata without fetching the selected artifact") {
  using namespace mobagen::modules;
  RecordingHttpClient client;

  const auto result = plan_module_sync(product(), client, options());

  REQUIRE(result.ok());
  REQUIRE(result.catalog != nullptr);
  REQUIRE(result.resolution.has_value());
  REQUIRE(client.requests.size() == 1);
  CHECK(client.requests.front().url == "https://plugins.mobagen.dev/v1/catalog.yaml");
  const auto provider = result.catalog->registry().find_provider("mobagen.runtime.tick");
  REQUIRE(provider.has_value());
  REQUIRE(result.resolution->lifecycle_order().size() == 1);
  CHECK(result.resolution->lifecycle_order().front() == *provider);
  const auto* artifact = result.catalog->artifact_for(*provider);
  REQUIRE(artifact != nullptr);
  CHECK(artifact->url == "https://plugins.mobagen.dev/mobagen.runtime.tick/1.0.0/windows.plugin");
}

TEST_CASE("Module sync plan: a resolution failure exposes no partial executable plan") {
  using namespace mobagen::modules;
  RecordingHttpClient client;

  const auto result = plan_module_sync(product(), client, options("customer.runtime.missing"));

  CHECK_FALSE(result.ok());
  CHECK(result.catalog == nullptr);
  CHECK_FALSE(result.resolution.has_value());
  REQUIRE(result.resolution_issues.size() == 1);
  CHECK(result.resolution_issues.front().code == ResolutionIssueCode::UnknownProvider);
}

TEST_CASE("Module sync plan: an unknown profile fails before any network request") {
  using namespace mobagen::modules;
  RecordingHttpClient client;
  auto resolver_options = options();
  resolver_options.profile = "missing";

  const auto result = plan_module_sync(product(), client, resolver_options);

  CHECK_FALSE(result.ok());
  CHECK(client.requests.empty());
  REQUIRE(result.resolution_issues.size() == 1);
  CHECK(result.resolution_issues.front().code == ResolutionIssueCode::UnknownProfile);
}
