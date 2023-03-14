//#ifndef MOBAGEN_WEBREQUEST_H
//#define MOBAGEN_WEBREQUEST_H
//
//#include <memory>
//#include <string>
//#include <map>
//
// enum class WebRequestVerb { GET, POST, PUT };
//
// enum class ErrorCode {
//  OK = 0,
//  CONNECTION_FAILURE,
//  EMPTY_RESPONSE,
//  HOST_RESOLUTION_FAILURE,
//  INTERNAL_ERROR,
//  INVALID_URL_FORMAT,
//  NETWORK_RECEIVE_ERROR,
//  NETWORK_SEND_FAILURE,
//  OPERATION_TIMEDOUT,
//  PROXY_RESOLUTION_FAILURE,
//  SSL_CONNECT_ERROR,
//  SSL_LOCAL_CERTIFICATE_ERROR,
//  SSL_REMOTE_CERTIFICATE_ERROR,
//  SSL_CACERT_ERROR,
//  GENERIC_SSL_ERROR,
//  UNSUPPORTED_PROTOCOL,
//  REQUEST_CANCELLED,
//  TOO_MANY_REDIRECTS,
//  UNKNOWN_ERROR = 1000,
//};
//
// struct WebResponse {
// public:
//  uint32_t status_code;
//  std::string text;
//  std::map<std::string, std::string> header;
//  std::string url;
//  // todo: cookies
//  ErrorCode errorCode;
//  std::string ErrorMessage;
//  std::string raw_header{};
//  std::string status_line{};
//  std::string reason{};
//  // todo: uploaded_bytes{};
//  // todo: downloaded_bytes{};
//};
//
// struct WebRequest {
//  WebRequest(std::string uri, WebRequestVerb verb, std::map<std::string, std::string> headers);
//};
//
//#endif  // MOBAGEN_WEBREQUEST_H
