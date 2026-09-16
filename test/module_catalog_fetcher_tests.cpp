#include <doctest/doctest.h>

#include <cstddef>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "modules/catalog_fetcher.hpp"

namespace {

  constexpr std::string_view valid_catalog = R"yaml(schema: 1
providers: {}
)yaml";

  class FakeHttpsClient final : public mobagen::http::Client {
  public:
    mobagen::http::GetResult get(const mobagen::http::GetRequest& request) override {
      requests.push_back(request);
      if (results.empty()) return {.error = mobagen::http::Error{mobagen::http::ErrorCode::Transfer, "no fake response"}};
      auto result = std::move(results.front());
      results.pop_front();
      return result;
    }

    std::vector<mobagen::http::GetRequest> requests;
    std::deque<mobagen::http::GetResult> results;
  };

  mobagen::http::GetResult response(std::string_view body, std::uint16_t status = 200) {
    std::vector<std::byte> bytes(body.size());
    for (std::size_t index = 0; index < body.size(); ++index) bytes[index] = static_cast<std::byte>(body[index]);
    return {.response = mobagen::http::Response{status, std::move(bytes)}};
  }

  mobagen::modules::ProductDescriptor sourced_product() {
    return {
        .name = "source-fetch",
        .sources = {{"official", "https://plugins.mobagen.dev/catalog.yaml"}, {"customer", "https://plugins.example.com/catalog.yaml"}},
    };
  }

}  // namespace

TEST_CASE("Module catalog fetcher: source catalogs are fetched over a bounded HTTPS contract") {
  using namespace mobagen;

  FakeHttpsClient client;
  client.results.push_back(response(valid_catalog));
  client.results.push_back(response(valid_catalog));

  const auto result = modules::fetch_module_catalogs(sourced_product(), client);

  REQUIRE(result.ok());
  REQUIRE(result.catalogs.size() == 2);
  CHECK(result.catalogs[0].source.name == "official");
  CHECK(result.catalogs[1].source.name == "customer");
  REQUIRE(client.requests.size() == 2);
  CHECK(client.requests[0].url == "https://plugins.mobagen.dev/catalog.yaml");
  CHECK(client.requests[0].max_response_bytes == modules::max_module_catalog_bytes);
  CHECK(client.requests[0].max_redirects == 0);
  CHECK(client.requests[0].connect_timeout.count() == 5000);
  CHECK(client.requests[0].transfer_timeout.count() == 30000);
}

TEST_CASE("Module catalog fetcher: any transport, status, size, or parse failure is transactional") {
  using namespace mobagen;

  SUBCASE("transport failure") {
    FakeHttpsClient client;
    client.results.push_back({.error = http::Error{http::ErrorCode::Tls, "certificate rejected"}});
    const auto result = modules::fetch_module_catalogs(sourced_product(), client);
    CHECK_FALSE(result.ok());
    CHECK(result.catalogs.empty());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().code == modules::CatalogFetchIssueCode::Transport);
  }

  SUBCASE("HTTP status failure") {
    FakeHttpsClient client;
    client.results.push_back(response({}, 404));
    const auto result = modules::fetch_module_catalogs(sourced_product(), client);
    CHECK_FALSE(result.ok());
    CHECK(result.catalogs.empty());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().code == modules::CatalogFetchIssueCode::HttpStatus);
  }

  SUBCASE("client cannot bypass the response limit") {
    FakeHttpsClient client;
    client.results.push_back(response(std::string(modules::max_module_catalog_bytes + 1, 'x')));
    const auto result = modules::fetch_module_catalogs(sourced_product(), client);
    CHECK_FALSE(result.ok());
    CHECK(result.catalogs.empty());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().code == modules::CatalogFetchIssueCode::LimitExceeded);
  }

  SUBCASE("catalog parse failure") {
    FakeHttpsClient client;
    client.results.push_back(response("schema: 1\nproviders: [invalid]\n"));
    const auto result = modules::fetch_module_catalogs(sourced_product(), client);
    CHECK_FALSE(result.ok());
    CHECK(result.catalogs.empty());
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().code == modules::CatalogFetchIssueCode::InvalidCatalog);
    CHECK_FALSE(result.issues.front().catalog_errors.empty());
  }
}
