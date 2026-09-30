/* Freestanding shim <string.h> */
#ifndef _SHIM_STRING_H_
#define _SHIM_STRING_H_
#include <stddef.h>
size_t strlen(const char* text);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t count);
int memcmp(const void* left, const void* right, size_t count);
int memcmp_const(const void* left, const void* right, size_t count);
void* memcpy(void* destination, const void* source, size_t count);
void* memmove(void* destination, const void* source, size_t count);
void* memset(void* destination, int value, size_t count);
char* strcpy(char* destination, const char* source);
char* strncpy(char* destination, const char* source, size_t count);
char* strcat(char* destination, const char* source);
char* strchr(const char* text, int character);
char* strrchr(const char* text, int character);
char* strstr(const char* haystack, const char* needle);
char* strerror(int error);
int strcoll(const char* left, const char* right);
size_t strspn(const char* text, const char* accept);
size_t strcspn(const char* text, const char* reject);
char* strtok(char* text, const char* delimiters);
char* strdup(const char* text);
void* memchr(const void* text, int character, size_t count);
void* memrchr(const void* text, int character, size_t count);
#endif
