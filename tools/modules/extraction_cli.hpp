#pragma once

#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::modules::cli {

  [[nodiscard]] int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error);

}  // namespace mobagen::modules::cli
