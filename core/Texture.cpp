#include "Texture.h"
#include "SDL_image.h"
#include "SDL_surface.h"

Texture* Texture::LoadSVGFromString(SDL_Renderer* renderer, const std::string& svgtxt) {
  auto tex = new Texture();

  SDL_RWops* rw = SDL_RWFromConstMem(svgtxt.c_str(), svgtxt.size());
  // todo: check if it is correct
  tex->surface = IMG_Load_RW(rw, 1);
  tex->texture = SDL_CreateTextureFromSurface(renderer, tex->surface);
  SDL_QueryTexture(tex->texture, nullptr, nullptr, &tex->dimensions.x, &tex->dimensions.y);
  return tex;
}
Texture::~Texture() {
  SDL_DestroyTexture(texture);
  // todo: destroy surface!
  //    SDL_DestroySurface(surface);
}
void Texture::Draw(SDL_Renderer* renderer) {
  SDL_Rect rect;
  // todo: make rotation aware
  rect.w = dimensions.x * scale.x;
  rect.h = dimensions.y * scale.y;
  // to center
  rect.x = position.x - rect.w / 2.f;
  rect.y = position.y - rect.h / 2.f;
  SDL_RenderCopy(renderer, texture, nullptr, &rect);
}
void Texture::Draw(SDL_Renderer* renderer, Vector2 position, Vector2 scale) {
  SDL_Rect rect;
  // todo: make rotation aware
  rect.w = dimensions.x * scale.x;
  rect.h = dimensions.y * scale.y;
  // to center
  rect.x = position.x - rect.w / 2.f;
  rect.y = position.y - rect.h / 2.f;
  SDL_RenderCopy(renderer, texture, nullptr, &rect);
}
