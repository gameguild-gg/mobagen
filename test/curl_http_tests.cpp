#include <doctest/doctest.h>

#include <chrono>

#include "http/curl_client.hpp"

TEST_CASE("Curl HTTP client: invalid or insecure requests fail before network access") {
  using namespace std::chrono_literals;
  using namespace mobagen::http;

  CurlClient client;
  GetRequest valid_limits{
      .url = "http://plugins.mobagen.dev/catalog.yaml",
      .max_response_bytes = 1024,
      .connect_timeout = 1s,
      .transfer_timeout = 2s,
  };

  auto insecure = client.get(valid_limits);
  REQUIRE_FALSE(insecure.ok());
  REQUIRE(insecure.error.has_value());
  CHECK(insecure.error->code == ErrorCode::InvalidRequest);

  valid_limits.url = "https://plugins.mobagen.dev/catalog.yaml";
  valid_limits.max_response_bytes = 0;
  auto unbounded = client.get(valid_limits);
  REQUIRE_FALSE(unbounded.ok());
  REQUIRE(unbounded.error.has_value());
  CHECK(unbounded.error->code == ErrorCode::InvalidRequest);

  valid_limits.max_response_bytes = 1024;
  const auto missing_sink = client.get_stream(valid_limits, {});
  REQUIRE_FALSE(missing_sink.ok());
  REQUIRE(missing_sink.error.has_value());
  CHECK(missing_sink.error->code == ErrorCode::InvalidRequest);
}
