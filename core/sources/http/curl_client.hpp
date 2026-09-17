#pragma once

#include "client.hpp"

namespace mobagen::http {

  class CurlClient final : public Client {
  public:
    [[nodiscard]] GetResult get(const GetRequest& request) override;
    [[nodiscard]] StreamGetResult get_stream(const GetRequest& request, BodySink sink) override;
  };

}  // namespace mobagen::http
