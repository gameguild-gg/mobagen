#include "Texture.h"
#include "SDL_image.h"

std::shared_ptr<Texture> Texture::LoadSVGFromString(SDL_Renderer* renderer, const std::string& svgtxt) {
  auto tex = std::make_shared<Texture>();

  SDL_RWops *rw = SDL_RWFromConstMem(svgtxt.c_str(), svgtxt.size());
  tex->surface = std::shared_ptr<SDL_Surface>(IMG_Load_RW(rw, 1));
  tex->texture = std::shared_ptr<SDL_Texture>(SDL_CreateTextureFromSurface(renderer, tex->surface.get()));
  return tex;
}
