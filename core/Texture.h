#ifndef MOBAGEN_TEXTURE_H
#define MOBAGEN_TEXTURE_H
#include "SDL_render.h"
#include <string>

class Texture {
 private:
  SDL_Surface* surface = nullptr;
  SDL_Texture* texture = nullptr;
 public:
  Texture();
  ~Texture();
  // todo: hide or wrap this sdl_renderer type
  static Texture* LoadSVGFromString(SDL_Renderer * renderer, const std::string& svgtxt);
};

#endif  // MOBAGEN_TEXTURE_H
