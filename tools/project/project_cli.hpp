#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace mobagen::compositions::cli {

  [[nodiscard]] int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error);

}  // namespace mobagen::compositions::cli
