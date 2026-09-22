/*
 * Reference guest for the module signature-extraction tooling (todo 3).
 *
 * Compiled to a wasm binary by the test fixtures via emcc; the extraction
 * CLI must derive module.manifest from the annotation table alone. The
 * golden manifest (golden_manifest.yaml in this directory) must match the
 * tool's output byte-for-byte, including the plugin.wasm payload hash+size.
 */

#include <mobagen/module/module_abi.h>

#include <stddef.h>
#include <stdint.h>

static int32_t reference_guest_ping(int32_t input, int32_t scale) { return input * scale; }

static int32_t reference_guest_span_bytes(const MobagenModuleSpan32* span) { return (int32_t)span->size; }

static void reference_guest_health(void) {}

MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(reference_table, 3)
MOBAGEN_MODULE_EXPORT_ENTRY(reference_table, reference_guest_ping, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(reference_table, reference_guest_span_bytes, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_PTR)
MOBAGEN_MODULE_EXPORT_ENTRY(reference_table, reference_guest_health, MOBAGEN_MODULE_T_VOID)
MOBAGEN_MODULE_EXPORT_TABLE_END(reference_table, reference_guest_ping, reference_guest_span_bytes, reference_guest_health);

MOBAGEN_MODULE_EXPORT_PUBLISH_TABLE(reference_table)

MOBAGEN_MODULE_EXPORT MobagenModuleStatus MOBAGEN_MODULE_CALL mobagen_module_entry_v1(const MobagenModuleHostApiV1* host,
                                                                                      MobagenModuleDescriptorV1* descriptor) {
  (void)host;
  if (descriptor == NULL || descriptor->struct_size < MOBAGEN_MODULE_DESCRIPTOR_V1_SIZE) return MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT;
  descriptor->abi_version = MOBAGEN_MODULE_ABI_VERSION;
  descriptor->threads_policy = MOBAGEN_MODULE_THREADS_NONE;
  return MOBAGEN_MODULE_STATUS_OK;
}
