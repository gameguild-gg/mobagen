/*
 * QuickJS scripting guest (dynamic-loading-all-platforms todo 16).
 *
 * quickjs-ng v0.10.0 (MIT) compiled to freestanding wasm — the JS engine
 * lives ONLY inside this guest; the host never embeds an engine natively.
 * Mirrors modules/asset_store/portable_asset_store_plugin.c: full portable
 * plugin contract (wasm_abi.h), module ABI v1 annotation table (todo 1
 * macros) for the extraction tool, PLUS the scripting exports:
 *
 *   mobagen_scripting_eval_v1(span source, span result) -> i32
 *       Evaluates `source` as global JS. On success the result span is
 *       filled with `{"ok":true,"value":<json>}`; on a JS error (syntax or
 *       runtime) with `{"ok":false,"error":"Name: message"}`. Returns a
 *       wasm status code (0 ok). Result capacity < 16 fails closed with
 *       OUT_OF_MEMORY. The result JSON encoding is the module's documented
 *       contract (see modules/scripting_quickjs/README section below).
 *
 *   mobagen_scripting_call_v1(span name, span arguments, span result) -> i32
 *       Calls global function `name` with a JSON array of arguments; result
 *       span carries the same {"ok":...} envelope.
 *
 * Result envelope format (manifest-comment contract):
 *   {"ok":true,"value":"2"}          — value JSON-stringified
 *   {"ok":false,"error":"SyntaxError: unexpected token in expression: 'x'"}
 *
 * Spans follow todo 10's wasm32 lowering: ONE i32 cell naming the
 * {offset,size} pair staged in guest linear memory. The host stages the
 * pair + source bytes through writable_memory() then invokes the export.
 *
 * QuickJS runtime is created lazily at start() and torn down at stop() —
 * a reload (Restart policy) gets a fresh realm.
 */
#include <mobagen/module/module_abi.h>
#include <mobagen/plugin/wasm_abi.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "quickjs.h"

#if defined(__wasm__)
#  define MOBAGEN_WASM_GUEST_EXPORT __attribute__((visibility("default")))
#else
#  define MOBAGEN_WASM_GUEST_EXPORT
#endif

/* Capability id mirrors asset_store's scheme ("assets.store.v1"):
 * dotted id whose last segment is .v<digits>. */
#define MOBAGEN_SCRIPTING_CAPABILITY_ID "mobagen.scripting.v1"

#define LINEAR_MEMORY_BYTES UINT32_C(1048576) /* 1 MiB exchange arena */
#define METADATA_BYTES UINT32_C(256)
#define MAX_ALLOCATIONS UINT32_C(16)
#define MAX_RESULT_BYTES (MOBAGEN_WASM_MAX_CONFIGURATION_BYTES / 16) /* 64 KiB */

typedef struct Allocation {
  uint32_t offset;
  uint32_t size;
  uint32_t active;
} Allocation;

_Alignas(8) static uint8_t linear_memory[LINEAR_MEMORY_BYTES];
static Allocation allocations[MAX_ALLOCATIONS];
static uint32_t configured;
static uint32_t started;
static uint32_t metadata_ready;

static JSRuntime* js_runtime;
static JSContext* js_context;

static const char provider_id[] = "mobagen.scripting.quickjs";
static const char capability_id[] = MOBAGEN_SCRIPTING_CAPABILITY_ID;
static const char configuration_schema[] = "mobagen.scripting.quickjs.config.v1";

static void copy_bytes(uint8_t* destination, const uint8_t* source, uint32_t size) {
  uint32_t index;
  for (index = 0; index < size; ++index) destination[index] = source[index];
}

static void zero_bytes(uint8_t* destination, uint32_t size) {
  uint32_t index;
  for (index = 0; index < size; ++index) destination[index] = 0;
}

static uint32_t guest_offset(const uint8_t* pointer) {
#if defined(__wasm__)
  return (uint32_t)(uintptr_t)pointer;
#else
  return (uint32_t)(pointer - linear_memory);
#endif
}

static uint8_t* guest_pointer(uint32_t offset, uint32_t size) {
  uintptr_t address;
  uintptr_t begin = (uintptr_t)linear_memory;
  uintptr_t end = begin + LINEAR_MEMORY_BYTES;
#if defined(__wasm__)
  address = (uintptr_t)offset;
#else
  if (offset > LINEAR_MEMORY_BYTES) return NULL;
  address = begin + offset;
#endif
  if (address < begin || address > end || size > end - address) return NULL;
  return (uint8_t*)address;
}

static uint32_t read_u32(const uint8_t* bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void write_u32(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8U);
  bytes[2] = (uint8_t)(value >> 16U);
  bytes[3] = (uint8_t)(value >> 24U);
}

static void ensure_metadata(void) {
  const uint32_t provider_offset = 8;
  const uint32_t capability_offset = 48;
  const uint32_t provides_offset = 88;
  const uint32_t schema_offset = 104;
  if (metadata_ready) return;
  zero_bytes(linear_memory, METADATA_BYTES);
  copy_bytes(linear_memory + provider_offset, (const uint8_t*)provider_id, (uint32_t)(sizeof(provider_id) - 1U));
  copy_bytes(linear_memory + capability_offset, (const uint8_t*)capability_id, (uint32_t)(sizeof(capability_id) - 1U));
  write_u32(linear_memory + provides_offset, guest_offset(linear_memory + capability_offset));
  write_u32(linear_memory + provides_offset + 4, (uint32_t)(sizeof(capability_id) - 1U));
  copy_bytes(linear_memory + schema_offset, (const uint8_t*)configuration_schema, (uint32_t)(sizeof(configuration_schema) - 1U));
  metadata_ready = 1;
}

static uint32_t local_offset(uint32_t offset, uint32_t size) {
  uint8_t* pointer = guest_pointer(offset, size);
  return pointer == NULL ? UINT32_MAX : (uint32_t)(pointer - linear_memory);
}

static int overlaps(uint32_t offset, uint32_t size, const Allocation* allocation) {
  return allocation->active && offset < allocation->offset + allocation->size && allocation->offset < offset + size;
}

/* ---- portable plugin contract ---- */

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_allocate_v1(uint32_t size, uint32_t alignment) {
  uint32_t slot;
  uint32_t candidate;
  uint32_t occupied;
  if (size == 0) return MOBAGEN_WASM_NULL_OFFSET;
  if (alignment == 0 || alignment > MOBAGEN_WASM_EXCHANGE_ALIGNMENT || (alignment & (alignment - 1U)) != 0
      || size > LINEAR_MEMORY_BYTES - METADATA_BYTES) {
    return MOBAGEN_WASM_NULL_OFFSET;
  }
  ensure_metadata();
  for (slot = 0; slot < MAX_ALLOCATIONS && allocations[slot].active; ++slot) {
  }
  if (slot == MAX_ALLOCATIONS) return MOBAGEN_WASM_NULL_OFFSET;
  for (candidate = METADATA_BYTES; candidate <= LINEAR_MEMORY_BYTES - size; candidate += MOBAGEN_WASM_EXCHANGE_ALIGNMENT) {
    uint32_t index;
    occupied = 0;
    for (index = 0; index < MAX_ALLOCATIONS; ++index) {
      if (overlaps(candidate, size, &allocations[index])) {
        occupied = 1;
        break;
      }
    }
    if (!occupied) {
      allocations[slot].offset = candidate;
      allocations[slot].size = size;
      allocations[slot].active = 1;
      return guest_offset(linear_memory + candidate);
    }
  }
  return MOBAGEN_WASM_NULL_OFFSET;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_deallocate_v1(uint32_t offset, uint32_t size, uint32_t alignment) {
  uint32_t index;
  uint32_t local;
  if (alignment == 0 || alignment > MOBAGEN_WASM_EXCHANGE_ALIGNMENT || (alignment & (alignment - 1U)) != 0) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  local = local_offset(offset, size);
  if (local == UINT32_MAX) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < MAX_ALLOCATIONS; ++index) {
    if (allocations[index].active && allocations[index].offset == local && allocations[index].size == size) {
      allocations[index].active = 0;
      return MOBAGEN_WASM_STATUS_OK;
    }
  }
  return MOBAGEN_WASM_STATUS_NOT_FOUND;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_query_v1(uint32_t descriptor_offset, uint32_t descriptor_capacity) {
  uint8_t* descriptor;
  ensure_metadata();
  if (descriptor_capacity < MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  descriptor = guest_pointer(descriptor_offset, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  if (descriptor == NULL) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  zero_bytes(descriptor, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  write_u32(descriptor, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  write_u32(descriptor + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  write_u32(descriptor + 8, guest_offset(linear_memory + 8));
  write_u32(descriptor + 12, (uint32_t)(sizeof(provider_id) - 1U));
  write_u32(descriptor + 16, 1);
  write_u32(descriptor + 20, 0);
  write_u32(descriptor + 24, 0);
  write_u32(descriptor + 28, MOBAGEN_WASM_RELOAD_RESTART);
  write_u32(descriptor + 32, guest_offset(linear_memory + 88)); /* provides array */
  write_u32(descriptor + 36, 1);
  write_u32(descriptor + 64, guest_offset(linear_memory + 104)); /* configuration schema */
  write_u32(descriptor + 68, (uint32_t)(sizeof(configuration_schema) - 1U));
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_configure_v1(uint32_t configuration_offset, uint32_t configuration_size) {
  if (started) return MOBAGEN_WASM_STATUS_CONFLICT;
  if (configuration_size != 0 && guest_pointer(configuration_offset, configuration_size) == NULL) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  configured = 1;
  return MOBAGEN_WASM_STATUS_OK;
}

static void teardown_runtime(void) {
  if (js_context != NULL) {
    JS_FreeContext(js_context);
    js_context = NULL;
  }
  if (js_runtime != NULL) {
    JS_FreeRuntime(js_runtime);
    js_runtime = NULL;
  }
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_start_v1(void) {
  if (!configured || started) return MOBAGEN_WASM_STATUS_CONFLICT;
  js_runtime = JS_NewRuntime();
  if (js_runtime == NULL) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  JS_SetMemoryLimit(js_runtime, 32U * 1024U * 1024U);
  JS_SetMaxStackSize(js_runtime, 512U * 1024U);
  js_context = JS_NewContext(js_runtime);
  if (js_context == NULL) {
    teardown_runtime();
    return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  }
  started = 1;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_quiesce_v1(void) {
  if (!started) return MOBAGEN_WASM_STATUS_CONFLICT;
  started = 0;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_stop_v1(void) {
  teardown_runtime();
  configured = 0;
  started = 0;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_process_v1(uint32_t input_batch_offset, uint32_t output_batch_offset,
                                                                  uint32_t result_offset) {
  /* Scripting never rides the per-frame command pipeline (plan rule). */
  (void)input_batch_offset;
  (void)output_batch_offset;
  (void)result_offset;
  if (!started) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  return MOBAGEN_WASM_STATUS_UNSUPPORTED;
}

/* ---- scripting core ---- */

/* Writes `text` into the result span with JSON envelope prefix/suffix.
 * Returns MOBAGEN_WASM_STATUS_OUT_OF_MEMORY when the span is too small. */
static uint32_t write_envelope(const MobagenModuleSpan32* result, const char* prefix, const char* text, uint32_t text_size, const char* suffix) {
  const uint32_t prefix_size = (uint32_t)strlen(prefix);
  const uint32_t suffix_size = (uint32_t)strlen(suffix);
  uint8_t* out;
  uint32_t total;
  if (result->size < prefix_size + text_size + suffix_size + 1U) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  out = guest_pointer(result->offset, result->size);
  if (out == NULL) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  total = 0;
  copy_bytes(out + total, (const uint8_t*)prefix, prefix_size);
  total += prefix_size;
  copy_bytes(out + total, (const uint8_t*)text, text_size);
  total += text_size;
  copy_bytes(out + total, (const uint8_t*)suffix, suffix_size);
  total += suffix_size;
  out[total] = 0;
  return MOBAGEN_WASM_STATUS_OK;
}

/* JSON-escape into a caller scratch buffer (always NUL-terminated). */
static void json_escape(const char* text, uint32_t size, char* out, uint32_t capacity) {
  uint32_t in_index = 0;
  uint32_t out_index = 0;
  for (in_index = 0; in_index < size && out_index + 7U < capacity; ++in_index) {
    unsigned char c = (unsigned char)text[in_index];
    const char* escape = NULL;
    char buffer[8];
    switch (c) {
      case '"': escape = "\\\""; break;
      case '\\': escape = "\\\\"; break;
      case '\n': escape = "\\n"; break;
      case '\r': escape = "\\r"; break;
      case '\t': escape = "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          buffer[0] = '\\';
          buffer[1] = 'u';
          buffer[2] = '0';
          buffer[3] = '0';
          buffer[4] = (char)('0' + ((c >> 4) & 0xf));
          buffer[5] = (char)('0' + (c & 0xf));
          buffer[6] = 0;
          escape = buffer;
        }
        break;
    }
    if (escape != NULL) {
      while (*escape != 0 && out_index + 1U < capacity) out[out_index++] = *escape++;
      continue;
    }
    out[out_index++] = (char)c;
  }
  out[out_index] = 0;
}

/* Copies a JSValue's string form into scratch, JSON-escaped. Returns bytes
 * written (excluding NUL) or UINT32_MAX on allocation failure. */
static uint32_t stringify_value(char* scratch, uint32_t capacity, JSValue value, JSContext* ctx) {
  const char* text;
  size_t length = 0;
  uint32_t result_size;
  text = JS_ToCStringLen2(ctx, &length, value, 0);
  if (text == NULL) {
    JS_FreeValue(ctx, JS_GetException(ctx)); /* stringify itself threw */
    if (capacity < 8) return UINT32_MAX;
    copy_bytes((uint8_t*)scratch, (const uint8_t*)"<unprintable>", 13);
    scratch[13] = 0;
    return 13;
  }
  json_escape(text, (uint32_t)length, scratch, capacity);
  result_size = (uint32_t)strlen(scratch);
  JS_FreeCString(ctx, text);
  return result_size;
}

static _Thread_local char result_scratch[MAX_RESULT_BYTES];

/* Shared envelope: on pending exception builds the {"ok":false,...} error
 * text from the exception's name/message. */
static uint32_t report_exception(const MobagenModuleSpan32* result, JSContext* ctx) {
  JSValue exception = JS_GetException(ctx);
  char error_text[MAX_RESULT_BYTES];
  uint32_t error_size = 0;
  error_text[0] = 0;
  if (JS_IsError(ctx, exception)) {
    JSValue name = JS_GetPropertyStr(ctx, exception, "name");
    JSValue message = JS_GetPropertyStr(ctx, exception, "message");
    const char* name_text = JS_ToCString(ctx, name);
    const char* message_text = JS_ToCString(ctx, message);
    if (name_text != NULL && message_text != NULL) {
      uint32_t name_size = (uint32_t)strlen(name_text);
      uint32_t message_size = (uint32_t)strlen(message_text);
      if (name_size + 2 + message_size + 1 > sizeof(error_text)) {
        if (name_size + 2 >= sizeof(error_text)) name_size = 0;
        message_size = (uint32_t)sizeof(error_text) - name_size - 3;
      }
      copy_bytes((uint8_t*)error_text, (const uint8_t*)name_text, name_size);
      error_text[name_size] = ':';
      error_text[name_size + 1] = ' ';
      copy_bytes((uint8_t*)error_text + name_size + 2, (const uint8_t*)message_text, message_size);
      error_text[name_size + 2 + message_size] = 0;
      error_size = name_size + 2 + message_size;
    }
    if (name_text != NULL) JS_FreeCString(ctx, name_text);
    if (message_text != NULL) JS_FreeCString(ctx, message_text);
    JS_FreeValue(ctx, name);
    JS_FreeValue(ctx, message);
  }
  if (error_size == 0) {
    error_size = stringify_value(error_text, (uint32_t)sizeof(error_text), exception, ctx);
    if (error_size == UINT32_MAX) error_size = 0;
  }
  JS_FreeValue(ctx, exception);
  return write_envelope(result, "{\"ok\":false,\"error\":\"", error_text, error_size, "\"}");
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_scripting_eval_v1(MobagenModuleSpan32 source, MobagenModuleSpan32 result) {
  const uint8_t* source_bytes;
  JSValue value;
  uint32_t status;
  uint32_t value_size;
  if (!started || js_context == NULL) return MOBAGEN_WASM_STATUS_CONFLICT;
  if (source.size == 0 || source.size > MAX_RESULT_BYTES) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  if (result.size < 16) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  source_bytes = guest_pointer(source.offset, source.size);
  if (source_bytes == NULL) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  /* JS_Eval requires NUL-terminated input; stage through the scratch. */
  copy_bytes((uint8_t*)result_scratch, source_bytes, source.size);
  result_scratch[source.size] = 0;
  value = JS_Eval(js_context, result_scratch, (size_t)source.size, "<eval>", JS_EVAL_TYPE_GLOBAL);
  if (JS_VALUE_GET_TAG(value) == JS_TAG_EXCEPTION) {
    return report_exception(&result, js_context);
  }
  value_size = stringify_value(result_scratch, (uint32_t)sizeof(result_scratch), value, js_context);
  JS_FreeValue(js_context, value);
  if (value_size == UINT32_MAX) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  status = write_envelope(&result, "{\"ok\":true,\"value\":\"", result_scratch, value_size, "\"}");
  return status;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_scripting_call_v1(MobagenModuleSpan32 name, MobagenModuleSpan32 arguments,
                                                             MobagenModuleSpan32 result) {
  const uint8_t* name_bytes;
  JSValue global;
  JSValue function;
  JSValue parsed;
  JSValue value;
  uint32_t status;
  uint32_t value_size;
  if (!started || js_context == NULL) return MOBAGEN_WASM_STATUS_CONFLICT;
  if (name.size == 0 || name.size > 256 || arguments.size > MAX_RESULT_BYTES) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  if (result.size < 16) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  name_bytes = guest_pointer(name.offset, name.size);
  if (name_bytes == NULL) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  copy_bytes((uint8_t*)result_scratch, name_bytes, name.size);
  result_scratch[name.size] = 0;

  global = JS_GetGlobalObject(js_context);
  function = JS_GetPropertyStr(js_context, global, result_scratch);
  JS_FreeValue(js_context, global);
  if (JS_VALUE_GET_TAG(function) == JS_TAG_EXCEPTION) {
    return report_exception(&result, js_context);
  }
  if (!JS_IsFunction(js_context, function)) {
    JS_FreeValue(js_context, function);
    return write_envelope(&result, "{\"ok\":false,\"error\":\"not a function: ", (const char*)result_scratch, name.size, "\"}");
  }

  if (arguments.size == 0) {
    value = JS_Call(js_context, function, JS_UNDEFINED, 0, NULL);
  } else {
    const uint8_t* argument_bytes = guest_pointer(arguments.offset, arguments.size);
    char argument_scratch[MAX_RESULT_BYTES];
    if (argument_bytes == NULL) {
      JS_FreeValue(js_context, function);
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    copy_bytes((uint8_t*)argument_scratch, argument_bytes, arguments.size);
    argument_scratch[arguments.size] = 0;
    parsed = JS_ParseJSON(js_context, argument_scratch, (size_t)arguments.size, "<arguments>");
    if (JS_VALUE_GET_TAG(parsed) == JS_TAG_EXCEPTION) {
      JS_FreeValue(js_context, function);
      return report_exception(&result, js_context);
    }
    {
      int64_t count = 0;
      JSValue items[8];
      int index;
      uint32_t used = 0;
      if (JS_GetLength(js_context, parsed, &count) < 0 || count < 0 || count > 8) {
        JS_FreeValue(js_context, parsed);
        JS_FreeValue(js_context, function);
        return write_envelope(&result, "{\"ok\":false,\"error\":\"arguments array must hold 0..8 items", "", 0, "\"}");
      }
      for (index = 0; index < (int)count; ++index) {
        items[used++] = JS_GetPropertyUint32(js_context, parsed, (uint32_t)index);
      }
      value = JS_Call(js_context, function, JS_UNDEFINED, (int)count, items);
      while (used > 0) JS_FreeValue(js_context, items[--used]);
    }
    JS_FreeValue(js_context, parsed);
  }
  JS_FreeValue(js_context, function);
  if (JS_VALUE_GET_TAG(value) == JS_TAG_EXCEPTION) {
    return report_exception(&result, js_context);
  }
  value_size = stringify_value(result_scratch, (uint32_t)sizeof(result_scratch), value, js_context);
  JS_FreeValue(js_context, value);
  if (value_size == UINT32_MAX) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  status = write_envelope(&result, "{\"ok\":true,\"value\":\"", result_scratch, value_size, "\"}");
  return status;
}

/* ---- module ABI v1 ---- */

/* Annotation table (todo 1 macros; todo 18's 9-entry capacity — the max the
 * table macros support). The two span params lower to one i32 cell each on
 * wasm32 (todo 10). process/entry stay ABI exports but outside the table:
 * process is per-frame plumbing the plan forbids routing anyway, and the
 * loader validates entry separately (mobagen_module_entry_v1 symbol). */
MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(mobagen_scripting_table, 9)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_wasm_plugin_allocate_v1, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                            MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_wasm_plugin_deallocate_v1, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                            MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_wasm_plugin_query_v1, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                            MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_wasm_plugin_configure_v1, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                            MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_wasm_plugin_start_v1, MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_wasm_plugin_quiesce_v1, MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_wasm_plugin_stop_v1, MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_scripting_eval_v1, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_SPAN,
                            MOBAGEN_MODULE_T_SPAN)
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_scripting_call_v1, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_SPAN,
                            MOBAGEN_MODULE_T_SPAN, MOBAGEN_MODULE_T_SPAN)
MOBAGEN_MODULE_EXPORT_TABLE_END(mobagen_scripting_table, mobagen_wasm_plugin_allocate_v1, mobagen_wasm_plugin_deallocate_v1,
                                mobagen_wasm_plugin_query_v1, mobagen_wasm_plugin_configure_v1, mobagen_wasm_plugin_start_v1,
                                mobagen_wasm_plugin_quiesce_v1, mobagen_wasm_plugin_stop_v1, mobagen_scripting_eval_v1,
                                mobagen_scripting_call_v1);
MOBAGEN_MODULE_EXPORT_PUBLISH_TABLE(mobagen_scripting_table)

MOBAGEN_WASM_GUEST_EXPORT MobagenModuleStatus MOBAGEN_MODULE_CALL mobagen_module_entry_v1(const MobagenModuleHostApiV1* host,
                                                                                          MobagenModuleDescriptorV1* descriptor) {
  (void)host;
  if (descriptor == NULL || descriptor->struct_size < MOBAGEN_MODULE_DESCRIPTOR_V1_SIZE) return MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT;
  descriptor->threads_policy = MOBAGEN_MODULE_THREADS_NONE;
  return MOBAGEN_MODULE_STATUS_OK;
}
