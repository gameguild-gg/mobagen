/*
 * Freestanding-libc shim implementation for the Lua wasm guest (todo 17).
 * Mirrors modules/scripting_quickjs/freestanding_libc_shim.c with one
 * structural difference: MATH.
 *
 * wasm32 has no transcendental instructions, so `__builtin_sin/pow/...`
 * lower to libc CALLS — the QuickJS shim's __builtin math bodies would
 * be self-recursive here. Instead:
 *  - wasi guest: musl's f64 libm/ctype/time members come from the
 *    sysroot libc.a via lua_math_archive.cmake (import-free, correct).
 *    This shim defines only allocation + the FILE drop-sinks those
 *    members' stdio edges need (__fwritex/__towrite/...).
 *  - emcc guest: emscripten's musl sysroot already links correct math
 *    with -sSTANDALONE_WASM; the strong definitions below keep libc's
 *    wasi-importing members out of the link exactly like QuickJS.
 */
#if defined(__EMSCRIPTEN__)
/* musl's headers own the prototypes; include them BEFORE anything else so
   the shim's declarations never conflict (FILE* vs void* is a hard error). */
#  include <stdio.h>
#  include <stdlib.h>
#  include <time.h>
#  include <sys/time.h>
#  define MOBAGEN_LUA_SHIM_SKIP_DECLARATIONS 1
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

/* ---- memory allocation: bump allocator over static storage ---- */

#define SHIM_HEAP_BYTES (6u * 1024u * 1024u)
_Alignas(16) static unsigned char shim_heap[SHIM_HEAP_BYTES];
static uintptr_t shim_heap_cursor;

static void* shim_malloc(size_t size) {
  uintptr_t base = (uintptr_t)shim_heap;
  uintptr_t cursor = base + shim_heap_cursor;
  uintptr_t aligned = (cursor + 15u) & ~(uintptr_t)15u;
  if (size == 0) size = 1u;
  if (aligned + size > base + SHIM_HEAP_BYTES) return NULL;
  shim_heap_cursor = (aligned + size) - base;
  return (void*)aligned;
}

void* malloc(size_t size) { return shim_malloc(size); }

void* calloc(size_t count, size_t size) {
  void* pointer;
  size_t bytes;
  if (count != 0 && size > (size_t)-1 / count) return NULL;
  bytes = count * size;
  pointer = shim_malloc(bytes);
  if (pointer != NULL) memset(pointer, 0, bytes);
  return pointer;
}

void* realloc(void* pointer, size_t size) {
  /* Bump allocator: copy-and-leak (Lua's realloc callers grow; blocks
   * reset with the guest instance). */
  void* replacement;
  if (pointer == NULL) return shim_malloc(size);
  replacement = shim_malloc(size);
  if (replacement == NULL) return NULL;
  memcpy(replacement, pointer, size);
  return replacement;
}

void free(void* pointer) {
  (void)pointer;
}

void abort(void) {
  for (;;) {
    __builtin_trap();
  }
}

/* ---- wasi-libc seams (math archive members' stdio edges) ---- */

#if !defined(__EMSCRIPTEN__)
/* vfprintf's write path goes through libc.a's own __fwritex into
   snprintf's in-memory sink — no shim drop-sinks for it. The static stdout
   vtable references fd_close/fd_seek through the exported indirect table
   (unreachable from our exports); wasm-opt strips those imports
   POST_BUILD, exactly like the QuickJS guest. */
void __stdio_exit_needed(void);
void __stdio_exit_needed(void) {}

int __lockfile(void* stream);
int __lockfile(void* stream) {
  (void)stream;
  return 1;
}

void __unlockfile(void* stream);
void __unlockfile(void* stream) {
  (void)stream;
}

/* libc.a's static stdin/stdout/stderr FILE vtables reference the wasi
   syscall edges; the scripting exports never touch those streams, so
   these bodies replace them with import-free failures. Keeping the
   symbols defined avoids pulling __stdio_read/write/close's fd imports
   into the link. */
int __stdio_read(void* stream, unsigned char* buffer, size_t count);
int __stdio_read(void* stream, unsigned char* buffer, size_t count) {
  (void)stream;
  (void)buffer;
  (void)count;
  return 0;
}

int __stdio_close(void* stream);
int __stdio_close(void* stream) {
  (void)stream;
  return 0;
}

int __stdio_write(void* stream, const unsigned char* buffer, size_t count);
int __stdio_write(void* stream, const unsigned char* buffer, size_t count) {
  (void)stream;
  (void)buffer;
  (void)count;
  return 0;
}

int __isatty(int fd);
int __isatty(int fd) {
  (void)fd;
  return 0;
}

long long __stdio_seek(void* stream, long long offset, int whence);
long long __stdio_seek(void* stream, long long offset, int whence) {
  (void)stream;
  (void)offset;
  (void)whence;
  return -1;
}

/* File-opening edges stay shim drop-fails: libc.a's real implementations
   pull wasi path_open/fd_renumber imports, and the io library is never
   opened (linit.c patch) — luaL_loadfilex just sees NULL. */
void* fopen(const char* path, const char* mode);
void* fopen(const char* path, const char* mode) {
  (void)path;
  (void)mode;
  return NULL;
}

void* freopen(const char* path, const char* mode, void* stream);
void* freopen(const char* path, const char* mode, void* stream) {
  (void)path;
  (void)mode;
  (void)stream;
  return NULL;
}

int getc(void* stream);
int getc(void* stream) {
  (void)stream;
  return -1;
}

int ungetc(int character, void* stream);
int ungetc(int character, void* stream) {
  (void)character;
  (void)stream;
  return -1;
}

/* The lauxlib file surface (luaL_loadfilex / luaL_fileresult) still
   references the stdio entry points even though the io/os libraries are
   not opened (linit.c patch). stdin/stdout/stderr + the FILE family come
   from libc.a members (lua_math_archive.cmake); the opens themselves are
   never exercised by the scripting exports. */

int setvbuf(void* stream, char* buffer, int mode, size_t size);
int setvbuf(void* stream, char* buffer, int mode, size_t size) {
  (void)stream;
  (void)buffer;
  (void)mode;
  (void)size;
  return 0;
}

int remove(const char* path);
int remove(const char* path) {
  (void)path;
  return -1;
}

int rename(const char* from, const char* to);
int rename(const char* from, const char* to) {
  (void)from;
  (void)to;
  return -1;
}

int system(const char* command);
int system(const char* command) {
  (void)command;
  return -1;
}

char* getenv(const char* name);
char* getenv(const char* name) {
  (void)name;
  return NULL;
}

char* strerror(int error);
char* strerror(int error) {
  (void)error;
  return (char*)"error";
}

int fflush(void* stream);
int fflush(void* stream) {
  (void)stream;
  return 0;
}

int fputc(int character, void* stream);
int fputc(int character, void* stream) {
  (void)stream;
  return character;
}

char* fgets(char* destination, int size, void* stream);
char* fgets(char* destination, int size, void* stream) {
  (void)stream;
  if (size > 0) destination[0] = '\0';
  return NULL;
}

/* ---- string family (compilers emit libcalls for these too) ---- */

size_t strlen(const char* text);
size_t strlen(const char* text) {
  size_t length = 0;
  while (text[length] != '\0') ++length;
  return length;
}

int strcmp(const char* left, const char* right);
int strcmp(const char* left, const char* right) {
  while (*left != '\0' && *left == *right) {
    ++left;
    ++right;
  }
  return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int strncmp(const char* left, const char* right, size_t count);
int strncmp(const char* left, const char* right, size_t count) {
  while (count > 0 && *left != '\0' && *left == *right) {
    ++left;
    ++right;
    --count;
  }
  return count == 0 ? 0 : (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int memcmp(const void* left, const void* right, size_t count);
int memcmp(const void* left, const void* right, size_t count) {
  const unsigned char* a = (const unsigned char*)left;
  const unsigned char* b = (const unsigned char*)right;
  size_t index;
  for (index = 0; index < count; ++index) {
    if (a[index] != b[index]) return (int)a[index] - (int)b[index];
  }
  return 0;
}

void* memcpy(void* destination, const void* source, size_t count);
void* memcpy(void* destination, const void* source, size_t count) {
  unsigned char* out = (unsigned char*)destination;
  const unsigned char* in = (const unsigned char*)source;
  size_t index;
  for (index = 0; index < count; ++index) out[index] = in[index];
  return destination;
}

void* memmove(void* destination, const void* source, size_t count);
void* memmove(void* destination, const void* source, size_t count) {
  unsigned char* out = (unsigned char*)destination;
  const unsigned char* in = (const unsigned char*)source;
  size_t index;
  if (out < in) {
    for (index = 0; index < count; ++index) out[index] = in[index];
  } else if (out > in) {
    for (index = count; index > 0; --index) out[index - 1] = in[index - 1];
  }
  return destination;
}

void* memset(void* destination, int value, size_t count);
void* memset(void* destination, int value, size_t count) {
  unsigned char* out = (unsigned char*)destination;
  size_t index;
  for (index = 0; index < count; ++index) out[index] = (unsigned char)value;
  return destination;
}

void* memchr(const void* text, int character, size_t count);
void* memchr(const void* text, int character, size_t count) {
  const unsigned char* bytes = (const unsigned char*)text;
  size_t index;
  for (index = 0; index < count; ++index) {
    if (bytes[index] == (unsigned char)character) return (void*)&bytes[index];
  }
  return NULL;
}

char* strchr(const char* text, int character);
char* strchr(const char* text, int character) {
  for (;;) {
    if (*text == (char)character) return (char*)text;
    if (*text == '\0') return NULL;
    ++text;
  }
}

char* strrchr(const char* text, int character);
char* strrchr(const char* text, int character) {
  const char* last = NULL;
  for (;;) {
    if (*text == (char)character) last = text;
    if (*text == '\0') return (char*)last;
    ++text;
  }
}

char* strstr(const char* haystack, const char* needle);
char* strstr(const char* haystack, const char* needle) {
  const size_t needle_length = strlen(needle);
  if (needle_length == 0) return (char*)haystack;
  for (; *haystack != '\0'; ++haystack) {
    if (strncmp(haystack, needle, needle_length) == 0) return (char*)haystack;
  }
  return NULL;
}

size_t strspn(const char* text, const char* accept);
size_t strspn(const char* text, const char* accept) {
  size_t length = 0;
  while (text[length] != '\0' && strchr(accept, text[length]) != NULL) ++length;
  return length;
}

size_t strcspn(const char* text, const char* reject);
size_t strcspn(const char* text, const char* reject) {
  size_t length = 0;
  while (text[length] != '\0' && strchr(reject, text[length]) == NULL) ++length;
  return length;
}

char* strpbrk(const char* text, const char* separators);
char* strpbrk(const char* text, const char* separators) {
  for (; *text != '\0'; ++text) {
    if (strchr(separators, *text) != NULL) return (char*)text;
  }
  return NULL;
}

char* strcpy(char* destination, const char* source);
char* strcpy(char* destination, const char* source) {
  char* out = destination;
  while ((*out++ = *source++) != '\0') {
  }
  return destination;
}

char* strncpy(char* destination, const char* source, size_t count);
char* strncpy(char* destination, const char* source, size_t count) {
  char* out = destination;
  while (count > 0 && (*out++ = *source++) != '\0') --count;
  while (count > 0) {
    *out++ = '\0';
    --count;
  }
  return destination;
}

char* strcat(char* destination, const char* source);
char* strcat(char* destination, const char* source) {
  char* out = destination + strlen(destination);
  while ((*out++ = *source++) != '\0') {
  }
  return destination;
}

/* wasi-libc errno is a _Thread_local int on wasip1. */
_Thread_local int errno;
#endif

/* ---- time (deterministic seeds; mirrors the QuickJS shim clock).
 * wasi-libc time_t is long long (64-bit) — match the signature the
 * sysroot headers declare or wasm-ld emits signature_mismatch traps. */

uint64_t mobagen_lua_shim_monotonic_ns(void);
uint64_t mobagen_lua_shim_monotonic_ns(void) {
  static uint32_t counter;
  counter += 0x9e3779b9u;
  return 0x100000001b3ull * counter;
}

/* wasi-libc declares time_t as long long; emscripten's musl uses its own
 * time_t/clock_t (its <time.h> is already included at the top under
 * __EMSCRIPTEN__). Match the declaring headers exactly or wasm-ld emits
 * signature_mismatch traps at the callsites. */
#if defined(__EMSCRIPTEN__)
typedef time_t mobagen_lua_time_t;
typedef clock_t mobagen_lua_clock_t;
#else
typedef long long mobagen_lua_time_t;
typedef long mobagen_lua_clock_t;
#endif

mobagen_lua_time_t time(mobagen_lua_time_t* out);
mobagen_lua_time_t time(mobagen_lua_time_t* out) {
  const mobagen_lua_time_t seconds = (mobagen_lua_time_t)(mobagen_lua_shim_monotonic_ns() / 1000000000ull);
  if (out != NULL) *out = seconds;
  return seconds;
}

mobagen_lua_clock_t clock(void);
mobagen_lua_clock_t clock(void) {
  return (mobagen_lua_clock_t)(mobagen_lua_shim_monotonic_ns() / 1000ull);
}
