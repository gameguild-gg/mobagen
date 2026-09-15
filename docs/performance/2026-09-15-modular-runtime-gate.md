# Modular runtime performance gate

The `MobagenPerformanceGate` target is the blocking Release benchmark for the
native module boundary. It builds a lean host with the runtime, built-in engine
subsystems, bundled modules, HTTP backend, and command-line tools disabled.

It reports these operations as JSON:

- `startup.module_manager`: indexes an already-resolved activation plan and
  verifies that the host still has zero active providers and capabilities.
- `acquire.first`: performs the first lazy DLL load, ABI query, configuration,
  lifecycle start, and capability lookup for a fresh manager.
- `dispatch.direct_2m` and `dispatch.cached_2m`: compare two million calls
  through identical C function-table seams. The modular side uses the pointer
  cached from the manager's first acquire; there is no map lookup per call.
- `frame.direct` and `frame.modular`: compare 240 representative CPU frames.
  Both sides execute the same deterministic 8,192-item workload.

Dispatch and frame samples are measured as short A/B blocks with alternating
order. Each outer sample is the median block cost scaled to the complete work
count, which rejects scheduler interruptions without hiding persistent cost.
The gate calculates paired overhead for every outer sample and fails with exit
code 3 when the fifth percentile (`p05`) is above 1%. In other words, a change
is blocked only when at least 95% of paired observations breach the budget, not
because one side happened to be preempted. The median remains visible for
diagnosis.

After warm-up, the benchmark enables an allocation probe around a complete
modular dispatch and frame sample. Any C++ allocation fails the run with exit
code 4. Startup and first-acquire timings are reported but do not share the 1%
hot-path budget because loading and activation intentionally occur only once.

Run the same gate used by CI:

```sh
cmake --build build-performance --config Release --target MobagenPerformanceGate
```
