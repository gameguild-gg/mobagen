#include "asset_cli.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string_view> arguments;
  arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
  for (int index = 1; index < argc; ++index) {
    arguments.emplace_back(argv[index] == nullptr ? "" : argv[index]);
  }
  return mobagen::assets::cli::run(arguments, std::cout, std::cerr);
}
