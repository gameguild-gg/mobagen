#pragma once

#include "SDL_video.h"

class Window {
    Window(char *title);

    ~Window(void);

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

private:
    SDL_Window *m_window;
    SDL_GLContext m_glContext;
//    std::unique_ptr<GuiManager> m_guiManager;

//    int m_width, m_height;

//    Input m_input;

//    bool m_quit;
//    bool m_fullscreen;
};

