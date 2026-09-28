#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <type_traits>

#include <mobagen/module/module_abi.h>

extern "C" int mobagen_module_abi_c_compile_test(void);

namespace {

  MobagenModuleStatus MOBAGEN_MODULE_CALL cpp_compute(std::uint32_t a, std::uint32_t b, std::uint32_t* out) {
    if (out == nullptr) return MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT;
    *out = a * b;
    return MOBAGEN_MODULE_STATUS_OK;
  }

  std::uint64_t MOBAGEN_MODULE_CALL cpp_tick_count(void) { return 42; }

}  // namespace

MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(MobagenCppReferenceExports, 2)
MOBAGEN_MODULE_EXPORT_ENTRY(MobagenCppReferenceExports, cpp_compute, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                            MOBAGEN_MODULE_T_PTR)
MOBAGEN_MODULE_EXPORT_ENTRY(MobagenCppReferenceExports, cpp_tick_count, MOBAGEN_MODULE_T_I64)
MOBAGEN_MODULE_EXPORT_TABLE_END(MobagenCppReferenceExports, cpp_compute, cpp_tick_count);

/* constexpr meta (C++ side) agrees with the annotated encoding. */
static_assert(mobagen::module::SignatureIdV1{MOBAGEN_MODULE_SIG_3(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                                                                  MOBAGEN_MODULE_T_PTR)}
                      .return_code()
                  == MOBAGEN_MODULE_T_I32,
              "meta return");
static_assert(mobagen::module::SignatureIdV1{MOBAGEN_MODULE_SIG_3(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                                                                  MOBAGEN_MODULE_T_PTR)}
                      .param_count()
                  == 3,
              "meta param count");
static_assert(mobagen::module::SignatureIdV1{MOBAGEN_MODULE_SIG_3(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                                                                  MOBAGEN_MODULE_T_PTR)}
                      .param_code(2)
                  == MOBAGEN_MODULE_T_PTR,
              "meta param 2");
static_assert(mobagen::module::SignatureIdV1{MOBAGEN_MODULE_SIG_3(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                                                                  MOBAGEN_MODULE_T_PTR)}
                  .valid(),
              "meta valid");
static_assert(!mobagen::module::SignatureIdV1{0x12345678U}.valid(), "meta invalid");

TEST_CASE("Module ABI: versioned structures are fixed-width and self-describing") {
  static_assert(std::is_standard_layout_v<MobagenModuleSpan32>);
  static_assert(std::is_trivially_copyable_v<MobagenModuleSpan32>);
  static_assert(std::is_standard_layout_v<MobagenModuleExportEntryV1>);
  static_assert(std::is_trivially_copyable_v<MobagenModuleExportEntryV1>);
  static_assert(std::is_standard_layout_v<MobagenModuleExportTableV1>);
  static_assert(std::is_trivially_copyable_v<MobagenModuleExportTableV1>);
  static_assert(std::is_standard_layout_v<MobagenModuleHostApiV1>);
  static_assert(std::is_trivially_copyable_v<MobagenModuleHostApiV1>);
  static_assert(std::is_standard_layout_v<MobagenModuleDescriptorV1>);
  static_assert(std::is_trivially_copyable_v<MobagenModuleDescriptorV1>);

  CHECK(MOBAGEN_MODULE_ABI_VERSION == 1U);
  CHECK(MOBAGEN_MODULE_HOST_API_V1_SIZE == sizeof(MobagenModuleHostApiV1));
  CHECK(MOBAGEN_MODULE_DESCRIPTOR_V1_SIZE == sizeof(MobagenModuleDescriptorV1));
  CHECK(offsetof(MobagenModuleHostApiV1, struct_size) == 0U);
  CHECK(offsetof(MobagenModuleHostApiV1, abi_version) == sizeof(std::uint32_t));
  CHECK(offsetof(MobagenModuleDescriptorV1, struct_size) == 0U);
  CHECK(offsetof(MobagenModuleDescriptorV1, abi_version) == sizeof(std::uint32_t));
}

TEST_CASE("Module ABI: entry and threading-contract symbols are the documented constants") {
  CHECK(std::string{MOBAGEN_MODULE_ENTRY_V1_SYMBOL} == "mobagen_module_entry_v1");
  CHECK(std::string{MOBAGEN_MODULE_TABLE_EXPORT_SYMBOL_V1} == "mobagen_module_exports_v1");
  CHECK(std::string{MOBAGEN_MODULE_IMPORT_MODULE_V1} == "mobagen_module_v1");
  CHECK(std::string{MOBAGEN_MODULE_THREAD_SPAWN_IMPORT_V1} == "mobagen_thread_spawn_v1");
  CHECK(std::string{MOBAGEN_MODULE_THREAD_QUIESCE_EXPORT_V1} == "mobagen_module_thread_quiesce_v1");
  CHECK(MOBAGEN_MODULE_THREADS_NONE == 0U);
  CHECK(MOBAGEN_MODULE_THREADS_MANAGED == 1U);
  CHECK(MOBAGEN_MODULE_MAX_EXPORT_PARAMS == 5U);
}

TEST_CASE("Module ABI: signature ids encode and decode deterministically") {
  const std::uint32_t params[] = {MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_PTR};
  const std::uint32_t id = mobagen_module_signature_encode(MOBAGEN_MODULE_T_I32, params, 3);
  CHECK(id == MOBAGEN_MODULE_SIG_3(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_PTR));

  std::uint32_t ret = 0;
  std::uint32_t count = 0;
  REQUIRE(mobagen_module_signature_decode(id, &ret, &count));
  CHECK(ret == MOBAGEN_MODULE_T_I32);
  CHECK(count == 3U);
  for (std::uint32_t i = 0; i < count; ++i) CHECK(mobagen_module_signature_param(id, i) == params[i]);
  /* Out-of-range parameter index reads as void. */
  CHECK(mobagen_module_signature_param(id, 3) == MOBAGEN_MODULE_T_VOID);

  /* Round-trips for every type code in every slot. */
  const std::uint32_t codes[] = {MOBAGEN_MODULE_T_VOID, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I64, MOBAGEN_MODULE_T_F32,
                                 MOBAGEN_MODULE_T_F64,  MOBAGEN_MODULE_T_PTR, MOBAGEN_MODULE_T_SPAN};
  for (std::uint32_t r : codes) {
    for (std::uint32_t p : codes) {
      const std::uint32_t one[] = {p};
      const std::uint32_t encoded = mobagen_module_signature_encode(r, one, 1);
      std::uint32_t dr = 0xFF;
      std::uint32_t dc = 0;
      REQUIRE(mobagen_module_signature_decode(encoded, &dr, &dc));
      CHECK(dr == r);
      CHECK(dc == 1U);
      CHECK(mobagen_module_signature_param(encoded, 0) == p);
      CHECK(mobagen_module_signature_encode(dr, one, dc) == encoded);
    }
  }

  /* Distinct signatures must produce distinct ids. */
  const std::uint32_t a[] = {MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I64};
  const std::uint32_t b[] = {MOBAGEN_MODULE_T_I64, MOBAGEN_MODULE_T_I32};
  CHECK(mobagen_module_signature_encode(MOBAGEN_MODULE_T_I32, a, 2) != mobagen_module_signature_encode(MOBAGEN_MODULE_T_I32, b, 2));

  /* Malformed ids are rejected, not decoded. */
  CHECK_FALSE(mobagen_module_signature_decode(0x12345678U, &ret, &count));
  CHECK_FALSE(mobagen_module_signature_decode(id | 1U, &ret, &count));      /* bad magic/version */
  CHECK(mobagen_module_signature_encode(MOBAGEN_MODULE_T_I32, a, 6) == 0U); /* too many params */
}

TEST_CASE("Module ABI: annotation table generates correct entries") {
  CHECK(mobagen_module_abi_c_compile_test() == 0);

  CHECK(MobagenCppReferenceExports.kind == MOBAGEN_MODULE_TABLE_V1);
  CHECK(MobagenCppReferenceExports.abi_version == MOBAGEN_MODULE_ABI_VERSION);
  CHECK(MobagenCppReferenceExports.entries[0].signature_id
        == MOBAGEN_MODULE_SIG_3(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_PTR));
  CHECK(MobagenCppReferenceExports.entries[1].signature_id == MOBAGEN_MODULE_SIG_0(MOBAGEN_MODULE_T_I64));
  REQUIRE(MobagenCppReferenceExports.entry_count == 2U);
  CHECK(std::string{MobagenCppReferenceExports.entries[0].name} == "cpp_compute");
  CHECK(std::string{MobagenCppReferenceExports.entries[1].name} == "cpp_tick_count");
  CHECK(MobagenCppReferenceExports.entries[0].fn == reinterpret_cast<MobagenModuleAnyFn>(cpp_compute));

  std::uint32_t out = 0;
  CHECK(cpp_compute(6, 7, &out) == MOBAGEN_MODULE_STATUS_OK);
  CHECK(out == 42U);
  CHECK(MobagenCppReferenceExports.entries[1].signature_id == MOBAGEN_MODULE_SIG_0(MOBAGEN_MODULE_T_I64));
}

TEST_CASE("Module ABI: threads contract resolves spawn only for managed modules") {
  CHECK(mobagen_module_threads_spawn_allowed(MOBAGEN_MODULE_THREADS_NONE) == 0);
  CHECK(mobagen_module_threads_spawn_allowed(MOBAGEN_MODULE_THREADS_MANAGED) != 0);
  CHECK(mobagen_module_threads_requires_quiesce(MOBAGEN_MODULE_THREADS_NONE) == 0);
  CHECK(mobagen_module_threads_requires_quiesce(MOBAGEN_MODULE_THREADS_MANAGED) != 0);
  CHECK(mobagen_module_threads_requires_quiesce(99U) == 0);
}
