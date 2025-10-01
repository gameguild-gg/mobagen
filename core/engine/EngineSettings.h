#ifndef MOBAGEN_ENGINESETTINGS_H
#define MOBAGEN_ENGINESETTINGS_H

struct EngineSettings {
  bool debug : 1 = true;
  bool fullscreen : 1 = false;
  bool vsync : 1 = true;
  bool showWindow : 1 = true;
  bool headless : 1 = false;
};

#endif  // MOBAGEN_ENGINESETTINGS_H
