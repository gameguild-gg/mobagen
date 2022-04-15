#pragma once

#include <string>
#include "SDL_video.h"
#include "SDL_render.h"
#include "Vector2.h"

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

class Window {

    //    void init(void);

//    void tick(void);

//    void swapBuffer(void);

//    int poll_event(void);

//    int getWidth(void);

//    int getHeight(void);

    // getViewport(void) const;

    // getDisplaySize(void) const;

    // getDrawableSize(void) const;

    // static const char *getClipboardText(void* user_data);

    // static void setClipboardText(void* user_data, const char* text);

    // void makeCurrentContext(void) const;

    //Input *getInput(void);

    SDL_Window *getSDLWindow(void);

//    bool shouldQuit(void) const;

//    void drawCursor(bool enabled);

//    void setFullscreen(uint32_t flag);

//    void toggleFullscreen(void);

public:
    explicit Window(std::string title);

    SDL_Window *sdlWindow;
    SDL_Renderer *sdlRenderer;

    Vector2 size() const;

    ~Window();

private:
    SDL_GLContext m_glContext;
//    std::unique_ptr<GuiManager> m_guiManager;

//    int m_width, m_height;

//    Input m_input;

//    bool m_quit;
//    bool m_fullscreen;
};

