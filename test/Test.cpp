#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>
#include "Engine.h"
#include "GameObject.h"
//#include "cpr/cpr.h"

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
//
// TEST_CASE("Start and Stop Engine") {
//  cpr::Response r = cpr::Get(cpr::Url{"https://google.com"});//, cpr::VerifySsl(false));
//  r.status_code;             // 200
//  r.header["content-type"];  // application/json; charset=utf-8
//  r.text;                    // JSON text string
//
//  CHECK(r.status_code == 200);
//}