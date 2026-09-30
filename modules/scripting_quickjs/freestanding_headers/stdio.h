/* Freestanding shim <stdio.h> — supplied via -isystem for the QuickJS
   guest. Streams are opaque drop-sinks: nothing is ever written. */
#ifndef _SHIM_STDIO_H_
#define _SHIM_STDIO_H_
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
typedef struct _mobagen_shim_file MobagenFILEShim;
#define FILE MobagenFILEShim
#define stdout ((MobagenFILEShim*)0)
#define stderr ((MobagenFILEShim*)1)
#define stdin ((MobagenFILEShim*)2)
int snprintf(char* buffer, size_t size, const char* format, ...);
int vsnprintf(char* buffer, size_t size, const char* format, va_list arguments);
int vfprintf(MobagenFILEShim* stream, const char* format, va_list arguments);
int fprintf(MobagenFILEShim* stream, const char* format, ...);
int printf(const char* format, ...);
int putchar(int character);
int puts(const char* text);
int fputs(const char* text, MobagenFILEShim* stream);
int fputc(int character, MobagenFILEShim* stream);
size_t fwrite(const void* data, size_t size, size_t count, MobagenFILEShim* stream);
#endif
