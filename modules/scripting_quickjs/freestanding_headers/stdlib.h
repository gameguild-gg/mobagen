/* Freestanding shim <stdlib.h> — supplied via -isystem for the QuickJS guest. */
#ifndef _SHIM_STDLIB_H_
#define _SHIM_STDLIB_H_
#include <stddef.h>
void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* pointer, size_t size);
void free(void* pointer);
void abort(void);
double strtod(const char* text, char** end);
double atof(const char* text);
int abs(int value);
long labs(long value);
long long llabs(long long value);
void* alloca(size_t size);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
int atoi(const char* text);
long strtol(const char* text, char** end, int base);
unsigned long strtoul(const char* text, char** end, int base);
char* getenv(const char* name);
int rand(void);
void srand(unsigned seed);
#endif
