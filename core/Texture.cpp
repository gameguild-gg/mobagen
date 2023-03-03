#include "Texture.h"
#include "SDL_image.h"
#include "SDL_surface.h"

std::shared_ptr<Texture> Texture::LoadSVGFromString(SDL_Renderer* renderer, const std::string& svgtxt) {
  auto tex = std::make_shared<Texture>();

  SDL_RWops *rw = SDL_RWFromConstMem(svgtxt.c_str(), svgtxt.size());
  //todo: check if it is correct
  tex->surface = IMG_Load_RW(rw, 1);
  tex->texture = SDL_CreateTextureFromSurface(renderer, tex->surface);
  return tex;
}
Texture::~Texture() {
    SDL_DestroyTexture(texture);
    // todo: destroy surface!
//    SDL_DestroySurface(surface);
}
