#ifndef MOBAGEN_NODE_H
#define MOBAGEN_NODE_H
#include <vector>

// TODO: unstable interface
struct Node {
 public:
  Node() = default;
  Node(bool North, bool East, bool South, bool West){
    data;
  }
 private:
  uint8_t data;
 public:
  bool inline North(){return data & 0x1;};
  bool inline East(){return data>>1 & 0x1;};
  bool inline South(){return data>>2 & 0x01;};
  bool inline West(){return data>>3 & 0x01;};
};

#endif  // MOBAGEN_NODE_H
