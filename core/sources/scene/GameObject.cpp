#include "GameObject.h"
#include "../engine/Engine.h"

GameObject::GameObject(Engine* pEngine) : engine(pEngine), Object() {
  // todo: use a scene manager approach to this
  // todo: this addition should be done only on the end of the frame. it should not be committed now. given this engine is still single threaded, it
  engine->gameObjectsToBeStarted.emplace(this);
}
