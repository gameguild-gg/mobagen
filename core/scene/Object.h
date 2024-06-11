#ifndef MOBAGEN_ENGINE_OBJECT_H_
#define MOBAGEN_ENGINE_OBJECT_H_

#include <string>
#include <vector>
#include <cstdint>

class Object {
public:
  const std::string& name() const { return _name; }
  void name(const std::string& name) { _name = name; }

private:
  std::string _name;

private:
  int _instanceId;  // todo: initialize this properly

public:
  // todo: make it const to protect from modification
  std::uint64_t instanceId() { return _instanceId; }

  // todo: equals comparison and attribution via copy or move ?
public:
  Object(){};  // todo: implement copy and set internal variables

public:
  // todo: mark to be deleted next frame
  static void Destroy(const Object& object){};

public:
  template <class T> static std::vector<T> FindObjectsOfType();

public:
  static void DontDestroyOnLoad(const Object& object){};

public:
  std::string ToString() { return _name; };
};

#endif  // MOBAGEN_ENGINE_OBJECT_H_
