#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "plugins/plugin_abi.h"

extern "C" int mobagen_plugin_abi_c_compile_test(void);

TEST_CASE("Plugin ABI: public contract compiles as C and C++") {
  static_assert(std::is_standard_layout_v<MobagenStringView>);
  static_assert(std::is_trivially_copyable_v<MobagenStringView>);
  static_assert(std::is_standard_layout_v<MobagenHostApiV1>);
  static_assert(std::is_trivially_copyable_v<MobagenHostApiV1>);
  static_assert(std::is_standard_layout_v<MobagenPluginLifecycleV1>);
  static_assert(std::is_trivially_copyable_v<MobagenPluginLifecycleV1>);
  static_assert(std::is_standard_layout_v<MobagenPluginDescriptorV1>);
  static_assert(std::is_trivially_copyable_v<MobagenPluginDescriptorV1>);

  CHECK(MOBAGEN_PLUGIN_ABI_VERSION == 1U);
  CHECK(MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE == sizeof(MobagenPluginDescriptorV1));
  CHECK(mobagen_plugin_abi_c_compile_test() == 0);
}

TEST_CASE("Plugin ABI: extensible structures begin with size and version") {
  CHECK(offsetof(MobagenHostApiV1, struct_size) == 0);
  CHECK(offsetof(MobagenHostApiV1, abi_version) == sizeof(std::uint32_t));
  CHECK(offsetof(MobagenPluginDescriptorV1, struct_size) == 0);
  CHECK(offsetof(MobagenPluginDescriptorV1, abi_version) == sizeof(std::uint32_t));
  CHECK(offsetof(MobagenPluginLifecycleV1, struct_size) == 0);
}

TEST_CASE("Plugin ABI: exported entry point has one canonical symbol") {
  CHECK(std::string{MOBAGEN_PLUGIN_ENTRY_V1_SYMBOL} == "mobagen_plugin_entry_v1");
}
