#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "headless/composition.hpp"
#include "modules/lockfile.hpp"
#include "modules/manifest_parser.hpp"

TEST_CASE("Module composition: headless YAML resolves, ticks, locks, and stops end to end") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
name: minimal-headless
modules:
  runtime:
    use: default
plugins: []
profiles:
  release:
    linkage: static
    editor: false
)yaml";
  const auto manifest = parse_product_manifest(source);
  REQUIRE(manifest.ok());

  auto composition
      = mobagen::compositions::create_headless_composition(*manifest.descriptor, mobagen::compositions::native_target_platform(), "release");
  REQUIRE(composition.ok());
  REQUIRE(composition.composition != nullptr);
  CHECK(composition.composition->ticks() == 0);
  REQUIRE(composition.composition->tick());
  CHECK(composition.composition->ticks() == 1);

  const auto lockfile = serialize_lockfile(composition.composition->registry(), composition.composition->resolution(),
                                           {.sdk = {1, 0, 0}, .target = mobagen::compositions::native_target_platform(), .profile = "release"});
  REQUIRE(lockfile.ok());
#ifdef _WIN32
  constexpr std::string_view target = "windows";
#elif defined(__APPLE__)
  constexpr std::string_view target = "macos";
#else
  constexpr std::string_view target = "linux";
#endif
  const std::string expected = "schema: 1\nsdk: 1.0.0\ntarget: " + std::string(target)
                               + "\nprofile: release\nresolved:\n"
                                 "  runtime.tick.v1:\n"
                                 "    provider: mobagen.runtime.headless\n"
                                 "    version: 1.0.0\n"
                                 "    linkage: static\n"
                                 "dependencies: []\n"
                                 "plugins: {}\n";
  CHECK(*lockfile.contents == expected);

  const auto stopped = composition.composition->stop();
  CHECK(stopped.ok());
  CHECK_FALSE(composition.composition->tick());
}
