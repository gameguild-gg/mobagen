#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace mobagen::plugins {
  class PortableWasmBackend;
}

namespace mobagen::plugins::cli {

  struct PluginCliServices {
    PortableWasmBackend* portable_backend{};
  };

  [[nodiscard]] int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error);
  [[nodiscard]] int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error, PluginCliServices services);

}  // namespace mobagen::plugins::cli
