#ifndef MOBAGEN_NODE_H
#define MOBAGEN_NODE_H
#include <iostream>
#include <vector>

// TODO: unstable interface
struct Node {
 public:
  Node() = default;
  Node(bool north, bool east, bool south, bool west){
    data=north;
    data|=east<<1;
    data|=south<<2;
    data|=west<<3;
  }
 private:
  uint8_t data;
 public:
  bool inline GetNorth(){return data & 0x1;};
  bool inline GetEast(){return data>>1 & 0x1;};
  bool inline GetSouth(){return data>>2 & 0x1;};
  bool inline GetWest(){return data>>3 & 0x1;};

  // todo set
  void inline SetNorth(bool x){
    data = x ? data | 1U << 0 : data & ~(1U << 0);
  };
  void inline SetEast(bool x){
    data = x ? data | 1U << 1 : data & ~(1U << 1);
  };
  void inline SetSouth(bool x){
    data = x ? data | 1U << 2 : data & ~(1U << 2);
  };
  void inline SetWest(bool x){
    data = x ? data | 1U << 3 : data & ~(1U << 3);
  };
};

#endif  // MOBAGEN_NODE_H
