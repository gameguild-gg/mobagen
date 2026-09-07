// ============================================================================
// Android SDL_main anchor — SDL3 callback-model edition.
//
// On Android the real entry point is Java: SDLActivity loads libmain.so and
// calls nativeRunMain("SDL_main"), which dlsym's SDL_main inside the loaded
// .so and runs it through SDL_RunApp (SDL3 3.4.0: android-project/.../
// SDLActivity.java:260,284; src/core/android/SDL_android.c:838-841).
//
// SDL_main.h marks Android SDL_MAIN_NEEDED + SDL_MAIN_EXPORTED (include/SDL3/
// SDL_main.h:179-194): no native main() is synthesized on Android, and under
// SDL_MAIN_USE_CALLBACKS the impl header itself DEFINES the exported SDL_main
// that drives SDL_EnterAppMainCallbacks — "a standard SDL_main, which the app
// SHOULD NOT ALSO SUPPLY" (include/SDL3/SDL_main_impl.h:46-60; the generic
// main() at impl:135 is compiled out by the !SDL_MAIN_EXPORTED guard at
// impl:69). That definition lives in core/sources/app/sdl_app.cpp, the single
// TU including the SDL3 main header, so hosted apps (MOBAGEN_MAIN) define
// neither SDL_main nor main() — this shim no longer forwards anything.
//
// Remaining job: sdl_app.o sits inside the libmobagen_core_app.a static
// archive. Desktop executables pull it implicitly (crt's undefined `main`
// resolves to the one SDL_main_impl.h synthesizes there), but an Android
// SHARED library has no undefined entry symbol of its own — unaided, the
// linker leaves sdl_app.o dormant and libmain.so exports no SDL_main
// (build stays green; dlsym then fails at runtime, SDL_android.c:896).
// Referencing SDL_main below creates the undefined reference that drags
// sdl_app.o — with its default-visibility SDL_main (SDL_main.h:253-256 +
// SDL_begin_code.h:375) — into every <target>_android_so link.
//
// Legacy note: this flow no longer supports apps defining their own main()
// and expecting a SDL_main -> main() forward. Every windowed app is
// MOBAGEN_MAIN-hosted now; the only dir==target app still defining main()
// is dawn_probe, a non-SDL console probe that cannot produce a working
// SDLActivity APK through this packaging path anyway.
// ============================================================================

extern "C" int SDL_main(int argc, char* argv[]);

// The pointer is never read; its relocation against SDL_main IS the anchor.
// `used` stops the compiler from eliding the unused internal-linkage object.
__attribute__((used))
static int (*const sdl_main_anchor)(int, char*[]) = &SDL_main;
