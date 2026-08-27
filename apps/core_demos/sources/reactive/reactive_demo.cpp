// Reactive demo: the editor's window/level -> transfer-function pipeline.
// A Computed derives from Signals; an Effect "uploads" whenever the derived value
// changes — all wired automatically by reading, with change-detection skipping
// redundant work.

#include "reactive.hpp"

#include <cstdio>

using namespace reactive;

int main() {
  Signal<float> center{40};
  Signal<float> width{400};

  // Stand-in for buildLUT(center, width): a Computed derived from both signals.
  Computed<float> lut_key([&] { return center.get() * 1000.f + width.get(); });

  int uploads = 0;
  // "Upload the LUT to the GPU" whenever the derived key changes. Reading
  // lut_key here subscribes this effect to it — no manual wiring.
  Effect upload([&] {
    std::printf("  [effect] LUT key=%.0f -> upload to GPU\n", lut_key.get());
    ++uploads;
  });  // runs once immediately

  std::printf("center.set(20):\n");
  center.set(20);
  std::printf("width.set(800):\n");
  width.set(800);
  std::printf("width.set(800) again:\n");
  width.set(800);  // no change -> nothing
  std::printf("center.set(20) again:\n");
  center.set(20);  // no change -> nothing

  std::printf("total uploads = %d  (expect 3: 1 initial + 2 real changes)  [%s]\n", uploads, uploads == 3 ? "OK" : "FAIL");
  return 0;
}
