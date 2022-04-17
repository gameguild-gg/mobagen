#include "GameObject.h"
#include "Engine.h"

GameObject::GameObject(Engine *pEngine): engine(pEngine) {
    // todo: use a scene manager approach to this
    // todo: this addition should be done only on the end of the frame. it should not be committed now. given this engine is still single threaded, it should be fine
    engine->gameObjects.emplace(this);
}
