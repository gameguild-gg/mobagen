#ifndef MOBAGEN_TEXTURE_H
#define MOBAGEN_TEXTURE_H
#include "Point2D.h"
#include "SDL_render.h"
#include "Transform.h"
#include <string>

// todo: make this a game object
class Texture {
private:
  SDL_Surface* surface = nullptr;
  SDL_Texture* texture = nullptr;

public:
  Point2D dimensions;
  Vector2 scale = {1, 1};
  Vector2 position = {0, 0};

public:
  Texture(){};
  ~Texture();

  void Draw(SDL_Renderer* renderer);
  void Draw(SDL_Renderer* renderer, Vector2 position, Vector2 scale);

  // todo: hide or wrap this sdl_renderer type
  // todo: use asset loader in order do not use strings here!
  static Texture* LoadSVGFromString(SDL_Renderer* renderer, const std::string& svgtxt);
};

#endif  // MOBAGEN_TEXTURE_H
