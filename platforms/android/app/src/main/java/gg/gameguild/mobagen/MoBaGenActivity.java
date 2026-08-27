package gg.gameguild.mobagen;

import org.libsdl.app.SDLActivity;

/**
 * MoBaGenActivity — thin wrapper around SDL3's SDLActivity.
 *
 * SDL's SDLActivity.onCreate() calls System.loadLibrary("main"), which maps to
 * libmain.so placed in jniLibs/ by build.py.  All SDL event handling, window
 * creation, and OpenGL ES / WebGPU surface management happens inside SDL.
 *
 * To extend behaviour (e.g., request Android permissions before SDL starts),
 * override the relevant SDL hook methods here.
 */
public class MoBaGenActivity extends SDLActivity {
  // No additional overrides required for a basic SDL3 app.
  // SDL_main() in your C++ code is the entry point.
}
