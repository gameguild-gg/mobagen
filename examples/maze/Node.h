#ifndef MOBAGEN_NODE_H
#define MOBAGEN_NODE_H
#include <vector>

// TODO: unstable interface
struct Node {
 public:
  Node() = default;
  Node(bool north, bool east, bool south, bool west){
    data+=(uint8_t)north;
    data+=((uint8_t)north)>>1;
    data+=((uint8_t)north)>>2;
    data+=((uint8_t)north)>>3;
  }
 private:
  uint8_t data;
 public:
  bool inline GetNorth(){return data & 0x1 == 0x1;};
  bool inline GetEast(){return data>>1 & 0x1 == 0x1;};
  bool inline GetSouth(){return data>>2 & 0x01 == 0x1;};
  bool inline GetWest(){return data>>3 & 0x01 == 0x1;};

  // todo set
  void inline SetNorth(bool x){};
  void inline SetEast(bool x){};
  void inline SetSouth(bool x){};
  void inline SetWest(bool x){};
};

#endif  // MOBAGEN_NODE_H
