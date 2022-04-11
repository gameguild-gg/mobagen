#ifndef EDITOR_H
#define EDITOR_H

#include "GameObject.h"

class Editor: GameObject {
public:
    explicit Editor(Engine *pEngine);
    void OnGui(ImGuiContext *context) override;
};
#endif