/* Freestanding shim <assert.h> */
#ifndef _SHIM_ASSERT_H_
#define _SHIM_ASSERT_H_
void __assert_fail(const char* assertion, const char* file, unsigned line, const char* function);
#define assert(expression) ((void)((expression) ? (void)0 : __assert_fail(#expression, __FILE__, __LINE__, __func__)))
#endif
