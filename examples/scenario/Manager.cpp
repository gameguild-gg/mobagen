#include "Manager.h"
Manager::Manager(Engine* engine, int size)
    : GameObject(engine) {

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
    throw error;
    // If the locking fails, you might want to handle it somehow. SDL_GetError(); or something here.
  }

  SDL_PixelFormat pixelFormat;
  pixelFormat.format = format;
  for(uint64_t line=0; line<h; line++){
    for(uint64_t column=0; column<h; column++) {
      auto lc = line*column;
      // Now you want to format the color to a correct format that SDL can use.
      // Basically we convert our RGB color to a hex-like BGR color.
      auto color = SDL_MapRGB(&pixelFormat, input[lc].r,
                              input[lc].g, input[lc].b);
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
  const SDL_Rect r = {0,0, 512,512};

  SDL_RenderCopy(renderer, texture, NULL, &r);
}
Manager::~Manager() {}
void Manager::Start() {
  texture = SDL_CreateTexture(engine->window->sdlRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 512, 512);
  std::vector<Color32> colors;
  colors.resize(512*512);
  for(int i=0;i<512*512;i++)
    colors[i]=Color::Green;
  SetPixels(colors);
}
void Manager::OnGui(ImGuiContext* context) { GameObject::OnGui(context); }
void Manager::Update(float deltaTime) { GameObject::Update(deltaTime); }
void Manager::Clear() {}
int Manager::GetSize() const { return sideSize; }
