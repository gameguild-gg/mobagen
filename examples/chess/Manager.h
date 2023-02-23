#ifndef CHESS_MANAGER_H
#define CHESS_MANAGER_H

#include <Engine.h>
#include <GameObject.h>


class Manager: GameObject  {
 public:
  explicit Manager(Engine* pEngine) : GameObject(pEngine) {}
};

#endif  // CHESS_MANAGER_H
