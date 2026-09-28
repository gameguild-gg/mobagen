/*
 * Freestanding-libc shim for the QuickJS wasm guest (todo 16).
 *
 * The guest is built with -nostdlib (wasi-sdk) or -sSTANDALONE_WASM (emcc)
 * so WAMR can load it as a zero-import module. QuickJS only needs a sliver
 * of libc: mem* helpers, integer->string, strtod, a few libm builtins and a
 * monotonic clock seed for its xorshift RNG. This header (forced-include in
 * BOTH guest builds) supplies that sliver without the wasi/emscripten
 * syscall surface.
 *
 * The trick: quickjs's own headers guard platform includes; the shim
 * defines the standard names directly (std* helpers come from the
 * compiler's freestanding <stddef.h>/<stdint.h>/<stdarg.h>), and
 * <sys/time.h>/<time.h>/<math.h> are satisfied through the macros below so
 * cutils.c's js__gettimeofday_us/js__hrtime_ns compile against shim types.
 *
 * quickjs-ng pin: v0.10.0 (MIT, fetched via CPM — external/quickjs-ng.cmake).
 */
#ifndef MOBAGEN_QUICKJS_FREESTANDING_SHIM_H
#define MOBAGEN_QUICKJS_FREESTANDING_SHIM_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#if defined(MOBAGEN_QUICKJS_SHIM_SKIP_DECLARATIONS)
/* emcc path: musl headers declared everything already. */
#else
/* stdio subset (integer + minimal float formatting). FILE is an opaque
   incomplete type: the streams are never dereferenced, only dropped. */
typedef struct _mobagen_shim_file MobagenFILEShim;
int snprintf(char* buffer, size_t size, const char* format, ...);
int fprintf(MobagenFILEShim* stream, const char* format, ...);
int printf(const char* format, ...);
int vsnprintf(char* buffer, size_t size, const char* format, va_list arguments);
int vfprintf(MobagenFILEShim* stream, const char* format, va_list arguments);
int putchar(int character);
int puts(const char* text);
int fputs(const char* text, MobagenFILEShim* stream);
int fputc(int character, MobagenFILEShim* stream);
size_t fwrite(const void* data, size_t size, size_t count, MobagenFILEShim* stream);

/* stdlib subset. */
double strtod(const char* text, char** end);
void abort(void);
void* malloc(size_t size);
void free(void* pointer);
void* realloc(void* pointer, size_t size);
void* calloc(size_t count, size_t size);
int abs(int value);
long labs(long value);

/* math subset (number parsing/formatting call sites). */
double pow(double base, double exponent);
double log2(double value);
double log(double value);
double exp(double value);
double fmod(double numerator, double denominator);
double sqrt(double value);
double ceil(double value);
double floor(double value);
double trunc(double value);
int isfinite(double value);
int isnan(double value);
int signbit(double value);
#endif

/*
 * <sys/time.h>/<time.h> interception: quickjs's cutils.c includes them
 * whenever _MSC_VER is unset. Redirect the include to a shim header that
 * declares only what the vendored sources touch.
 */
#define __MOBAGEN_TIME_HEADER_INTERCEPT__ 1

#endif /* MOBAGEN_QUICKJS_FREESTANDING_SHIM_H */
