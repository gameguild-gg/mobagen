#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "plugins/plugin_abi.h"

extern "C" int mobagen_plugin_abi_c_compile_test(void);

namespace {

  struct LegacyMobagenPluginDescriptorV1 {
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    MobagenStringView id;
    std::uint32_t version_major;
    std::uint32_t version_minor;
    std::uint32_t version_patch;
    MobagenReloadPolicy reload_policy;
    void* plugin_state;
    const MobagenStringView* provides;
    std::uint32_t provides_count;
    const MobagenStringView* required;
    std::uint32_t required_count;
    const MobagenStringView* optional;
    std::uint32_t optional_count;
    const MobagenStringView* conflicts;
    std::uint32_t conflicts_count;
    MobagenPluginLifecycleV1 lifecycle;
  };

}  // namespace

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
  CHECK(MOBAGEN_PLUGIN_DESCRIPTOR_V1_BASE_SIZE == sizeof(LegacyMobagenPluginDescriptorV1));
  CHECK(MOBAGEN_PLUGIN_DESCRIPTOR_V1_BASE_SIZE == offsetof(MobagenPluginDescriptorV1, configuration_schema));
  CHECK(MOBAGEN_PLUGIN_DESCRIPTOR_V1_BASE_SIZE < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE);
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
