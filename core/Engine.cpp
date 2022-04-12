#include "Engine.h"
#include "SDL.h"

#ifdef EMSCRIPTEN
static Engine *instance = nullptr;
void Engine::loop(void){
  instance->Tick();
}
#endif

Engine::Engine() {
#ifdef EMSCRIPTEN
    instance = nullptr;
#endif
    window = nullptr;
}

Engine::~Engine() {
    if(window) {
        // Cleanup
        ImGui_ImplSDLRenderer_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        delete (window);
    }
}

void Engine::Run() {
    // Main loop
#ifndef EMSCRIPTEN
    while (!done) { Tick(); }
    Exit();
#endif
}

int Engine::Start(std::string title) {
    SDL_Log("Initializing Window");
    window = new Window(std::move(title));
    if(window != nullptr)
        SDL_Log("Window Initialized");
    else
        exit(0);

#ifdef EMSCRIPTEN
    SDL_Log("Setting main loop for emscripten");
    instance = this;
    emscripten_set_main_loop(Engine::loop, 0, 1); // should be called only after sldrenderinit
    SDL_Log("Main loop set");
#endif

    return true;
}

void Engine::Tick() {
    // inputs processing
    processInput();

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // update
    auto deltaTime = ImGui::GetIO().DeltaTime;
    for(auto go : gameObjects)
        go->Update(deltaTime);

    // iterate over all game objects ui
    ImGuiContext *context = ImGui::GetCurrentContext();
    for(auto go : gameObjects)
        go->OnGui(context); // todo: find a better way to pass imgui context

    // Rendering
    ImGui::Render();
    SDL_SetRenderDrawColor(window->sdlRenderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
    SDL_RenderClear(window->sdlRenderer);

    // Draw
    for(auto go : gameObjects)
        go->OnDraw(window->sdlRenderer);

    ImGui_ImplSDLRenderer_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(window->sdlRenderer);

    SDL_Delay(0);
}

void Engine::Exit() {
    SDL_Log("Exit called");
    done = true;
    for(auto go : gameObjects)
        delete(go); // clear all remaining game objects
    gameObjects.clear();
    SDL_Log("Game Objects Cleared");
}

void Engine::processInput() {
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
        arrowInput = Vector2();

        switch(event.type){
            case SDL_KEYDOWN:
                switch( event.key.keysym.sym ){
                    case SDLK_LEFT:
                        arrowInput += Vector2::left();
                        break;
                    case SDLK_RIGHT:
                        arrowInput += Vector2::right();
                        break;
                    case SDLK_UP:
                        arrowInput += Vector2::up();
                        break;
                    case SDLK_DOWN:
                        arrowInput += Vector2::down();
                        break;
                    default:
                        break;
                }
                break;
        }

        arrowInput = arrowInput.normalized();
    }
}

Vector2 Engine::getInputArrow() const {
    return arrowInput;
}
