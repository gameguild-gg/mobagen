#include "Engine.h"

Engine *Engine::instance = nullptr;

#ifdef EMSCRIPTEN
void Engine::loop() {
    {
        instance->Tick();
    }
}
#endif

Engine::Engine() {}

Engine::~Engine() {}

void Engine::Run() {
    // Main loop
#ifndef EMSCRIPTEN
    while (!done) { Tick(); }
    Exit();
#endif
}

int Engine::Start() {
    window = new Window("Placeholder");

#ifdef EMSCRIPTEN
    instance = this;
    emscripten_set_main_loop(Engine::loop, 0, 1); // should be called only after sldrenderinit
#endif
    return true;
}

void Engine::Tick() {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
    // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            done = true;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window->sdlWindow))
            done = true;
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // TODO: move this to the EDITOR location
//    ImGui::ShowDemoWindow(&show_demo_window);
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("New Scene");
            ImGui::MenuItem("Open Scene");
            ImGui::MenuItem("Open Recent");
            ImGui::Separator();
            ImGui::MenuItem("Save");
            ImGui::MenuItem("Save As...");
            ImGui::MenuItem("Save As Scene Template...");
            ImGui::Separator();
            ImGui::MenuItem("New Project...");
            ImGui::MenuItem("Open Project...");
            ImGui::MenuItem("Save Project");
            ImGui::Separator();
            ImGui::MenuItem("Build Settings");
            ImGui::MenuItem("Build and Run");
            ImGui::Separator();
            if(ImGui::MenuItem("Exit")) done = true;

            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Edit")) {
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Assets")) {
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("GameObject")) {
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Component")) {
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Window")) {
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Help")) {
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::Text("%.3fms (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::EndMenuBar();
    }



    // Rendering
    ImGui::Render();
    SDL_SetRenderDrawColor(window->sdlRenderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
    SDL_RenderClear(window->sdlRenderer);
    ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(window->sdlRenderer);

    SDL_Delay(0);
}

void Engine::Exit() {
    // Cleanup
    ImGui_ImplSDLRenderer_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    delete(window);
}
