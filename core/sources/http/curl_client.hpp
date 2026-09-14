#pragma once

#include "client.hpp"

namespace mobagen::http {

  class CurlClient final : public Client {
  public:
    [[nodiscard]] GetResult get(const GetRequest& request) override;
  };

}  // namespace mobagen::http
