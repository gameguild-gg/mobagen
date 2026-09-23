/* Freestanding shim <time.h> — supplied via -isystem for the QuickJS
   guest builds. On wasi the sysroot's own struct/timespec leak in through
   stdint.h->alltypes.h before any <time.h> include, so this header REUSES
   the sysroot struct definitions (idempotent guards) instead of
   redefining them; only the clock entry points are declared here. */
#ifndef _SHIM_TIME_H_
#define _SHIM_TIME_H_
#include <__struct_timespec.h>
#include <__struct_tm.h>
#include <__typedef_clockid_t.h>
#include <__typedef_time_t.h>
#include <__typedef_clock_t.h>
/* quickjs reads tm_gmtoff/tm_zone; the wasi struct names them __tm_* and
   only maps the friendly names under _GNU_SOURCE (a compile flag here). */
#ifdef _GNU_SOURCE
#undef __tm_gmtoff
#undef __tm_zone
#define tm_gmtoff __tm_gmtoff
#define tm_zone __tm_zone
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 3
#endif
int clock_gettime(int clock, struct timespec* out);
struct tm* gmtime_r(const time_t* clock, struct tm* out);
struct tm* localtime_r(const time_t* clock, struct tm* out);
time_t mktime(struct tm* out);
time_t time(time_t* out);
#define CLOCKS_PER_SEC 1000000
clock_t clock(void);
#endif
