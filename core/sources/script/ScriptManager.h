// #ifndef MOBAGEN_SCRIPTMANAGER_H
// #define MOBAGEN_SCRIPTMANAGER_H
//
// #include "quickjspp.hpp"
//
//// todo: generate bindings for quickjs. see https://github.com/ftk/quickjspp/tree/master/binding-generator
//
//// singleton
// class ScriptManager {
// public:
//   static ScriptManager& getInstance() {
//     static ScriptManager instance;
//     return instance;
//   }
//   ScriptManager(ScriptManager const&) = delete;
//   void operator=(ScriptManager const&) = delete;
//   ~ScriptManager() = default;
//
//   // quickjs stuff
//   qjs::Runtime runtime;
//   qjs::Context context = qjs::Context(runtime);
//
//   void Initialize();
// protected:
//   ScriptManager() = default;
// };
//
// #endif  // MOBAGEN_SCRIPTMANAGER_H
