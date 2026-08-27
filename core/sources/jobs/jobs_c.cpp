// C ABI facade implementation — thin wrapper translating opaque handles +
// function-pointer callbacks to the C++ Scheduler. This is the only place the
// renderer/editor/other languages need to link against for the job system.

#include "jobs_c.h"

#include "scheduler.hpp"

using jobs::Scheduler;
using jobs::WaitGroup;

extern "C" {

JobSystem* jobs_create(unsigned workers) { return reinterpret_cast<JobSystem*>(new Scheduler(workers)); }

void jobs_destroy(JobSystem* js) { delete reinterpret_cast<Scheduler*>(js); }

unsigned jobs_worker_count(const JobSystem* js) { return reinterpret_cast<const Scheduler*>(js)->worker_count(); }

void jobs_parallel_for(JobSystem* js, size_t n, size_t grain, JobRangeFn fn, void* user) {
  auto* s = reinterpret_cast<Scheduler*>(js);
  WaitGroup wg;
  s->parallel_for(
      n, grain, [fn, user](size_t b, size_t e) { fn(b, e, user); }, wg);
  s->wait(wg);  // block until this batch completes
}

}  // extern "C"
