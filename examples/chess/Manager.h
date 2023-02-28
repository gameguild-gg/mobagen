#ifndef CHESS_MANAGER_H
#define CHESS_MANAGER_H

#include "WorldState.h"
#include <Engine.h>
#include <GameObject.h>
#include <iostream>

class Manager: GameObject  {
 private:
  WorldState state;
  vector<WorldState> previousStates;

 public:
  explicit Manager(Engine* pEngine) : GameObject(pEngine) {
    state.Reset();
    cout << state.toString() << endl;
  }
  void OnGui(ImGuiContext* context) override;
  void OnDraw(SDL_Renderer* renderer) override;
};

#endif  // CHESS_MANAGER_H
