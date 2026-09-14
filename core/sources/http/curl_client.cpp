#include "curl_client.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif
#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace mobagen::http {
  namespace {

    constexpr std::uint32_t max_supported_redirects = 10;

    struct CurlGlobalState {
      CurlGlobalState() : result(curl_global_init(CURL_GLOBAL_DEFAULT)) {}
      ~CurlGlobalState() {
        if (result == CURLE_OK) curl_global_cleanup();
      }

      CURLcode result;
    };

    struct ResponseBuffer {
      std::vector<std::byte> bytes;
      std::size_t limit{};
      bool limit_exceeded{};
      bool allocation_failed{};
    };

    CurlGlobalState& global_state() {
      static CurlGlobalState state;
      return state;
    }

    std::size_t write_body(char* contents, std::size_t size, std::size_t count, void* state_pointer) noexcept {
      auto& state = *static_cast<ResponseBuffer*>(state_pointer);
      if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        state.limit_exceeded = true;
        return 0;
      }
      const auto byte_count = size * count;
      if (byte_count == 0) return 0;
      if (byte_count > state.limit - state.bytes.size()) {
        state.limit_exceeded = true;
        return 0;
      }
      try {
        const auto* begin = reinterpret_cast<const std::byte*>(contents);
        state.bytes.insert(state.bytes.end(), begin, begin + byte_count);
      } catch (...) {
        state.allocation_failed = true;
        return 0;
      }
      return byte_count;
    }

    ErrorCode map_error(CURLcode code) noexcept {
      switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
          return ErrorCode::Resolve;
        case CURLE_COULDNT_CONNECT:
          return ErrorCode::Connect;
        case CURLE_OPERATION_TIMEDOUT:
          return ErrorCode::Timeout;
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CONNECT_ERROR:
          return ErrorCode::Tls;
        default:
          return ErrorCode::Transfer;
      }
    }

    GetResult failure(ErrorCode code, std::string message) { return {.error = Error{code, std::move(message)}}; }

  }  // namespace

  GetResult CurlClient::get(const GetRequest& request) {
    if (!request.url.starts_with("https://") || request.url.contains('#') || request.url.contains('@')) {
      return failure(ErrorCode::InvalidRequest, "HTTP client accepts credential-free HTTPS URLs only");
    }
    constexpr auto max_curl_file_size = static_cast<std::uintmax_t>(std::numeric_limits<curl_off_t>::max());
    if (request.max_response_bytes == 0 || static_cast<std::uintmax_t>(request.max_response_bytes) > max_curl_file_size
        || request.connect_timeout.count() <= 0 || request.transfer_timeout.count() <= 0
        || request.connect_timeout.count() > LONG_MAX || request.transfer_timeout.count() > LONG_MAX
        || request.max_redirects > max_supported_redirects) {
      return failure(ErrorCode::InvalidRequest, "HTTP request limits or timeouts are invalid");
    }

    auto& global = global_state();
    if (global.result != CURLE_OK) return failure(ErrorCode::Transfer, "libcurl global initialization failed");

    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle handle{curl_easy_init(), curl_easy_cleanup};
    if (!handle) return failure(ErrorCode::Transfer, "libcurl request allocation failed");

    ResponseBuffer body{.limit = request.max_response_bytes};
    try {
      body.bytes.reserve(std::min<std::size_t>(request.max_response_bytes, 64U * 1024U));
    } catch (...) {
      return failure(ErrorCode::Transfer, "HTTP response buffer allocation failed");
    }
    std::array<char, CURL_ERROR_SIZE> error_buffer{};

    CURLcode option_result = CURLE_OK;
    const auto set_option = [&](CURLoption option, auto value) {
      if (option_result == CURLE_OK) option_result = curl_easy_setopt(handle.get(), option, value);
    };
    set_option(CURLOPT_URL, request.url.c_str());
    set_option(CURLOPT_PROTOCOLS_STR, "https");
    set_option(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    set_option(CURLOPT_FOLLOWLOCATION, request.max_redirects == 0 ? 0L : 1L);
    set_option(CURLOPT_MAXREDIRS, static_cast<long>(request.max_redirects));
    set_option(CURLOPT_SSL_VERIFYPEER, 1L);
    set_option(CURLOPT_SSL_VERIFYHOST, 2L);
    set_option(CURLOPT_NETRC, CURL_NETRC_IGNORED);
    set_option(CURLOPT_NOSIGNAL, 1L);
    set_option(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.connect_timeout.count()));
    set_option(CURLOPT_TIMEOUT_MS, static_cast<long>(request.transfer_timeout.count()));
    set_option(CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(request.max_response_bytes));
    set_option(CURLOPT_WRITEFUNCTION, write_body);
    set_option(CURLOPT_WRITEDATA, &body);
    set_option(CURLOPT_ERRORBUFFER, error_buffer.data());
    set_option(CURLOPT_USERAGENT, "MobagenModuleManager/1");
    if (option_result != CURLE_OK) return failure(ErrorCode::Transfer, curl_easy_strerror(option_result));

    const auto transfer = curl_easy_perform(handle.get());
    if (body.limit_exceeded) return failure(ErrorCode::LimitExceeded, "HTTP response exceeded its byte limit");
    if (body.allocation_failed) return failure(ErrorCode::Transfer, "HTTP response buffer allocation failed");
    if (transfer == CURLE_FILESIZE_EXCEEDED) {
      return failure(ErrorCode::LimitExceeded, "HTTP response exceeded its declared byte limit");
    }
    if (transfer != CURLE_OK) {
      const std::string message = error_buffer.front() == '\0' ? curl_easy_strerror(transfer) : error_buffer.data();
      return failure(map_error(transfer), message);
    }

    long status = 0;
    const auto info = curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (info != CURLE_OK || status < 0 || status > std::numeric_limits<std::uint16_t>::max()) {
      return failure(ErrorCode::Transfer, "HTTP response status is unavailable or invalid");
    }
    return {.response = Response{static_cast<std::uint16_t>(status), std::move(body.bytes)}};
  }

}  // namespace mobagen::http
