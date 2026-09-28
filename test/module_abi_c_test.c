#include <mobagen/module/module_abi.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static MobagenModuleStatus MOBAGEN_MODULE_CALL compute_hit(uint32_t a, uint32_t b, uint32_t* out) {
  if (out == NULL) return MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT;
  *out = a + b;
  return MOBAGEN_MODULE_STATUS_OK;
}

static MobagenModuleStatus MOBAGEN_MODULE_CALL reset_cache(void) { return MOBAGEN_MODULE_STATUS_OK; }

static uint32_t guest_offset = 0;

static uint32_t MOBAGEN_MODULE_CALL read_offset(void) { return guest_offset; }

static MobagenModuleStatus MOBAGEN_MODULE_CALL write_span(MobagenModuleSpan32 span) {
  guest_offset = span.offset;
  return MOBAGEN_MODULE_STATUS_OK;
}

/*
 * C11 annotation table: declared with the canonical macros, each entry
 * registered with return + parameter type codes. The extraction tool (todo 3)
 * and the manifest stay in sync with this table.
 */
MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(MobagenReferenceModuleExports, 4)
MOBAGEN_MODULE_EXPORT_ENTRY(MobagenReferenceModuleExports, compute_hit, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                            MOBAGEN_MODULE_T_PTR)
MOBAGEN_MODULE_EXPORT_ENTRY(MobagenReferenceModuleExports, reset_cache, MOBAGEN_MODULE_T_VOID)
MOBAGEN_MODULE_EXPORT_ENTRY(MobagenReferenceModuleExports, read_offset, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_VOID)
MOBAGEN_MODULE_EXPORT_ENTRY(MobagenReferenceModuleExports, write_span, MOBAGEN_MODULE_T_VOID, MOBAGEN_MODULE_T_SPAN)
MOBAGEN_MODULE_EXPORT_TABLE_END(MobagenReferenceModuleExports, compute_hit, reset_cache, read_offset, write_span);

_Static_assert(MobagenReferenceModuleExports.entries[0].signature_id == UINT32_C(0x4D324D00), "entry 0 signature id");
_Static_assert(MobagenReferenceModuleExports.entries[0].signature_id
                   == MOBAGEN_MODULE_SIG_3(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_PTR),
               "entry 0 signature id matches SIG macro");
_Static_assert(MobagenReferenceModuleExports.entries[3].signature_id == MOBAGEN_MODULE_SIG_1(MOBAGEN_MODULE_T_VOID, MOBAGEN_MODULE_T_SPAN),
               "entry 3 signature id");

int mobagen_module_abi_c_compile_test(void) {
  if (MobagenReferenceModuleExports.kind != MOBAGEN_MODULE_TABLE_V1) return 1;
  if (MobagenReferenceModuleExports.abi_version != MOBAGEN_MODULE_ABI_VERSION) return 2;
  if (MobagenReferenceModuleExports.entry_count != 4) return 3;
  if (MobagenReferenceModuleExports.entries[0].fn != (MobagenModuleAnyFn)compute_hit) return 4;
  if (MobagenReferenceModuleExports.entries[3].fn != (MobagenModuleAnyFn)write_span) return 5;
  if (strcmp(MobagenReferenceModuleExports.entries[1].name, "reset_cache") != 0) return 6;
  if (strcmp(MobagenReferenceModuleExports.entries[2].name, "read_offset") != 0) return 7;

  /* Round-trip: decode the annotated id, re-encode, compare. */
  uint32_t ret = 0;
  uint32_t count = 0;
  if (!mobagen_module_signature_decode(MobagenReferenceModuleExports.entries[0].signature_id, &ret, &count)) return 8;
  if (ret != MOBAGEN_MODULE_T_I32 || count != 3) return 9;
  uint32_t params[5] = {0};
  for (uint32_t i = 0; i < count; ++i) params[i] = (uint32_t)mobagen_module_signature_param(MobagenReferenceModuleExports.entries[0].signature_id, i);
  if (params[0] != MOBAGEN_MODULE_T_I32 || params[1] != MOBAGEN_MODULE_T_I32 || params[2] != MOBAGEN_MODULE_T_PTR) return 10;
  if (mobagen_module_signature_encode(ret, params, count) != MobagenReferenceModuleExports.entries[0].signature_id) return 11;

  /* Invalid ids must be rejected by the decoder. */
  if (mobagen_module_signature_decode(UINT32_C(0x12345678), &ret, &count)) return 12;
  if (mobagen_module_signature_decode(MobagenReferenceModuleExports.entries[0].signature_id | UINT32_C(1), &ret, &count)) return 13;

  MobagenModuleHostApiV1 host;
  host.struct_size = (uint32_t)sizeof(host);
  host.abi_version = MOBAGEN_MODULE_ABI_VERSION;
  host.module_context = 0;

  MobagenModuleDescriptorV1 descriptor;
  descriptor.struct_size = (uint32_t)sizeof(descriptor);
  descriptor.abi_version = MOBAGEN_MODULE_ABI_VERSION;
  descriptor.threads_policy = MOBAGEN_MODULE_THREADS_NONE;

  uint32_t out = 0;
  MobagenModuleSpan32 span;
  span.offset = 2;
  span.size = 8;
  if (compute_hit(2, 3, &out) != MOBAGEN_MODULE_STATUS_OK || out != 5) return 14;
  if (reset_cache() != MOBAGEN_MODULE_STATUS_OK) return 15;
  if (read_offset() != 0) return 16;
  if (write_span(span) != MOBAGEN_MODULE_STATUS_OK || read_offset() != 2) return 17;

  return host.struct_size == sizeof(host) && descriptor.struct_size == sizeof(descriptor)
                 && strcmp(MOBAGEN_MODULE_ENTRY_V1_SYMBOL, "mobagen_module_entry_v1") == 0
                 && strcmp(MOBAGEN_MODULE_THREAD_SPAWN_IMPORT_V1, "mobagen_thread_spawn_v1") == 0
                 && strcmp(MOBAGEN_MODULE_THREAD_QUIESCE_EXPORT_V1, "mobagen_module_thread_quiesce_v1") == 0
                 && strcmp(MOBAGEN_MODULE_TABLE_EXPORT_SYMBOL_V1, "mobagen_module_exports_v1") == 0
             ? 0
             : 18;
}
