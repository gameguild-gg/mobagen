#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mobagen::http {

  enum class ErrorCode : std::uint8_t { InvalidRequest, Resolve, Connect, Tls, Timeout, LimitExceeded, Transfer };

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

  class Client {
  public:
    virtual ~Client() = default;
    [[nodiscard]] virtual GetResult get(const GetRequest& request) = 0;
  };

}  // namespace mobagen::http
