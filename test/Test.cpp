#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>
#include "Engine.h"
#include "GameObject.h"

class Quitter : public GameObject {
public:
  Quitter(Engine* pEngine) : GameObject(pEngine){};
  void Update(float time) override { engine->Exit(); }
};

TEST_CASE("Start and Stop Engine") {
  auto engine = new Engine();
  if (engine->Start("Test engine")) {
    new Quitter(engine);
    engine->Run();
  }
  engine->Exit();
  delete engine;
  engine = nullptr;
  //  CHECK(Scheduler::Instance().RunningWorkersQuantity() == Scheduler::Instance().ConcurrencyLevel());
}