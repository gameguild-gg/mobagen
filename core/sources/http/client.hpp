#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::http {

  enum class ErrorCode : std::uint8_t {
    InvalidRequest,
    Resolve,
    Connect,
    Tls,
    Timeout,
    LimitExceeded,
    SinkRejected,
    Transfer,
  };

  struct Error {
    ErrorCode code{};
    std::string message;
  };

  struct GetRequest {
    std::string url;
    std::size_t max_response_bytes{};
    std::chrono::milliseconds connect_timeout{};
    std::chrono::milliseconds transfer_timeout{};
    std::uint32_t max_redirects{};
  };

  struct Response {
    std::uint16_t status{};
    std::vector<std::byte> body;
  };

  struct GetResult {
    std::optional<Response> response;
    std::optional<Error> error;

    [[nodiscard]] bool ok() const noexcept { return response.has_value() && !error.has_value(); }
  };

  struct BodySink {
    void* context{};
    bool (*write)(void* context, std::span<const std::byte> bytes) noexcept{};
  };

  struct StreamResponse {
    std::uint16_t status{};
    std::size_t body_bytes{};
  };

  struct StreamGetResult {
    std::optional<StreamResponse> response;
    std::optional<Error> error;

    [[nodiscard]] bool ok() const noexcept { return response.has_value() && !error.has_value(); }
  };

  class Client {
  public:
    virtual ~Client() = default;
    [[nodiscard]] virtual GetResult get(const GetRequest& request) = 0;

    [[nodiscard]] virtual StreamGetResult get_stream(const GetRequest& request, BodySink sink) {
      if (sink.write == nullptr || request.max_response_bytes == 0) {
        return {.error = Error{ErrorCode::InvalidRequest, "HTTP stream sink or response limit is invalid"}};
      }
      auto fetched = get(request);
      if (!fetched.ok()) {
        return {.error = fetched.error.value_or(Error{ErrorCode::Transfer, "HTTP client returned no response"})};
      }
      if (fetched.response->body.size() > request.max_response_bytes) {
        return {.error = Error{ErrorCode::LimitExceeded, "HTTP response exceeded its byte limit"}};
      }
      if (!fetched.response->body.empty() && !sink.write(sink.context, fetched.response->body)) {
        return {.error = Error{ErrorCode::SinkRejected, "HTTP response sink rejected bytes"}};
      }
      return {.response = StreamResponse{fetched.response->status, fetched.response->body.size()}};
    }
  };

}  // namespace mobagen::http
