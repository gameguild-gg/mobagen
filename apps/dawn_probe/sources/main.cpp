// Dawn risk-gate: proves WebGPU-via-Dawn fetches, builds, and links on this box,
// in isolation from the renderer. If this builds + runs, full master alignment is
// feasible; if Dawn won't build here, we learn it cheaply before rewiring anything.
#include <webgpu/webgpu_cpp.h>

#include <cstdio>

int main() {
  wgpu::Instance instance = wgpu::CreateInstance();
  std::printf("Dawn WebGPU instance created: %s\n", instance ? "OK" : "NULL");
  return instance ? 0 : 1;
}
