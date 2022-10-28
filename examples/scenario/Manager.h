#ifndef MANAGER_H
#define MANAGER_H

#include "GameObject.h"

class Manager: public GameObject{
 public:
  explicit Manager(Engine* engine, int size);
};

#endif  // MANAGER_H
