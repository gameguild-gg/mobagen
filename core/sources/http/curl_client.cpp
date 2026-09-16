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

    struct CurlStreamState {
      BodySink sink;
      std::size_t limit{};
      std::size_t received{};
      bool limit_exceeded{};
      bool sink_rejected{};
    };

    struct ResponseBuffer {
      std::vector<std::byte> bytes;
      bool allocation_failed{};
    };

    CurlGlobalState& global_state() {
      static CurlGlobalState state;
      return state;
    }

    std::size_t write_body(char* contents, std::size_t size, std::size_t count, void* state_pointer) noexcept {
      auto& state = *static_cast<CurlStreamState*>(state_pointer);
      if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
        state.limit_exceeded = true;
        return 0;
      }
      const auto byte_count = size * count;
      if (byte_count == 0) return 0;
      if (byte_count > state.limit - state.received) {
        state.limit_exceeded = true;
        return 0;
      }
      const auto* begin = reinterpret_cast<const std::byte*>(contents);
      if (!state.sink.write(state.sink.context, {begin, byte_count})) {
        state.sink_rejected = true;
        return 0;
      }
      state.received += byte_count;
      return byte_count;
    }

    bool append_response_bytes(void* context, std::span<const std::byte> bytes) noexcept {
      auto& buffer = *static_cast<ResponseBuffer*>(context);
      try {
        buffer.bytes.insert(buffer.bytes.end(), bytes.begin(), bytes.end());
      } catch (...) {
        buffer.allocation_failed = true;
        return false;
      }
      return true;
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
    ResponseBuffer body;
    try {
      body.bytes.reserve(std::min<std::size_t>(request.max_response_bytes, 64U * 1024U));
    } catch (...) {
      return failure(ErrorCode::Transfer, "HTTP response buffer allocation failed");
    }
    auto streamed = get_stream(request, {.context = &body, .write = append_response_bytes});
    if (!streamed.ok()) {
      if (body.allocation_failed) return failure(ErrorCode::Transfer, "HTTP response buffer allocation failed");
      return {.error = std::move(streamed.error)};
    }
    return {.response = Response{streamed.response->status, std::move(body.bytes)}};
  }

  StreamGetResult CurlClient::get_stream(const GetRequest& request, BodySink sink) {
    if (!request.url.starts_with("https://") || request.url.contains('#') || request.url.contains('@')) {
      return {.error = Error{ErrorCode::InvalidRequest, "HTTP client accepts credential-free HTTPS URLs only"}};
    }
    constexpr auto max_curl_file_size = static_cast<std::uintmax_t>(std::numeric_limits<curl_off_t>::max());
    if (sink.write == nullptr || request.max_response_bytes == 0 || static_cast<std::uintmax_t>(request.max_response_bytes) > max_curl_file_size
        || request.connect_timeout.count() <= 0 || request.transfer_timeout.count() <= 0 || request.connect_timeout.count() > LONG_MAX
        || request.transfer_timeout.count() > LONG_MAX || request.max_redirects > max_supported_redirects) {
      return {.error = Error{ErrorCode::InvalidRequest, "HTTP stream sink, limits, or timeouts are invalid"}};
    }

    auto& global = global_state();
    if (global.result != CURLE_OK) {
      return {.error = Error{ErrorCode::Transfer, "libcurl global initialization failed"}};
    }

    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle handle{curl_easy_init(), curl_easy_cleanup};
    if (!handle) {
      return {.error = Error{ErrorCode::Transfer, "libcurl request allocation failed"}};
    }
    CurlStreamState stream{.sink = sink, .limit = request.max_response_bytes};
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
    set_option(CURLOPT_WRITEDATA, &stream);
    set_option(CURLOPT_ERRORBUFFER, error_buffer.data());
    set_option(CURLOPT_USERAGENT, "MobagenModuleManager/1");
    if (option_result != CURLE_OK) {
      return {.error = Error{ErrorCode::Transfer, curl_easy_strerror(option_result)}};
    }

    const auto transfer = curl_easy_perform(handle.get());
    if (stream.limit_exceeded) {
      return {.error = Error{ErrorCode::LimitExceeded, "HTTP response exceeded its byte limit"}};
    }
    if (stream.sink_rejected) {
      return {.error = Error{ErrorCode::SinkRejected, "HTTP response sink rejected bytes"}};
    }
    if (transfer == CURLE_FILESIZE_EXCEEDED) {
      return {.error = Error{ErrorCode::LimitExceeded, "HTTP response exceeded its declared byte limit"}};
    }
    if (transfer != CURLE_OK) {
      const std::string message = error_buffer.front() == '\0' ? curl_easy_strerror(transfer) : error_buffer.data();
      return {.error = Error{map_error(transfer), message}};
    }

    long status = 0;
    const auto info = curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (info != CURLE_OK || status < 0 || status > std::numeric_limits<std::uint16_t>::max()) {
      return {.error = Error{ErrorCode::Transfer, "HTTP response status is unavailable or invalid"}};
    }
    return {.response = StreamResponse{static_cast<std::uint16_t>(status), stream.received}};
  }

}  // namespace mobagen::http
