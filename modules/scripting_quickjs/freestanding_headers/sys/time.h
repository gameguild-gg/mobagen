/* Freestanding shim <sys/time.h> — supplied via -isystem for the QuickJS
   guest builds. Reuses the (guard-protected) sysroot struct timeval. */
#ifndef _SHIM_SYS_TIME_H_
#define _SHIM_SYS_TIME_H_
#include <__struct_timeval.h>
int gettimeofday(struct timeval* out, void* unused_timezone);
#endif
