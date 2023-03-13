#include "WebRequest.h"
#ifndef __EMSCRIPTEN__
#  include "cpr/cpr.h"
#endif

// ref.: see https://github.com/libcpr/cpr/blob/master/test/post_tests.cpp
WebRequest::WebRequest(std::string uri, WebRequestVerb verb = WebRequestVerb::GET, std::map<std::string, std::string> headers = {}) {
#ifndef __EMSCRIPTEN__
  cpr::Payload payload{};
  cpr::Session session;
  cpr::Header header;
  session.SetVerifySsl(false);  // todo: find a way to not pass verifyssl
  for (auto pair : headers) header.emplace(pair.first, pair.second);
  session.SetHeader(header);
  cpr::Response r;
  if (verb == WebRequestVerb::GET) {
    r = session.Get();
  }
#endif
}
