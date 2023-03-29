#ifndef MOBAGEN_SCRIPTEDBEHAVIOUR_H
#define MOBAGEN_SCRIPTEDBEHAVIOUR_H

#include "../error/NotImplemented.h"
#include "Behaviour.h"
#include <string>
#include <SDL.h>

// equivalent to monobeaviour in unity
class ScriptedBehaviour: public Behaviour {
  bool IsInvoking() { throw NotImplemented(); }
  void CancelInvoke() {throw NotImplemented();}
  void Invoke(std::string methodName, float time) {throw NotImplemented();}
  void InvokeRepeating(std::string methodName, float time, float repeatRate) {throw NotImplemented();}
  void CancelInvoke(std::string methodName) {throw NotImplemented();}
  bool IsInvoking(std::string methodName) {throw NotImplemented();}
  // Coroutine StartCoroutine(std::string methodName);
  // Coroutine StartCoroutine(string methodName, object value == nullptr);
  // Coroutine StartCoroutine(IEnumerator routine);
  // void StopCoroutine(IEnumerator routine);
  void StopCoroutine(std::string methodName){throw NotImplemented();};
  void StopAllCoroutines(){throw NotImplemented();};
  void print(std::string message) { SDL_Log("%s", message.c_str());};
};

#endif  // MOBAGEN_SCRIPTEDBEHAVIOUR_H
