#ifndef MOBAGEN_MODULE_ABI_H
#define MOBAGEN_MODULE_ABI_H

/*
 * Mobagen wasm module ABI v1.
 *
 * C11-only on purpose, mirroring the native plugin ABI
 * (mobagen/plugin/plugin_abi.h): versioned structs with struct_size checks,
 * extern C guards, and coarse calls only. Per-frame hot-path work never
 * crosses the module boundary.
 *
 * Guests annotate exported functions inside a MOBAGEN_MODULE_EXPORT_TABLE
 * block. The table is a compile-time constant exported under the canonical
 * symbol MOBAGEN_MODULE_TABLE_EXPORT_SYMBOL_V1; the signature-extraction tool
 * (todo 3) and the module manifest (todo 2) stay in sync with it. The exact
 * same macros compile unchanged in C++ translation units.
 *
 * Signature-id layout (32 bits):
 *
 *   [31:24] magic 0x4D        (rejects garbage ids)
 *   [23:20] parameter count   0..5
 *   [19:17] return type code
 *   [16:2]  parameter codes, 3 bits each, 5 slots (bit 14 = first param)
 *   [1:0]   reserved, zero    (decoder rejects otherwise)
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define MOBAGEN_MODULE_CALL __cdecl
#  if defined(MOBAGEN_MODULE_BUILD)
#    define MOBAGEN_MODULE_EXPORT __declspec(dllexport)
#  else
#    define MOBAGEN_MODULE_EXPORT
#  endif
#elif defined(MOBAGEN_MODULE_BUILD) && (defined(__GNUC__) || defined(__clang__))
#  define MOBAGEN_MODULE_CALL
#  define MOBAGEN_MODULE_EXPORT __attribute__((visibility("default")))
#else
#  define MOBAGEN_MODULE_CALL
#  define MOBAGEN_MODULE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_MODULE_ABI_VERSION UINT32_C(1)
#define MOBAGEN_MODULE_ENTRY_V1_SYMBOL "mobagen_module_entry_v1"
#define MOBAGEN_MODULE_TABLE_EXPORT_SYMBOL_V1 "mobagen_module_exports_v1"

/* Canonical import module name for host services exposed to guests. */
#define MOBAGEN_MODULE_IMPORT_MODULE_V1 "mobagen_module_v1"

typedef uint32_t MobagenModuleStatus;
#define MOBAGEN_MODULE_STATUS_OK UINT32_C(0)
#define MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define MOBAGEN_MODULE_STATUS_UNSUPPORTED UINT32_C(2)
#define MOBAGEN_MODULE_STATUS_FAILED UINT32_C(3)
#define MOBAGEN_MODULE_STATUS_OUT_OF_MEMORY UINT32_C(4)
#define MOBAGEN_MODULE_STATUS_CONFLICT UINT32_C(5)
#define MOBAGEN_MODULE_STATUS_NOT_FOUND UINT32_C(6)

/*
 * Threading contract (feeds the GC quiesce policy, todo 12):
 *
 *   THREADS_NONE     Default. The module must not spawn threads; the host
 *                    resolves no thread-spawning import for it, so spawning
 *                    is impossible by construction.
 *   THREADS_MANAGED  The module may spawn threads ONLY through the
 *                    host-provided import named
 *                    MOBAGEN_MODULE_THREAD_SPAWN_IMPORT_V1 (resolved for
 *                    managed modules only) and MUST export the per-thread
 *                    quiesce handshake named
 *                    MOBAGEN_MODULE_THREAD_QUIESCE_EXPORT_V1 so the GC can
 *                    park each guest thread at a safe point before marking.
 */
typedef uint32_t MobagenModuleThreadsPolicy;
#define MOBAGEN_MODULE_THREADS_NONE UINT32_C(0)
#define MOBAGEN_MODULE_THREADS_MANAGED UINT32_C(1)

#define MOBAGEN_MODULE_THREAD_SPAWN_IMPORT_V1 "mobagen_thread_spawn_v1"
#define MOBAGEN_MODULE_THREAD_QUIESCE_EXPORT_V1 "mobagen_module_thread_quiesce_v1"

/* Linear-memory exchange types: offsets and spans, never native pointers. */
typedef struct MobagenModuleSpan32 {
  uint32_t offset;
  uint32_t size;
} MobagenModuleSpan32;

/* ---- Signature-id type codes (3 bits) ---- */

#define MOBAGEN_MODULE_T_VOID UINT32_C(0)
#define MOBAGEN_MODULE_T_I32 UINT32_C(1)
#define MOBAGEN_MODULE_T_I64 UINT32_C(2)
#define MOBAGEN_MODULE_T_F32 UINT32_C(3)
#define MOBAGEN_MODULE_T_F64 UINT32_C(4)
#define MOBAGEN_MODULE_T_PTR UINT32_C(5)
#define MOBAGEN_MODULE_T_SPAN UINT32_C(6)

#define MOBAGEN_MODULE_MAX_EXPORT_PARAMS UINT32_C(5)
#define MOBAGEN_MODULE_SIG_MAGIC UINT32_C(0x4D)

#define MOBAGEN_MODULE_SIG_PACK(code_, shift_) ((UINT32_C(0xFF) & (code_)) << (shift_))

/* Constant-expression encoders; the RETURN type comes first. */
#define MOBAGEN_MODULE_SIG_0(ret_) (UINT32_C(0x4D000000) | MOBAGEN_MODULE_SIG_PACK(ret_, 17))
#define MOBAGEN_MODULE_SIG_1(ret_, p1_) (UINT32_C(0x4D100000) | MOBAGEN_MODULE_SIG_PACK(ret_, 17) | MOBAGEN_MODULE_SIG_PACK(p1_, 14))
#define MOBAGEN_MODULE_SIG_2(ret_, p1_, p2_) \
  (UINT32_C(0x4D200000) | MOBAGEN_MODULE_SIG_PACK(ret_, 17) | MOBAGEN_MODULE_SIG_PACK(p1_, 14) | MOBAGEN_MODULE_SIG_PACK(p2_, 11))
#define MOBAGEN_MODULE_SIG_3(ret_, p1_, p2_, p3_)                                                                                 \
  (UINT32_C(0x4D300000) | MOBAGEN_MODULE_SIG_PACK(ret_, 17) | MOBAGEN_MODULE_SIG_PACK(p1_, 14) | MOBAGEN_MODULE_SIG_PACK(p2_, 11) \
   | MOBAGEN_MODULE_SIG_PACK(p3_, 8))
#define MOBAGEN_MODULE_SIG_4(ret_, p1_, p2_, p3_, p4_)                                                                            \
  (UINT32_C(0x4D400000) | MOBAGEN_MODULE_SIG_PACK(ret_, 17) | MOBAGEN_MODULE_SIG_PACK(p1_, 14) | MOBAGEN_MODULE_SIG_PACK(p2_, 11) \
   | MOBAGEN_MODULE_SIG_PACK(p3_, 8) | MOBAGEN_MODULE_SIG_PACK(p4_, 5))
#define MOBAGEN_MODULE_SIG_5(ret_, p1_, p2_, p3_, p4_, p5_)                                                                       \
  (UINT32_C(0x4D500000) | MOBAGEN_MODULE_SIG_PACK(ret_, 17) | MOBAGEN_MODULE_SIG_PACK(p1_, 14) | MOBAGEN_MODULE_SIG_PACK(p2_, 11) \
   | MOBAGEN_MODULE_SIG_PACK(p3_, 8) | MOBAGEN_MODULE_SIG_PACK(p4_, 5) | MOBAGEN_MODULE_SIG_PACK(p5_, 2))

/* Runtime encode/decode (pure; decode returns 0 for malformed ids). */
static inline uint32_t mobagen_module_signature_encode(uint32_t return_code, const uint32_t* param_codes, uint32_t param_count) {
  if (param_count > MOBAGEN_MODULE_MAX_EXPORT_PARAMS) return UINT32_C(0);
  uint32_t id = UINT32_C(0x4D000000) | MOBAGEN_MODULE_SIG_PACK(return_code, 17) | (param_count << 20);
  for (uint32_t i = 0; i < param_count; ++i) {
    if (param_codes[i] > MOBAGEN_MODULE_T_SPAN) return UINT32_C(0);
    id |= MOBAGEN_MODULE_SIG_PACK(param_codes[i], 14 - 3 * i);
  }
  return id;
}

static inline int mobagen_module_signature_decode(uint32_t signature_id, uint32_t* return_code, uint32_t* param_count) {
  if ((signature_id >> 24) != MOBAGEN_MODULE_SIG_MAGIC) return 0;
  if ((signature_id & UINT32_C(3)) != 0) return 0;
  uint32_t count = (signature_id >> 20) & UINT32_C(0xF);
  if (count > MOBAGEN_MODULE_MAX_EXPORT_PARAMS) return 0;
  *param_count = count;
  *return_code = (signature_id >> 17) & UINT32_C(7);
  return 1;
}

static inline uint32_t mobagen_module_signature_param(uint32_t signature_id, uint32_t index) {
  if (index >= MOBAGEN_MODULE_MAX_EXPORT_PARAMS) return MOBAGEN_MODULE_T_VOID;
  return (signature_id >> (14 - 3 * index)) & UINT32_C(7);
}

static inline int mobagen_module_threads_spawn_allowed(MobagenModuleThreadsPolicy policy) { return policy == MOBAGEN_MODULE_THREADS_MANAGED; }

static inline int mobagen_module_threads_requires_quiesce(MobagenModuleThreadsPolicy policy) { return policy == MOBAGEN_MODULE_THREADS_MANAGED; }

/*
 * Versioned host API passed to the entry symbol. The guest must verify
 * struct_size >= MOBAGEN_MODULE_HOST_API_V1_SIZE and abi_version ==
 * MOBAGEN_MODULE_ABI_VERSION before using any field.
 */
typedef struct MobagenModuleHostApiV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void* module_context;
} MobagenModuleHostApiV1;

/* Generic function-ptr carrier; callers marshal through the signature-id. */
typedef void MOBAGEN_MODULE_CALL (*MobagenModuleAnyFn)(void);

typedef struct MobagenModuleExportEntryV1 {
  const char* name; /* export symbol name, as emitted by the annotation */
  MobagenModuleAnyFn fn;
  uint32_t signature_id;
  uint32_t reserved; /* must be zero; reserved for future flags */
} MobagenModuleExportEntryV1;

typedef uint32_t MobagenModuleExportTableKind;
#define MOBAGEN_MODULE_TABLE_V1 UINT32_C(0x4D313242) /* "M12B" */

/*
 * ABI view of an annotation table. The per-TU table type generated by the
 * macros below is layout-compatible: same header fields, then `capacity`
 * entries instead of the single placeholder entry.
 */
typedef struct MobagenModuleExportTableV1 {
  uint32_t kind; /* MOBAGEN_MODULE_TABLE_V1 */
  uint32_t abi_version;
  uint32_t entry_count;
  uint32_t reserved; /* must be zero */
  MobagenModuleExportEntryV1 entries[1];
} MobagenModuleExportTableV1;

typedef struct MobagenModuleDescriptorV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  MobagenModuleThreadsPolicy threads_policy;
} MobagenModuleDescriptorV1;

#define MOBAGEN_MODULE_HOST_API_V1_SIZE ((uint32_t)sizeof(MobagenModuleHostApiV1))
#define MOBAGEN_MODULE_DESCRIPTOR_V1_SIZE ((uint32_t)sizeof(MobagenModuleDescriptorV1))

typedef MobagenModuleStatus(MOBAGEN_MODULE_CALL* MobagenModuleEntryV1Fn)(const MobagenModuleHostApiV1* host, MobagenModuleDescriptorV1* descriptor);

MOBAGEN_MODULE_EXPORT MobagenModuleStatus MOBAGEN_MODULE_CALL mobagen_module_entry_v1(const MobagenModuleHostApiV1* host,
                                                                                      MobagenModuleDescriptorV1* descriptor);

/*
 * ---- Export annotation macros ----
 *
 *   MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(my_table, 2)
 *   MOBAGEN_MODULE_EXPORT_ENTRY(my_table, fn_a, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_PTR)
 *   MOBAGEN_MODULE_EXPORT_ENTRY(my_table, fn_b, MOBAGEN_MODULE_T_VOID)
 *   MOBAGEN_MODULE_EXPORT_TABLE_END(my_table, fn_a, fn_b)
 *
 * Each ENTRY declares a file-scope const entry (designated-initializer
 * style) tagged with name + signature-id; END assembles the versioned table
 * from the listed entries. An ENTRY with NO type code fails to compile via a
 * macro static_assert naming the missing signature_id field; an END list that
 * disagrees with the declared capacity fails to compile naming entry_count.
 */

#define MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(table_, capacity_) \
  typedef struct {                                           \
    uint32_t kind;                                           \
    uint32_t abi_version;                                    \
    uint32_t entry_count;                                    \
    uint32_t reserved;                                       \
    MobagenModuleExportEntryV1 entries[capacity_];           \
  } table_##_type;                                           \
  enum { table_##_capacity = (capacity_) };

/* Sentinel-prefixed arity count. Zero variadic args need comma elision:
   __VA_OPT__ on C++20/C23, GNU ## elision otherwise (clang/gcc default to
   gnu modes in this repo's CMake; MSVC's traditional preprocessor elides too). */
#if (defined(__cplusplus) && __cplusplus >= 202002L) || (!defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
#  define MOBAGEN_MODULE_ENTRY_NARG(a1_, a2_, a3_, a4_, a5_, a6_, a7_, a8_, a9_, a10_, a11_, count_, ...) count_
#  define MOBAGEN_MODULE_ENTRY_COUNT(...) MOBAGEN_MODULE_ENTRY_NARG(0 __VA_OPT__(, __VA_ARGS__), 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#else
#  define MOBAGEN_MODULE_ENTRY_NARG(a1_, a2_, a3_, a4_, a5_, a6_, a7_, a8_, a9_, a10_, a11_, count_, ...) count_
#  define MOBAGEN_MODULE_ENTRY_COUNT(...) MOBAGEN_MODULE_ENTRY_NARG(0, ##__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#endif
#define MOBAGEN_MODULE_ENTRY_CAT2(a_, b_) a_##b_
#define MOBAGEN_MODULE_ENTRY_CAT(a_, b_) MOBAGEN_MODULE_ENTRY_CAT2(a_, b_)

#ifdef __cplusplus
#  define MOBAGEN_MODULE_STATIC_ASSERT(cond_, msg_) static_assert(cond_, msg_)
#else
#  define MOBAGEN_MODULE_STATIC_ASSERT(cond_, msg_) _Static_assert(cond_, msg_)
#endif

#define MOBAGEN_MODULE_EXPORT_ENTRY(table_, fn_, ...) \
  MOBAGEN_MODULE_ENTRY_CAT(MOBAGEN_MODULE_ENTRY_, MOBAGEN_MODULE_ENTRY_COUNT(__VA_ARGS__))(table_, fn_, ##__VA_ARGS__)

#define MOBAGEN_MODULE_ENTRY_0(table_, fn_, ...)                                                                  \
  MOBAGEN_MODULE_STATIC_ASSERT(0,                                                                                 \
                               "MOBAGEN_MODULE_EXPORT_ENTRY: missing required field signature_id - annotate the " \
                               "return type code after the function name (e.g. MOBAGEN_MODULE_T_I32, then "       \
                               "parameter codes)")

#define MOBAGEN_MODULE_ENTRY_1(table_, fn_, ret_) \
  static const MobagenModuleExportEntryV1 table_##_entry_##fn_ = {#fn_, (MobagenModuleAnyFn)(fn_), MOBAGEN_MODULE_SIG_0(ret_), 0U};
#define MOBAGEN_MODULE_ENTRY_2(table_, fn_, ret_, p1_) \
  static const MobagenModuleExportEntryV1 table_##_entry_##fn_ = {#fn_, (MobagenModuleAnyFn)(fn_), MOBAGEN_MODULE_SIG_1(ret_, p1_), 0U};
#define MOBAGEN_MODULE_ENTRY_3(table_, fn_, ret_, p1_, p2_) \
  static const MobagenModuleExportEntryV1 table_##_entry_##fn_ = {#fn_, (MobagenModuleAnyFn)(fn_), MOBAGEN_MODULE_SIG_2(ret_, p1_, p2_), 0U};
#define MOBAGEN_MODULE_ENTRY_4(table_, fn_, ret_, p1_, p2_, p3_) \
  static const MobagenModuleExportEntryV1 table_##_entry_##fn_ = {#fn_, (MobagenModuleAnyFn)(fn_), MOBAGEN_MODULE_SIG_3(ret_, p1_, p2_, p3_), 0U};
#define MOBAGEN_MODULE_ENTRY_5(table_, fn_, ret_, p1_, p2_, p3_, p4_) \
  static const MobagenModuleExportEntryV1 table_##_entry_##fn_        \
      = {#fn_, (MobagenModuleAnyFn)(fn_), MOBAGEN_MODULE_SIG_4(ret_, p1_, p2_, p3_, p4_), 0U};
#define MOBAGEN_MODULE_ENTRY_6(table_, fn_, ret_, p1_, p2_, p3_, p4_, p5_) \
  static const MobagenModuleExportEntryV1 table_##_entry_##fn_             \
      = {#fn_, (MobagenModuleAnyFn)(fn_), MOBAGEN_MODULE_SIG_5(ret_, p1_, p2_, p3_, p4_, p5_), 0U};

#define MOBAGEN_MODULE_TABLE_LIST_1(table_, a_) \
  { table_##_entry_##a_ }
#define MOBAGEN_MODULE_TABLE_LIST_2(table_, a_, b_) \
  { table_##_entry_##a_, table_##_entry_##b_ }
#define MOBAGEN_MODULE_TABLE_LIST_3(table_, a_, b_, c_) \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_ }
#define MOBAGEN_MODULE_TABLE_LIST_4(table_, a_, b_, c_, d_) \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_, table_##_entry_##d_ }
#define MOBAGEN_MODULE_TABLE_LIST_5(table_, a_, b_, c_, d_, e_) \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_, table_##_entry_##d_, table_##_entry_##e_ }
#define MOBAGEN_MODULE_TABLE_LIST_6(table_, a_, b_, c_, d_, e_, f_) \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_, table_##_entry_##d_, table_##_entry_##e_, table_##_entry_##f_ }
#define MOBAGEN_MODULE_TABLE_LIST_7(table_, a_, b_, c_, d_, e_, f_, g_) \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_, table_##_entry_##d_, table_##_entry_##e_, table_##_entry_##f_, table_##_entry_##g_ }
#define MOBAGEN_MODULE_TABLE_LIST_8(table_, a_, b_, c_, d_, e_, f_, g_, h_)                                        \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_, table_##_entry_##d_, table_##_entry_##e_,        \
    table_##_entry_##f_, table_##_entry_##g_, table_##_entry_##h_ }
#define MOBAGEN_MODULE_TABLE_LIST_9(table_, a_, b_, c_, d_, e_, f_, g_, h_, i_)                                    \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_, table_##_entry_##d_, table_##_entry_##e_,        \
    table_##_entry_##f_, table_##_entry_##g_, table_##_entry_##h_, table_##_entry_##i_ }
#define MOBAGEN_MODULE_TABLE_LIST_10(table_, a_, b_, c_, d_, e_, f_, g_, h_, i_, j_)                               \
  { table_##_entry_##a_, table_##_entry_##b_, table_##_entry_##c_, table_##_entry_##d_, table_##_entry_##e_,        \
    table_##_entry_##f_, table_##_entry_##g_, table_##_entry_##h_, table_##_entry_##i_, table_##_entry_##j_ }

#define MOBAGEN_MODULE_EXPORT_TABLE_END(table_, ...)                                                                                     \
  static const table_##_type table_                                                                                                      \
      = {MOBAGEN_MODULE_TABLE_V1, MOBAGEN_MODULE_ABI_VERSION, (uint32_t)MOBAGEN_MODULE_ENTRY_COUNT(__VA_ARGS__), 0U,                     \
         MOBAGEN_MODULE_ENTRY_CAT(MOBAGEN_MODULE_TABLE_LIST_, MOBAGEN_MODULE_ENTRY_COUNT(__VA_ARGS__))(table_, ##__VA_ARGS__)};          \
  MOBAGEN_MODULE_STATIC_ASSERT(MOBAGEN_MODULE_ENTRY_COUNT(__VA_ARGS__) == table_##_capacity,                                             \
                                "MOBAGEN_MODULE_EXPORT_TABLE_END: entry_count mismatch - the entry list must name exactly the functions " \
                                "annotated since TABLE_BEGIN")

/*
 * Wasm discoverability bridge (additive; consumed by the extraction tool).
 *
 * A C struct array in linear memory cannot be a wasm export on its own. This
 * declares the canonical export symbol MOBAGEN_MODULE_TABLE_EXPORT_SYMBOL_V1
 * as a const global holding the table's linear-memory OFFSET. wasm-ld turns
 * that global's address into an immutable wasm global; the linker may fold
 * the address into the global init expression (global init == table offset,
 * read the table directly at that address) or keep the four storage bytes in
 * linear memory (storage at the global's address holds the table offset —
 * read a u32 there first and use it as the table address). Both shapes are
 * produced by wasm-ld depending on its garbage collection; consumers resolve
 * the export, then follow one indirection at most. `used` keeps the storage
 * alive in the second shape. Invoke AFTER MOBAGEN_MODULE_EXPORT_TABLE_END —
 * the table must be complete.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define MOBAGEN_MODULE_EXPORT_PUBLISH_TABLE(table_) \
    __attribute__((used, visibility("default"))) const uintptr_t mobagen_module_exports_v1 = (uintptr_t)&table_;
#else
#  define MOBAGEN_MODULE_EXPORT_PUBLISH_TABLE(table_) const uintptr_t mobagen_module_exports_v1 = (uintptr_t)&table_;
#endif

#ifdef __cplusplus
} /* extern "C" */

namespace mobagen {
  namespace module {

    /* constexpr meta over the signature-id encoding (C++ tooling side). */
    struct SignatureIdV1 {
      uint32_t value;

      constexpr uint32_t magic() const { return value >> 24; }
      constexpr uint32_t param_count() const { return (value >> 20) & 0xFU; }
      constexpr uint32_t return_code() const { return (value >> 17) & 0x7U; }
      constexpr uint32_t param_code(uint32_t index) const { return index < 5 ? (value >> (14 - 3 * index)) & 0x7U : 0U; }
      constexpr bool valid() const { return magic() == 0x4DU && (value & 3U) == 0 && param_count() <= 5 && return_code() <= 6; }
    };

  }  // namespace module
}  // namespace mobagen
#endif

#endif /* MOBAGEN_MODULE_ABI_H */
