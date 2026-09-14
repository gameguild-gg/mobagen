#include "composition.hpp"

#include <iostream>

int main() {
  using namespace mobagen::modules;

  const ProductDescriptor product{
      .name = "minimal-headless",
      .modules = {{.alias = "runtime", .provider = "default"}},
      .profiles = {{.name = "release", .linkage = LinkageMode::Static, .editor = false}},
  };
  auto result = mobagen::compositions::create_headless_composition(product, mobagen::compositions::native_target_platform(), "release");
  if (!result.ok() || !result.composition->tick()) return 1;
  std::cout << "headless-ticks: " << result.composition->ticks() << '\n';
  return result.composition->stop().ok() ? 0 : 1;
}
