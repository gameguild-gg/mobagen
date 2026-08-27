#ifndef JOBS_C_H
#define JOBS_C_H
// ============================================================================
// jobs_c.h — C ABI facade for the job system (the module's cross-language CONTRACT).
// ============================================================================
// Opaque handle + a function-pointer callback (a C ABI can't take a C++ lambda).
// Coarse by design: create a system, run a parallel_for, destroy. Hot per-element
// work stays inside `fn` as plain C/C++ — we never cross this ABI per item.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JobSystem JobSystem;  // opaque

// Called once per chunk with a half-open range [begin, end). `user` is opaque.
typedef void (*JobRangeFn)(size_t begin, size_t end, void* user);

JobSystem* jobs_create(unsigned workers);  // 0 => hardware_concurrency
void jobs_destroy(JobSystem* js);
unsigned jobs_worker_count(const JobSystem* js);

// Blocking data-parallel loop: split [0,n) into `grain`-sized chunks, run them on
// the pool, return when all complete. Call from the driver (non-worker) thread.
void jobs_parallel_for(JobSystem* js, size_t n, size_t grain, JobRangeFn fn, void* user);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // JOBS_C_H
