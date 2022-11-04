#ifndef MANAGER_H
#define MANAGER_H

#include "ColorT.h"
#include "GameObject.h"
#include <SDL.h>

class Manager: public GameObject {
 private:
  int sideSize;
  SDL_Texture *texture;

 public:
  ~Manager();
  explicit Manager(Engine* engine, int size);

  void Start() override;
  void OnGui(ImGuiContext *context) override;
  void OnDraw(SDL_Renderer* renderer) override;
  void Update(float deltaTime) override;

  void Clear();
  int GetSize() const;

  void SetPixels(std::vector<Color32> &pixels);
};

#endif  // MANAGER_H
