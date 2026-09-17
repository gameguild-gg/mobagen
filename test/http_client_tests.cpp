#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "http/client.hpp"

namespace {

  class BufferedHttpClient final : public mobagen::http::Client {
  public:
    mobagen::http::GetResult get(const mobagen::http::GetRequest&) override {
      ++calls;
      constexpr std::string_view text = "plugin-bytes";
      std::vector<std::byte> body(text.size());
      for (std::size_t index = 0; index < text.size(); ++index) body[index] = static_cast<std::byte>(text[index]);
      return {.response = mobagen::http::Response{200, std::move(body)}};
    }

    std::size_t calls{};
  };

  bool append_bytes(void* context, std::span<const std::byte> bytes) noexcept {
    auto& destination = *static_cast<std::vector<std::byte>*>(context);
    try {
      destination.insert(destination.end(), bytes.begin(), bytes.end());
    } catch (...) {
      return false;
    }
    return true;
  }

}  // namespace

TEST_CASE("HTTP client: buffered implementations inherit the bounded streaming contract") {
  using namespace mobagen::http;
  BufferedHttpClient client;
  std::vector<std::byte> streamed;
  GetRequest request{.url = "https://plugins.mobagen.dev/runtime.plugin", .max_response_bytes = 64};

  const auto result = client.get_stream(request, {.context = &streamed, .write = append_bytes});

  REQUIRE(result.ok());
  REQUIRE(result.response.has_value());
  CHECK(result.response->status == 200);
  CHECK(result.response->body_bytes == 12);
  CHECK(streamed.size() == 12);
  CHECK(client.calls == 1);
}

TEST_CASE("HTTP client: invalid and rejecting stream sinks fail explicitly") {
  using namespace mobagen::http;
  BufferedHttpClient client;
  GetRequest request{.url = "https://plugins.mobagen.dev/runtime.plugin", .max_response_bytes = 64};

  const auto invalid = client.get_stream(request, {});
  CHECK_FALSE(invalid.ok());
  REQUIRE(invalid.error.has_value());
  CHECK(invalid.error->code == ErrorCode::InvalidRequest);
  CHECK(client.calls == 0);

  const auto reject = [](void*, std::span<const std::byte>) noexcept { return false; };
  const auto rejected = client.get_stream(request, {.write = reject});
  CHECK_FALSE(rejected.ok());
  REQUIRE(rejected.error.has_value());
  CHECK(rejected.error->code == ErrorCode::SinkRejected);
  CHECK(client.calls == 1);

  std::vector<std::byte> streamed;
  request.max_response_bytes = 4;
  const auto oversized = client.get_stream(request, {.context = &streamed, .write = append_bytes});
  CHECK_FALSE(oversized.ok());
  REQUIRE(oversized.error.has_value());
  CHECK(oversized.error->code == ErrorCode::LimitExceeded);
  CHECK(streamed.empty());
  CHECK(client.calls == 2);
}
