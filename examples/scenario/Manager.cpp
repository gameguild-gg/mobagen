#include "Manager.h"
#include "generators/RandomGenerator.h"
#include <chrono>
#include <iostream>
Manager::Manager(Engine* engine, int size)
    : GameObject(engine) {
  // todo: add your generator here
  generators.push_back(new RandomScenarioGenerator());
}

void Manager::SetPixels(std::vector<Color32> &input) {
  Uint32* output = nullptr;
  int pitch = 0;
  uint32_t format;

  // Get the size of the texture.
  int w, h;
  SDL_QueryTexture(texture, &format, nullptr, &w, &h);

  // Now let's make our "pixels" pointer point to the texture data.
  if (SDL_LockTexture(texture, nullptr, (void**)&output, &pitch))
  {
    auto error = SDL_GetError();
    SDL_Log(error);
    return;
    // If the locking fails, you might want to handle it somehow. SDL_GetError(); or something here.
  }

  SDL_PixelFormat pixelFormat;
  pixelFormat.format = format;
  for(uint64_t line=0; line<h; line++){
    for(uint64_t column=0; column<h; column++) {
      auto lc = line*column;
      // Now you want to format the color to a correct format that SDL can use.
      // Basically we convert our RGB color to a hex-like BGR color.

      auto color = input[lc].GetPacked();
      // Before setting the color, we need to know where we have to place it.
      Uint32 pixelPosition = line * (pitch / sizeof(unsigned int)) + column;
      // Now we can set the pixel(s) we want.
      output[pixelPosition] = color;
    }
  }
  // Also don't forget to unlock your texture once you're done.
  SDL_UnlockTexture(texture);
}
void Manager::OnDraw(SDL_Renderer* renderer) {
  auto windowSize = engine->window->size();
  const SDL_Rect r = {0,0, sideSize,sideSize};

  SDL_RenderCopy(renderer, texture, NULL, &r);
}
Manager::~Manager() {}
void Manager::Start() {
  texture = SDL_CreateTexture(engine->window->sdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 512, 512);
  std::vector<Color32> colors;
  colors.resize(sideSize*sideSize);
  for(int i=0;i<sideSize*sideSize;i++)
    colors[i]=Color::Cyan;
  SetPixels(colors);
}
void Manager::OnGui(ImGuiContext* context) {
  ImGui::SetCurrentContext(context);
  float deltaTime = ImGui::GetIO().DeltaTime;

  ImGui::SetCurrentContext(context);
  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS",
              ImGui::GetIO().DeltaTime * 1000,
              1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate,
              ImGui::GetIO().Framerate);
  static auto newSize = sideSize;

  if(ImGui::SliderInt("Side Size", &newSize, 5, 29)) {
    newSize = (newSize/4)*4 + 1;
    if(newSize!=sideSize) {
      sideSize = newSize;
      Clear();
    }
  }

  ImGui::Text("Generator: %s", generators[generatorId]->GetName().c_str());
  if (ImGui::BeginCombo("##combo", generators[generatorId]->GetName().c_str())) // The second parameter is the label previewed before opening the combo.
  {
    for (int n = 0; n < generators.size(); n++) {
      bool is_selected =
          (generators[generatorId]->GetName() ==
           generators[n]
               ->GetName());  // You can store your selection however you want, outside or inside your objects
      if (ImGui::Selectable(generators[n]->GetName().c_str(), is_selected)) {
        generatorId = n;
        Clear();
      }
      if (is_selected)
        ImGui::SetItemDefaultFocus();  // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
    }
    ImGui::EndCombo();
  }

  if(ImGui::Button("Generate")) {
    auto start = std::chrono::high_resolution_clock::now();
    auto pixels = generators[generatorId]->Generate(sideSize);
    auto step = std::chrono::high_resolution_clock::now();
    SetPixels(pixels);
    auto end = std::chrono::high_resolution_clock::now();
    std::cout <<  std::chrono::duration_cast<std::chrono::microseconds>(step - start).count() << " " << std::chrono::duration_cast<std::chrono::microseconds>(end - step).count() << std::endl;
  }
}
void Manager::Update(float deltaTime) {
  GameObject::Update(deltaTime);
}
void Manager::Clear() {

}
int Manager::GetSize() const {
  return sideSize;
}
