#ifndef MOBAGEN_TEXTURE_H
#define MOBAGEN_TEXTURE_H
#include "SDL_render.h"
#include <memory>
#include <string>
class Texture {
 private:
  std::shared_ptr<SDL_Surface> surface;
  std::shared_ptr<SDL_Texture> texture;
 public:
  Texture();
  // todo: hide or wrap this sdl_renderer type
  static std::shared_ptr<Texture> LoadSVGFromString(SDL_Renderer * renderer, const std::string& svgtxt);
};

#endif  // MOBAGEN_TEXTURE_H
