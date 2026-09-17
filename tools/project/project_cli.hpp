#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace mobagen::plugins {
  class PortableWasmBackend;
}

namespace mobagen::http {
  class Client;
}

namespace mobagen::compositions::cli {

  struct ProjectCliServices {
    plugins::PortableWasmBackend* portable_backend{};
    http::Client* http_client{};
  };

  [[nodiscard]] int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error);
  [[nodiscard]] int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error, ProjectCliServices services);

}  // namespace mobagen::compositions::cli
