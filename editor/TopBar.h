#ifndef MOBAGEN_TOPBAR_H
#define MOBAGEN_TOPBAR_H

#include "scene/ScriptableObject.h"

class TopBar : public ScriptableObject {
private:
  bool enabled=true;

public:
  explicit TopBar() : ScriptableObject(){};

  void OnGui(ImGuiContext* context) override;
};

#endif  // MOBAGEN_TOPBAR_H
