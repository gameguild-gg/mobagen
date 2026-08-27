// ============================================================================
// Android SDL_main wrapper
//
// SDL3's Android backend looks up the symbol `SDL_main` in the loaded .so
// via dlsym and calls it as the program entry point. Examples define
// `int main(int, char**)` with `#define SDL_MAIN_HANDLED true` so that
// SDL3 doesn't generate a main-rewriting SDL_main macro on desktop.
//
// But Android is different: SDL3's lookup happens regardless of
// SDL_MAIN_HANDLED, so the .so MUST export an `SDL_main` symbol. This
// shim forwards SDL_main to the example's main() and is compiled
// unconditionally into every Android target via the
// ANDROID_MAIN_WRAPPER_SRC variable set up in the top-level CMakeLists.
// ============================================================================

#include <cstdint>

extern int main(int argc, char* argv[]);

extern "C" int32_t SDL_main(int32_t argc, char* argv[]) { return main(argc, argv); }
