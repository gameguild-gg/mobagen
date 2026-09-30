/*
 * Freestanding-libc shim implementation for the QuickJS wasm guest (todo 16).
 * See freestanding_libc_shim.h for the rationale.
 *
 * ONE body, TWO header regimes:
 *  - wasi-sdk -nostdlib: declarations come from the shadow headers in
 *    freestanding_headers/ (found via -isystem).
 *  - emcc -sSTANDALONE_WASM: declarations come from emscripten's musl
 *    sysroot. The STRONG definitions below satisfy every libc reference
 *    quickjs makes, so the musl archive members that would otherwise be
 *    pulled (clock_gettime, stdio, malloc) — and the wasi_snapshot_preview1
 *    imports they carry — never enter the link. The guest stays import-free
 *    for both WAMR and the browser backend.
 */
#if defined(__EMSCRIPTEN__)
/* musl's headers own the prototypes; include them BEFORE the shim header so
   its declarations never conflict (FILE* vs void* would be a hard error). */
#  include <stdio.h>
#  include <stdlib.h>
#  include <time.h>
#  include <sys/time.h>
#  define MOBAGEN_QUICKJS_SHIM_SKIP_DECLARATIONS 1
#endif

#include "freestanding_libc_shim.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#if !defined(__EMSCRIPTEN__)
#  include <stdio.h>   /* shadow: FILE + stdout/stderr macros */
#  include <stdlib.h>
#  include <string.h>
#  include <time.h>
#  include <sys/time.h>
#endif

/* ---- memory allocation: bump allocator over static storage ---- */

#define SHIM_HEAP_BYTES (6u * 1024u * 1024u)
_Alignas(16) static unsigned char shim_heap[SHIM_HEAP_BYTES];
static uintptr_t shim_heap_cursor;

uint64_t mobagen_quickjs_shim_monotonic_ns(void);

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
  if (pointer != NULL) {
    unsigned char* out = (unsigned char*)pointer;
    size_t index;
    for (index = 0; index < bytes; ++index) out[index] = 0;
  }
  return pointer;
}

void* realloc(void* pointer, size_t size) {
  /* The bump allocator cannot grow in place; copy-and-leak into the arena
   * (quickjs's realloc callers always grow; old blocks reset with the
   * guest instance). */
  void* replacement;
  if (pointer == NULL) return shim_malloc(size);
  replacement = shim_malloc(size);
  if (replacement == NULL) return NULL;
  {
    const unsigned char* source = (const unsigned char*)pointer;
    unsigned char* destination = (unsigned char*)replacement;
    size_t index;
    for (index = 0; index < size; ++index) destination[index] = source[index];
  }
  return replacement;
}

void free(void* pointer) {
  (void)pointer;
}

int abs(int value) { return value < 0 ? -value : value; }

long labs(long value) { return value < 0 ? -value : value; }

void abort(void) {
  for (;;) {
    __builtin_trap();
  }
}

/* ---- minimal strtod ---- */

static double shim_pow10(int exponent) {
  double result = 1.0;
  int index;
  if (exponent >= 0) {
    for (index = 0; index < exponent; ++index) result *= 10.0;
  } else {
    for (index = 0; index > exponent; --index) result /= 10.0;
  }
  return result;
}

double strtod(const char* text, char** end) {
  const char* cursor = text;
  int negative = 0;
  double value = 0.0;
  int exponent = 0;
  int seen_digit = 0;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
  if (*cursor == '+' || *cursor == '-') {
    negative = *cursor == '-';
    ++cursor;
  }
  while (*cursor >= '0' && *cursor <= '9') {
    value = value * 10.0 + (double)(*cursor - '0');
    ++cursor;
    seen_digit = 1;
  }
  if (*cursor == '.') {
    ++cursor;
    while (*cursor >= '0' && *cursor <= '9') {
      value = value * 10.0 + (double)(*cursor - '0');
      --exponent;
      ++cursor;
      seen_digit = 1;
    }
  }
  if (!seen_digit) {
    if (end != NULL) *end = (char*)text;
    return 0.0;
  }
  if (*cursor == 'e' || *cursor == 'E') {
    const char* save = cursor;
    int exponent_negative = 0;
    int explicit_exponent = 0;
    int exponent_digits = 0;
    ++cursor;
    if (*cursor == '+' || *cursor == '-') {
      exponent_negative = *cursor == '-';
      ++cursor;
    }
    while (*cursor >= '0' && *cursor <= '9') {
      if (explicit_exponent < 100000) explicit_exponent = explicit_exponent * 10 + (*cursor - '0');
      ++cursor;
      ++exponent_digits;
    }
    if (exponent_digits > 0) {
      exponent += exponent_negative ? -explicit_exponent : explicit_exponent;
    } else {
      cursor = save;
    }
  }
  if (end != NULL) *end = (char*)cursor;
  value *= shim_pow10(exponent);
  return negative ? -value : value;
}

/* ---- math builtins (compiler-rt/libm never enters the link) ---- */

double pow(double base, double exponent) { return __builtin_pow(base, exponent); }
double log2(double value) { return __builtin_log2(value); }
double log(double value) { return __builtin_log(value); }
double exp(double value) { return __builtin_exp(value); }
double sqrt(double value) { return __builtin_sqrt(value); }
double ceil(double value) { return __builtin_ceil(value); }
double floor(double value) { return __builtin_floor(value); }
double trunc(double value) { return __builtin_trunc(value); }
double acos(double value) { return __builtin_acos(value); }
double asin(double value) { return __builtin_asin(value); }
double atan(double value) { return __builtin_atan(value); }
double atan2(double y, double x) { return __builtin_atan2(y, x); }
double fmax(double left, double right) { return __builtin_fmax(left, right); }
double fmin(double left, double right) { return __builtin_fmin(left, right); }
double frexp(double value, int* exponent) { return __builtin_frexp(value, exponent); }
double ldexp(double value, int exponent) { return __builtin_ldexp(value, exponent); }
double scalbn(double value, int exponent) { return __builtin_scalbn(value, exponent); }
long lrint(double value) { return __builtin_lrint(value); }
double rint(double value) { return __builtin_rint(value); }
double nearbyint(double value) { return __builtin_nearbyint(value); }
double round(double value) { return __builtin_round(value); }
double fabs(double value) { return __builtin_fabs(value); }
double hypot(double x, double y) { return __builtin_hypot(x, y); }
double log10(double value) { return __builtin_log10(value); }
double cbrt(double value) { return __builtin_cbrt(value); }
double sinh(double value) { return __builtin_sinh(value); }
double cosh(double value) { return __builtin_cosh(value); }
double tanh(double value) { return __builtin_tanh(value); }
double exp2(double value) { return __builtin_exp2(value); }
double expm1(double value) { return __builtin_expm1(value); }
double log1p(double value) { return __builtin_log1p(value); }
double acosh(double value) { return __builtin_acosh(value); }
double asinh(double value) { return __builtin_asinh(value); }
double atanh(double value) { return __builtin_atanh(value); }
double sin(double value) { return __builtin_sin(value); }
double cos(double value) { return __builtin_cos(value); }
double tan(double value) { return __builtin_tan(value); }
double fmod(double numerator, double denominator) { return __builtin_fmod(numerator, denominator); }
int isinf(double value) { return __builtin_isinf(value); }
int isfinite(double value) { return __builtin_isfinite(value); }
int isnan(double value) { return __builtin_isnan(value); }
int signbit(double value) { return __builtin_signbit(value); }

int atoi(const char* text) { return (int)strtol(text, NULL, 10); }

long strtol(const char* text, char** end, int base) {
  const char* cursor = text;
  int negative = 0;
  long value = 0;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
  if (*cursor == '+' || *cursor == '-') {
    negative = *cursor == '-';
    ++cursor;
  }
  if (base == 0) {
    if (*cursor == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
      base = 16;
      cursor += 2;
    } else if (*cursor == '0') {
      base = 8;
    } else {
      base = 10;
    }
  }
  if (base == 16 && *cursor == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) cursor += 2;
  for (;;) {
    int digit;
    if (*cursor >= '0' && *cursor <= '9') digit = *cursor - '0';
    else if (base == 16 && *cursor >= 'a' && *cursor <= 'f') digit = *cursor - 'a' + 10;
    else if (base == 16 && *cursor >= 'A' && *cursor <= 'F') digit = *cursor - 'A' + 10;
    else break;
    if (digit >= base) break;
    value = value * base + digit;
    ++cursor;
  }
  if (end != NULL) *end = (char*)cursor;
  return negative ? -value : value;
}

unsigned long strtoul(const char* text, char** end, int base) { return (unsigned long)strtol(text, end, base); }
double atof(const char* text) { return strtod(text, NULL); }
char* getenv(const char* name) {
  (void)name;
  return NULL;
}
int rand(void) { return (int)(mobagen_quickjs_shim_monotonic_ns() >> 16); }
void srand(unsigned seed) { (void)seed; }

/* ---- assert / string ---- */

void __assert_fail(const char* assertion, const char* file, unsigned line, const char* function) {
  (void)assertion;
  (void)file;
  (void)line;
  (void)function;
  abort();
}

size_t strlen(const char* text) {
  size_t length = 0;
  while (text[length] != '\0') ++length;
  return length;
}

int strcmp(const char* left, const char* right) {
  while (*left != '\0' && *left == *right) {
    ++left;
    ++right;
  }
  return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int strncmp(const char* left, const char* right, size_t count) {
  while (count > 0 && *left != '\0' && *left == *right) {
    ++left;
    ++right;
    --count;
  }
  return count == 0 ? 0 : (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int memcmp(const void* left, const void* right, size_t count) {
  const unsigned char* a = (const unsigned char*)left;
  const unsigned char* b = (const unsigned char*)right;
  size_t index;
  for (index = 0; index < count; ++index) {
    if (a[index] != b[index]) return (int)a[index] - (int)b[index];
  }
  return 0;
}

void* memcpy(void* destination, const void* source, size_t count) {
  unsigned char* out = (unsigned char*)destination;
  const unsigned char* in = (const unsigned char*)source;
  size_t index;
  for (index = 0; index < count; ++index) out[index] = in[index];
  return destination;
}

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

void* memset(void* destination, int value, size_t count) {
  unsigned char* out = (unsigned char*)destination;
  size_t index;
  for (index = 0; index < count; ++index) out[index] = (unsigned char)value;
  return destination;
}

char* strcpy(char* destination, const char* source) {
  char* out = destination;
  while ((*out++ = *source++) != '\0') {
  }
  return destination;
}

char* strncpy(char* destination, const char* source, size_t count) {
  char* out = destination;
  while (count > 0 && (*out++ = *source++) != '\0') --count;
  while (count > 0) {
    *out++ = '\0';
    --count;
  }
  return destination;
}

char* strcat(char* destination, const char* source) {
  char* out = destination + strlen(destination);
  while ((*out++ = *source++) != '\0') {
  }
  return destination;
}

char* strchr(const char* text, int character) {
  for (;;) {
    if (*text == (char)character) return (char*)text;
    if (*text == '\0') return NULL;
    ++text;
  }
}

char* strrchr(const char* text, int character) {
  const char* last = NULL;
  for (;;) {
    if (*text == (char)character) last = text;
    if (*text == '\0') return (char*)last;
    ++text;
  }
}

char* strstr(const char* haystack, const char* needle) {
  const size_t needle_length = strlen(needle);
  if (needle_length == 0) return (char*)haystack;
  for (; *haystack != '\0'; ++haystack) {
    if (strncmp(haystack, needle, needle_length) == 0) return (char*)haystack;
  }
  return NULL;
}

char* strerror(int error) {
  (void)error;
  return (char*)"error";
}

size_t strspn(const char* text, const char* accept) {
  size_t length = 0;
  while (text[length] != '\0' && strchr(accept, text[length]) != NULL) ++length;
  return length;
}

size_t strcspn(const char* text, const char* reject) {
  size_t length = 0;
  while (text[length] != '\0' && strchr(reject, text[length]) == NULL) ++length;
  return length;
}

char* strtok(char* text, const char* delimiters) {
  static char* cursor;
  if (text != NULL) cursor = text;
  if (cursor == NULL) return NULL;
  while (*cursor != '\0' && strchr(delimiters, *cursor) != NULL) ++cursor;
  if (*cursor == '\0') {
    cursor = NULL;
    return NULL;
  }
  {
    char* token = cursor;
    while (*cursor != '\0' && strchr(delimiters, *cursor) == NULL) ++cursor;
    if (*cursor != '\0') *cursor++ = '\0';
    return token;
  }
}

void* memchr(const void* text, int character, size_t count) {
  const unsigned char* bytes = (const unsigned char*)text;
  size_t index;
  for (index = 0; index < count; ++index) {
    if (bytes[index] == (unsigned char)character) return (void*)&bytes[index];
  }
  return NULL;
}

void* memrchr(const void* text, int character, size_t count) {
  const unsigned char* bytes = (const unsigned char*)text;
  size_t index;
  for (index = count; index > 0; --index) {
    if (bytes[index - 1] == (unsigned char)character) return (void*)&bytes[index - 1];
  }
  return NULL;
}

char* strdup(const char* text) {
  const size_t length = strlen(text) + 1;
  char* copy = (char*)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

/* ---- time ---- */

uint64_t mobagen_quickjs_shim_monotonic_ns(void) {
  /* Deterministic seed source: quickjs only uses it for its xorshift RNG
   * (JS_NewContext) and Date.now before a host clock callback is installed.
   * A scripting sandbox does not need wall-clock entropy. */
  static uint32_t counter;
  counter += 0x9e3779b9u;
  return 0x100000001b3ull * counter;
}

int clock_gettime(int clock, struct timespec* out) {
  const uint64_t ticks = mobagen_quickjs_shim_monotonic_ns();
  (void)clock;
  if (out == NULL) return -1;
  out->tv_sec = (long)(ticks / 1000000000ull);
  out->tv_nsec = (long)(ticks % 1000000000ull);
  return 0;
}

int gettimeofday(struct timeval* out, void* unused_timezone) {
  const uint64_t ticks = mobagen_quickjs_shim_monotonic_ns();
  (void)unused_timezone;
  if (out == NULL) return -1;
  out->tv_sec = (long)(ticks / 1000000ull);
  out->tv_usec = (long)(ticks % 1000000ull);
  return 0;
}

/* Fixed UTC civil-calendar conversions (Howard Hinnant's algorithms). */
static long days_from_civil(long y, unsigned long m, unsigned long d) {
  y -= m <= 2;
  {
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned long yoe = (unsigned long)(y - era * 400);
    unsigned long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
  }
}

static void shim_epoch_days_to_civil(long days, int* year, int* month, int* day) {
  long z = days + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned long doe = (unsigned long)(z - era * 146097);
  unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned long mp = (5 * doy + 2) / 153;
  unsigned long d = doy - (153 * mp + 2) / 5 + 1;
  unsigned long m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) ++y;
  *year = (int)y;
  *month = (int)m;
  *day = (int)d;
}

static void shim_fill_tm(long seconds, struct tm* out) {
  long days = seconds / 86400;
  long remainder = seconds % 86400;
  int year;
  int month;
  int day;
  if (remainder < 0) {
    remainder += 86400;
    --days;
  }
  shim_epoch_days_to_civil(days, &year, &month, &day);
  out->tm_sec = (int)(remainder % 60);
  out->tm_min = (int)((remainder / 60) % 60);
  out->tm_hour = (int)(remainder / 3600);
  out->tm_mday = day;
  out->tm_mon = month - 1;
  out->tm_year = year - 1900;
  out->tm_wday = (int)(((days % 7) + 11) % 7);
  out->tm_yday = (int)(days - days_from_civil(year, 1, 1));
  out->tm_isdst = 0;
  out->tm_gmtoff = 0;
  out->tm_zone = "UTC";
}

struct tm* gmtime_r(const time_t* clock, struct tm* out) {
  if (clock == NULL || out == NULL) return NULL;
  shim_fill_tm((long)*clock, out);
  return out;
}

struct tm* localtime_r(const time_t* clock, struct tm* out) { return gmtime_r(clock, out); }

time_t mktime(struct tm* out) {
  long y = (long)out->tm_year + 1900;
  unsigned long m = (unsigned long)out->tm_mon + 1;
  unsigned long d = (unsigned long)out->tm_mday;
  y -= m <= 2;
  {
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned long yoe = (unsigned long)(y - era * 400);
    unsigned long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400 + out->tm_hour * 3600 + out->tm_min * 60 + out->tm_sec);
  }
}

time_t time(time_t* out) {
  const time_t seconds = (time_t)(mobagen_quickjs_shim_monotonic_ns() / 1000000000ull);
  if (out != NULL) *out = seconds;
  return seconds;
}

clock_t clock(void) { return (clock_t)(mobagen_quickjs_shim_monotonic_ns() / 1000ull); }

/* ---- printf subset ---- */

static void shim_emit_char(char character, char* buffer, size_t capacity, size_t* written) {
  if (buffer != NULL && *written + 1 < capacity) buffer[*written] = character;
  ++*written;
}

static void shim_emit_string(const char* text, char* buffer, size_t capacity, size_t* written) {
  if (text == NULL) text = "(null)";
  while (*text != '\0') {
    shim_emit_char(*text, buffer, capacity, written);
    ++text;
  }
}

static void shim_emit_unsigned(unsigned long value, unsigned base, int uppercase, char* buffer, size_t capacity, size_t* written) {
  char digits[24];
  int count = 0;
  const char* alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
  if (value == 0) digits[count++] = '0';
  while (value != 0) {
    digits[count++] = alphabet[value % base];
    value /= base;
  }
  while (count > 0) shim_emit_char(digits[--count], buffer, capacity, written);
}

static void shim_emit_signed(long value, char* buffer, size_t capacity, size_t* written) {
  unsigned long magnitude;
  if (value < 0) {
    shim_emit_char('-', buffer, capacity, written);
    magnitude = (unsigned long)(-(value + 1)) + 1u;
  } else {
    magnitude = (unsigned long)value;
  }
  shim_emit_unsigned(magnitude, 10, 0, buffer, capacity, written);
}

/* Minimal %f: fixed notation, default precision 6 (capped 15). QuickJS
 * formats JS numbers through its own dtoa + %s/%d paths. */
static void shim_emit_double(double value, int precision, char* buffer, size_t capacity, size_t* written) {
  if (isnan(value)) {
    shim_emit_string("nan", buffer, capacity, written);
    return;
  }
  if (isfinite(value) == 0) {
    shim_emit_string(signbit(value) ? "-inf" : "inf", buffer, capacity, written);
    return;
  }
  if (signbit(value)) {
    shim_emit_char('-', buffer, capacity, written);
    value = -value;
  }
  if (precision < 0) precision = 6;
  if (precision > 15) precision = 15;
  {
    double scale = shim_pow10(precision);
    uint64_t fraction = (uint64_t)(value * scale + 0.5);
    uint64_t whole = fraction / (uint64_t)scale;
    uint64_t decimals = fraction % (uint64_t)scale;
    shim_emit_unsigned((unsigned long)whole, 10, 0, buffer, capacity, written);
    if (precision > 0) {
      char digit_buffer[24];
      int count = 0;
      shim_emit_char('.', buffer, capacity, written);
      while (count < precision) {
        digit_buffer[count++] = (char)('0' + (int)(decimals % 10u));
        decimals /= 10u;
      }
      while (count > 0) shim_emit_char(digit_buffer[--count], buffer, capacity, written);
    }
  }
}

static size_t shim_vsnprintf(char* buffer, size_t capacity, const char* format, va_list arguments) {
  size_t written = 0;
  if (buffer != NULL && capacity > 0) buffer[0] = '\0';
  while (*format != '\0') {
    if (*format != '%') {
      shim_emit_char(*format, buffer, capacity, &written);
      ++format;
      continue;
    }
    ++format;
    {
      int long_modifier = 0;
      int precision = -1;
      while (*format == 'l') {
        ++long_modifier;
        ++format;
      }
      if (*format == '.') {
        ++format;
        precision = 0;
        while (*format >= '0' && *format <= '9') {
          precision = precision * 10 + (*format - '0');
          ++format;
        }
      }
      switch (*format) {
        case 'd':
        case 'i':
          if (long_modifier >= 2)
            shim_emit_signed((long)va_arg(arguments, long long), buffer, capacity, &written);
          else if (long_modifier == 1)
            shim_emit_signed((long)va_arg(arguments, long), buffer, capacity, &written);
          else
            shim_emit_signed((long)va_arg(arguments, int), buffer, capacity, &written);
          break;
        case 'u':
          shim_emit_unsigned(long_modifier ? va_arg(arguments, unsigned long) : (unsigned long)va_arg(arguments, unsigned int), 10, 0, buffer,
                             capacity, &written);
          break;
        case 'x':
          shim_emit_unsigned(long_modifier ? va_arg(arguments, unsigned long) : (unsigned long)va_arg(arguments, unsigned int), 16, 0, buffer,
                             capacity, &written);
          break;
        case 'X':
          shim_emit_unsigned(long_modifier ? va_arg(arguments, unsigned long) : (unsigned long)va_arg(arguments, unsigned int), 16, 1, buffer,
                             capacity, &written);
          break;
        case 'p':
          shim_emit_string("0x", buffer, capacity, &written);
          shim_emit_unsigned((unsigned long)(uintptr_t)va_arg(arguments, void*), 16, 0, buffer, capacity, &written);
          break;
        case 's':
          shim_emit_string(va_arg(arguments, const char*), buffer, capacity, &written);
          break;
        case 'c':
          shim_emit_char((char)va_arg(arguments, int), buffer, capacity, &written);
          break;
        case 'f':
          shim_emit_double(va_arg(arguments, double), precision, buffer, capacity, &written);
          break;
        case '%':
          shim_emit_char('%', buffer, capacity, &written);
          break;
        case '\0':
          continue;
        default:
          shim_emit_char('%', buffer, capacity, &written);
          shim_emit_char(*format, buffer, capacity, &written);
          break;
      }
      if (*format != '\0') ++format;
    }
  }
  if (buffer != NULL && capacity > 0) buffer[written < capacity ? written : capacity - 1] = '\0';
  return written;
}

#if defined(__EMSCRIPTEN__)
/* musl owns the printf family on this path; its stdout path routes through
   standalone.c's imported__wasi_fd_write. Define that FORWARDER's target
   instead: wasi_writeln writes through `imported__wasi_fd_write` which is
   declared with the wasi import attribute — we cannot remove it, but we can
   make nothing call it by providing emscripten_out/err weak overrides.
   The only remaining caller of fd_write for a scripting guest is
   JS_DumpMemoryUsage (JS_DUMP_MEM debug) and abort messages. Provide the
   weak overrides so musl stdio for OUR units never engages. */
void emscripten_out(const char* text) { (void)text; }
void emscripten_err(const char* text) { (void)text; }
#else
int vsnprintf(char* buffer, size_t size, const char* format, va_list arguments) {
  return (int)shim_vsnprintf(buffer, size, format, arguments);
}

int snprintf(char* buffer, size_t size, const char* format, ...) {
  va_list arguments;
  int result;
  va_start(arguments, format);
  result = (int)shim_vsnprintf(buffer, size, format, arguments);
  va_end(arguments);
  return result;
}

/* The guest has no stdio streams; quickjs reaches these only on debug dump
 * paths (JS_DUMP_LEAKS & friends). Drop the bytes silently. */
int vfprintf(FILE* stream, const char* format, va_list arguments) {
  (void)stream;
  (void)format;
  (void)arguments;
  return 0;
}

int fprintf(FILE* stream, const char* format, ...) {
  (void)stream;
  (void)format;
  return 0;
}

int printf(const char* format, ...) {
  (void)format;
  return 0;
}

int putchar(int character) {
  (void)character;
  return (int)(unsigned char)character;
}

int puts(const char* text) {
  (void)text;
  return 0;
}

int fputs(const char* text, FILE* stream) {
  (void)text;
  (void)stream;
  return 0;
}

int fputc(int character, FILE* stream) {
  (void)character;
  (void)stream;
  return character;
}

size_t fwrite(const void* data, size_t size, size_t count, FILE* stream) {
  (void)data;
  (void)stream;
  return count == 0 ? 0 : size * count;
}
#endif /* __EMSCRIPTEN__ split */
