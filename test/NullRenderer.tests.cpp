//#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
//
//#include <doctest/doctest.h>
//#include "engine/Engine.h"
//#include "scene/GameObject.h"
//
//class Quitter : public GameObject {
//public:
//  explicit Quitter(Engine* pEngine) : GameObject(pEngine){};
//  void Update(float time) override { engine->Exit(); }
//};
//
//TEST_CASE("Engine should run without Renderering") {
//  auto* engine = new Engine();
//  if (engine->Start("Test engine")) {
//    new Quitter(engine);
//    engine->Run();
//  }
//  engine->Exit();
//  delete engine;
//  engine = nullptr;
//}