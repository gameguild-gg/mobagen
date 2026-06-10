#ifndef MOBAGEN_BEHAVIOUR_H
#define MOBAGEN_BEHAVIOUR_H

#include "Component.h"
class Behaviour : public Component {
public:
  bool enabled = true;
  bool isActiveAndEnabled = true;
};

#endif  // MOBAGEN_BEHAVIOUR_H
