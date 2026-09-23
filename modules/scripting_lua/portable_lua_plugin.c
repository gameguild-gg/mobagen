/*
 * Lua scripting guest (dynamic-loading-all-platforms todo 17).
 *
 * Mirrors modules/scripting_quickjs/portable_quickjs_plugin.c: full
 * portable plugin contract (wasm_abi.h), module ABI v1 annotation table
 * (todo 1 macros), and the same scripting capability family:
 *
 *   mobagen_scripting_eval_v1(span source, span result) -> i32
 *       Evaluates `source` as a Lua chunk. On success the result span is
 *       filled with {"ok":true,"value":"<stringified>"}; on a Lua error
 *       (syntax or runtime) with {"ok":false,"error":"<message>"}. Result
 *       capacity < 16 fails closed with OUT_OF_MEMORY.
 *
 *   mobagen_scripting_call_v1(span name, span arguments, span result) -> i32
 *       Calls global function `name` with a JSON array of scalar arguments
 *       (string/number/boolean); result span carries the same envelope.
 *
 *   mobagen_scripting_error_v1(span result) -> i32
 *       Trap-recovery half (see below): after a trapped eval/call, the host
 *       re-enters the guest here to finish the unwind and collect the
 *       error text.
 *
 * Error recovery divergence from QuickJS (THE structural difference):
 * Lua's core unwinds errors with setjmp/longjmp (ldo.c). The import-free
 * wasm builds cannot use them (emscripten's JS setjmp needs env.invoke_*
 * imports the loader does not bind; wasi-libc setjmp requires wasm-EH,
 * which WAMR 2.4.5 fast-interp/AOT and the import-free browser guest do
 * not implement). The vendored ldo.c is patched (MOBAGEN_LUA_TRAP_RECOVERY)
 * so LUAI_THROW stages the error into guest statics and __builtin_trap()s.
 * The host sees the failed invoke, then calls mobagen_scripting_error_v1,
 * which performs the state restoration luaD_throw does for a thread with
 * no error handler (luaE_resetthread shape: unwind CallInfo chain, close
 * upvalues, park the error message at the stack base) and writes the
 * ok:false envelope. The Lua state stays usable afterwards.
 *
 * Spans follow todo 10's wasm32 lowering: ONE i32 cell naming the
 * {offset,size} pair staged in guest linear memory.
 */
#include <mobagen/module/module_abi.h>
#include <mobagen/plugin/wasm_abi.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Freestanding builds carry no <stdlib.h>/<stdio.h>; the sysroot headers
 * declare strtod/snprintf on the wasi path and musl does on the emcc
 * path — declare once here so both regimes compile. */
double strtod(const char* text, char** end);
int snprintf(char* buffer, size_t size, const char* format, ...);

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "ldo.h"
#include "lstate.h"
#include "llimits.h"

#if defined(__wasm__)
#  define MOBAGEN_WASM_GUEST_EXPORT __attribute__((visibility("default")))
#else
#  define MOBAGEN_WASM_GUEST_EXPORT
#endif

/* wasm-ld's shadow-stack global (initial value = the layout's stack top).
 * A trapped call skips every epilogue's restore, so the global leaks
 * downward across trap+drain cycles until exports underflow memory. Each
 * scripting export entry re-seeds it — safe because the guest is strictly
 * call-driven: nothing runs between host invokes. */

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

static lua_State* lua_state;

static const char provider_id[] = "mobagen.scripting.lua";
static const char capability_id[] = MOBAGEN_SCRIPTING_CAPABILITY_ID;
static const char configuration_schema[] = "mobagen.scripting.lua.config.v1";

/* ---- trap-recovery state (the patched ldo.c reads these) ---- */

/* Set while a scripting export runs a protected chunk; LUAI_THROW stages
 * here, traps, and mobagen_scripting_error_v1 drains it. */
static lua_State* trap_thread;
static int trap_status; /* LUA_ERRxxx staged by the throw */
static uint32_t trap_armed;

#if defined(__wasm__)
extern int __stack_pointer;
/* Initial value captured on the first clean entry (before any trap can
 * leak the global); recovery restores from the snapshot. Hardcoding the
 * layout constant instead corrupts data in the interpreter/AOT layouts
 * (AOT frames would overlap .rodata). */
static int mobagen_lua_stack_initial;
static void mobagen_lua_reset_shadow_stack(void) {
  if (trap_thread != NULL) {
    if (mobagen_lua_stack_initial != 0) __stack_pointer = mobagen_lua_stack_initial;
  } else if (mobagen_lua_stack_initial == 0) {
    mobagen_lua_stack_initial = __stack_pointer;
  }
}
#else
static void mobagen_lua_reset_shadow_stack(void) {
}
#endif

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

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_start_v1(void) {
  if (!configured || started) return MOBAGEN_WASM_STATUS_CONFLICT;
  lua_state = luaL_newstate();
  if (lua_state == NULL) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  luaL_openlibs(lua_state);
  started = 1;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_quiesce_v1(void) {
  if (!started) return MOBAGEN_WASM_STATUS_CONFLICT;
  started = 0;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_stop_v1(void) {
  if (lua_state != NULL) {
    lua_close(lua_state);
    lua_state = NULL;
  }
  configured = 0;
  started = 0;
  trap_armed = 0;
  trap_thread = NULL;
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

static uint32_t write_envelope(const MobagenModuleSpan32* result, const char* prefix, const char* text, uint32_t text_size,
                               const char* suffix) {
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

static char result_scratch[MAX_RESULT_BYTES];

/* Lua value -> string form in scratch (json-escaped). Numbers use
 * LUA_NUMBER_FMT ("%.14g"); integers print exactly; everything else
 * through lua_tolstring. Returns escaped size. */
/* Value -> string form. lua_pushfstring accepts only Lua's own format
 * specifiers (%d/%s/%I/%f...); number formatting routes through snprintf
 * (from the math archive) exactly like Lua's l_sprintf. */
static uint32_t stringify_top(char* scratch, uint32_t capacity, lua_State* L) {
  const char* text;
  size_t length = 0;
  if (lua_isinteger(L, -1)) {
    if (snprintf(scratch, capacity, LUA_INTEGER_FMT, lua_tointeger(L, -1)) < 0) scratch[0] = 0;
    return (uint32_t)strlen(scratch);
  }
  if (lua_isnumber(L, -1)) {
    if (snprintf(scratch, capacity, LUA_NUMBER_FMT, (double)lua_tonumber(L, -1)) < 0) scratch[0] = 0;
    return (uint32_t)strlen(scratch);
  }
  text = lua_tolstring(L, -1, &length);
  if (text == NULL) {
    copy_bytes((uint8_t*)scratch, (const uint8_t*)"<unprintable>", 13);
    scratch[13] = 0;
    return 13;
  }
  json_escape(text, (uint32_t)length, scratch, capacity);
  return (uint32_t)strlen(scratch);
}

/* ---- trap-recovery halves (declared in the patched ldo.c) ---- */

struct lua_longjmp* mobagen_lua_current_error_jmp(void) {
  return lua_state != NULL ? lua_state->errorJmp : NULL;
}

int mobagen_lua_toplevel_error_jmp_active(void) {
  return lua_state != NULL && lua_state->errorJmp != NULL;
}

/* LUAI_THROW lands here: snapshot, unwind-finish, trap. Runs INSIDE the
 * guest on the errored call chain; must only touch statics (any Lua C API
 * call could itself throw through the disarmed path). */
void mobagen_lua_stage_trap_error(lua_State* L, int status) {
  (void)L;
  trap_thread = L;
  trap_status = status != 0 ? status : LUA_ERRRUN;
  /* Volatile loop: neither engine may assume a return path (an
   * `unreachable` successor lets wamrc fold caller epilogues away and
   * execute the trap continuation with stale registers). */
  for (;;) {
    __asm__ volatile("");
    __builtin_trap();
  }
}

/* Host re-entry after the trap: finish the unwind luaD_throw would have
 * done for a handler-less thread (luaE_resetthread does the CallInfo +
 * to-be-closed cleanup and parks the message at the stack base), then
 * write the ok:false envelope. Returns the wasm status. */
/* Shared static scratch: 64 KiB stack locals overflow the wasm shadow
 * stack (wasm-ld sizes it from main's frame), so every large buffer is
 * file-scope, single-threaded guest. */
static char error_scratch[MAX_RESULT_BYTES];

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_scripting_error_v1(MobagenModuleSpan32 result) {
  mobagen_lua_reset_shadow_stack();
  const char* message;
  size_t length = 0;
  uint32_t error_size;
  lua_State* thread;
  if (trap_thread == NULL || lua_state == NULL) return MOBAGEN_WASM_STATUS_CONFLICT;
  if (result.size < 16) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  thread = trap_thread;
  trap_thread = NULL;
  trap_armed = 0;
  /* Same reset the stock luaD_throw applies to a thread with no handler:
   * the error object sits at the thread's top; resetthread unwinds the
   * CallInfo chain, runs __close metamethods (ignored failure), and moves
   * the message to the stack base. */
  (void)luaE_resetthread(thread, trap_status);
  /* The trap skipped luaD_rawrunprotected's epilogue — errorJmp still
   * points into the unwound C stack (never dereferenced, but stale) and
   * nCcalls was never restored. Clean both; then finish any GC step the
   * error interrupted (a trap can unwind mid-GC when a finalizer throws). */
  thread->errorJmp = NULL;
  thread->nCcalls = 0;
  lua_gc(thread, LUA_GCCOLLECT);
  message = lua_tolstring(thread, -1, &length);
  if (message == NULL) message = "lua error";
  json_escape(message, (uint32_t)strlen(message), error_scratch, (uint32_t)sizeof(error_scratch));
  error_size = (uint32_t)strlen(error_scratch);
  lua_settop(thread, 0);
  return write_envelope(&result, "{\"ok\":false,\"error\":\"", error_scratch, error_size, "\"}");
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_scripting_eval_v1(MobagenModuleSpan32 source, MobagenModuleSpan32 result) {
  mobagen_lua_reset_shadow_stack();
  const uint8_t* source_bytes;
  uint32_t value_size;
  if (!started || lua_state == NULL) return MOBAGEN_WASM_STATUS_CONFLICT;
  if (source.size == 0 || source.size > MAX_RESULT_BYTES) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  if (result.size < 16) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  source_bytes = guest_pointer(source.offset, source.size);
  if (source_bytes == NULL) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  copy_bytes((uint8_t*)result_scratch, source_bytes, source.size);
  result_scratch[source.size] = 0;

  trap_armed = 1;
  {
    const int load_status = (int)luaL_loadbufferx(lua_state, result_scratch, (size_t)source.size, "=<eval>", NULL);
    if (load_status != LUA_OK) {
      const char* message;
      size_t length = 0;
      uint32_t error_size;
      trap_armed = 0;
      message = lua_tolstring(lua_state, -1, &length);
      if (message == NULL) message = "syntax error";
      json_escape(message, (uint32_t)strlen(message), error_scratch, (uint32_t)sizeof(error_scratch));
      error_size = (uint32_t)strlen(error_scratch);
      lua_settop(lua_state, 0);
      return write_envelope(&result, "{\"ok\":false,\"error\":\"", error_scratch, error_size, "\"}");
    }
  }
  /* lua_pcall's protected frame is what the trap path interrupts on a
   * runtime error; the host finishes through mobagen_scripting_error_v1. */
  lua_pcall(lua_state, 0, LUA_MULTRET, 0);
  trap_armed = 0;
  if (lua_gettop(lua_state) == 0) {
    copy_bytes((uint8_t*)result_scratch, (const uint8_t*)"nil", 3);
    result_scratch[3] = 0;
    value_size = 3;
  } else {
    lua_settop(lua_state, -1); /* keep only the first result */
    value_size = stringify_top(result_scratch, (uint32_t)sizeof(result_scratch), lua_state);
  }
  lua_settop(lua_state, 0);
  return write_envelope(&result, "{\"ok\":true,\"value\":\"", result_scratch, value_size, "\"}");
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_scripting_call_v1(MobagenModuleSpan32 name, MobagenModuleSpan32 arguments,
                                                             MobagenModuleSpan32 result) {
  mobagen_lua_reset_shadow_stack();
  const uint8_t* name_bytes;
  const uint8_t* argument_bytes;
  uint32_t value_size;
  if (!started || lua_state == NULL) return MOBAGEN_WASM_STATUS_CONFLICT;
  if (name.size == 0 || name.size > 256 || arguments.size > MAX_RESULT_BYTES) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  if (result.size < 16) return MOBAGEN_WASM_STATUS_OUT_OF_MEMORY;
  name_bytes = guest_pointer(name.offset, name.size);
  if (name_bytes == NULL) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  copy_bytes((uint8_t*)result_scratch, name_bytes, name.size);
  result_scratch[name.size] = 0;

  lua_getglobal(lua_state, result_scratch);
  if (!lua_isfunction(lua_state, -1)) {
    lua_settop(lua_state, 0);
    return write_envelope(&result, "{\"ok\":false,\"error\":\"not a function: ", (const char*)result_scratch, name.size, "\"}");
  }

  if (arguments.size > 0) {
    argument_bytes = guest_pointer(arguments.offset, arguments.size);
    if (argument_bytes == NULL) {
      lua_settop(lua_state, 0);
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    {
      /* Minimal JSON array of scalars: [ ] with string / number / true /
       * false / null items only. Numbers may be integral or decimal. */
      const char* cursor = (const char*)argument_bytes;
      const char* end = cursor + arguments.size;
      uint32_t pushed = 0;
      while (cursor < end) {
        while (cursor < end && (*cursor == ' ' || *cursor == ',')) ++cursor;
        if (cursor >= end) break;
        if (*cursor == '"') {
          char* out = result_scratch;
          uint32_t used = 0;
          ++cursor;
          while (cursor < end && *cursor != '"' && used + 1 < MAX_RESULT_BYTES) {
            if (*cursor == '\\' && cursor + 1 < end) ++cursor;
            out[used++] = *cursor++;
          }
          out[used] = 0;
          if (cursor < end) ++cursor;
          lua_pushlstring(lua_state, out, used);
          ++pushed;
        } else if (end - cursor >= 4 && memcmp(cursor, "true", 4) == 0) {
          lua_pushboolean(lua_state, 1);
          ++pushed;
          cursor += 4;
        } else if (end - cursor >= 5 && memcmp(cursor, "false", 5) == 0) {
          lua_pushboolean(lua_state, 0);
          ++pushed;
          cursor += 5;
        } else if (end - cursor >= 4 && memcmp(cursor, "null", 4) == 0) {
          lua_pushnil(lua_state);
          ++pushed;
          cursor += 4;
        } else {
          char* end_of_number = NULL;
          const double number = strtod(cursor, &end_of_number);
          if (end_of_number == cursor) {
            lua_settop(lua_state, 0);
            return write_envelope(&result, "{\"ok\":false,\"error\":\"arguments must be a JSON array of scalars", "", 0, "\"}");
          }
          lua_pushnumber(lua_state, number);
          ++pushed;
          cursor = end_of_number;
        }
        if (pushed >= 8) break;
      }
      lua_pcall(lua_state, (int)pushed, 1, 0);
    }
  } else {
    lua_pcall(lua_state, 0, 1, 0);
  }
  value_size = stringify_top(result_scratch, (uint32_t)sizeof(result_scratch), lua_state);
  lua_settop(lua_state, 0);
  return write_envelope(&result, "{\"ok\":true,\"value\":\"", result_scratch, value_size, "\"}");
}

/* ---- module ABI v1 ---- */

MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(mobagen_scripting_table, 10)
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
MOBAGEN_MODULE_EXPORT_ENTRY(mobagen_scripting_table, mobagen_scripting_error_v1, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_SPAN)
MOBAGEN_MODULE_EXPORT_TABLE_END(mobagen_scripting_table, mobagen_wasm_plugin_allocate_v1, mobagen_wasm_plugin_deallocate_v1,
                                mobagen_wasm_plugin_query_v1, mobagen_wasm_plugin_configure_v1, mobagen_wasm_plugin_start_v1,
                                mobagen_wasm_plugin_quiesce_v1, mobagen_wasm_plugin_stop_v1, mobagen_scripting_eval_v1,
                                mobagen_scripting_call_v1, mobagen_scripting_error_v1);
MOBAGEN_MODULE_EXPORT_PUBLISH_TABLE(mobagen_scripting_table)

MOBAGEN_WASM_GUEST_EXPORT MobagenModuleStatus MOBAGEN_MODULE_CALL mobagen_module_entry_v1(const MobagenModuleHostApiV1* host,
                                                                                          MobagenModuleDescriptorV1* descriptor) {
  (void)host;
  if (descriptor == NULL || descriptor->struct_size < MOBAGEN_MODULE_DESCRIPTOR_V1_SIZE) return MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT;
  descriptor->threads_policy = MOBAGEN_MODULE_THREADS_NONE;
  return MOBAGEN_MODULE_STATUS_OK;
}
