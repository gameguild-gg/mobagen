#ifndef MOBAGEN_ENGINESETTINGS_H
#define MOBAGEN_ENGINESETTINGS_H

// Top-level switches for which UI / rendering backends the engine drives
// per frame. These can be combined freely except for the constraint below.
//
// Constraint: `core/Renderer2D` is backed by an `ImDrawList`, so any
// GameObject whose `OnDraw(Renderer2D&)` produces visible output requires
// `useImGui = true`. Setting `useImGui = false, useRmlUi = true` is
// intended for RmlUi-only demos that have no GameObjects; the engine will
// log a warning at startup if it sees any registered GameObject in that
// configuration. The same constraint applies to `OnGui`, which renders
// into ImGui directly.
struct EngineSettings {
  bool debug : 1 = true;
  bool fullscreen : 1 = false;
  bool vsync : 1 = true;
  bool showWindow : 1 = true;
  bool headless : 1 = false;

  // Drive the ImGui UI/render pipeline. Also required for GameObject
  // OnGui() / OnDraw(Renderer2D&) callbacks (see constraint above).
  bool useImGui : 1 = true;

  // Drive the RmlUi HTML/CSS pipeline (custom WebGPU backend). Independent
  // of ImGui — the two can run side-by-side. See the constraint above.
  bool useRmlUi : 1 = false;
};

#endif  // MOBAGEN_ENGINESETTINGS_H
