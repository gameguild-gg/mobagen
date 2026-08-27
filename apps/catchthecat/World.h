#ifndef WORLD_H
#define WORLD_H

// CatWorld — pure game-state logic for Catch The Cat.
// No rendering, no engine dependency. Rendering and ECS bootstrap live in main.cpp.
// Renamed from World to avoid collision with ecs::World from the DOD core.

#include "Cat.h"
#include "Catcher.h"
#include "Random.h"
#include <cstdint>
#include <iostream>
#include <vector>

// Point2D is available transitively via Cat.h -> Agent.h -> glm::ivec2 alias.

class CatWorld {
private:
  float timeBetweenAITicks_ = 1.0f;
  float timeForNextTick_ = 1.0f;
  bool catTurn_ = true;
  bool isSimulating_ = false;
  Point2D catPosition_ = {0, 0};
  bool catWon_ = false;
  bool catcherWon_ = false;

  Cat cat_;
  Catcher catcher_;

  std::vector<bool> worldState_;  // false = empty, true = blocked
  int sideSize_ = 0;

  void clearWorld();
  bool catWinVerification() const;
  bool catcherWinVerification() const;

public:
  Point2D lastMove = {0, 0};
  int64_t moveDuration = 0;

  explicit CatWorld(int size = 11);
  CatWorld(int mapSideSize, bool isCatTurn, Point2D catPos, std::vector<bool> map);

  // Hex-grid direction helpers (offset coordinates, same parity convention as original)
  static Point2D NE(const Point2D& p);
  static Point2D NW(const Point2D& p);
  static Point2D E(const Point2D& p);
  static Point2D W(const Point2D& p);
  static Point2D SE(const Point2D& p);
  static Point2D SW(const Point2D& p);

  // Accessors
  Point2D getCat() const { return catPosition_; }
  int getWorldSideSize() const { return sideSize_; }

  bool getContent(const Point2D& p) const { return worldState_[(p.y + sideSize_ / 2) * sideSize_ + p.x + sideSize_ / 2]; }
  bool getContent(int x, int y) const { return worldState_[(y + sideSize_ / 2) * sideSize_ + x + sideSize_ / 2]; }
  const std::vector<bool>& worldState() const { return worldState_; }

  void print() const;
  bool isValidPosition(const Point2D& p) const;
  static bool isNeighbor(const Point2D& p1, const Point2D& p2);

  // Game tick
  void step();
  void update(float deltaTime);

  // State accessors for GUI / ECS rendering in main.cpp
  bool isCatTurn() const { return catTurn_; }
  bool catWon() const { return catWon_; }
  bool catcherWon() const { return catcherWon_; }
  bool isSimulating() const { return isSimulating_; }
  void setSimulating(bool v) { isSimulating_ = v; }
  float timeBetweenAITicks() const { return timeBetweenAITicks_; }
  float& timeBetweenAITicksRef() { return timeBetweenAITicks_; }
  float timeForNextTick() const { return timeForNextTick_; }
  void randomize() { clearWorld(); }
  void setSizeAndReset(int n) {
    sideSize_ = n;
    clearWorld();
  }

  // Move validation (used by Cat / Catcher agents)
  bool catCanMoveToPosition(Point2D pos) const;
  bool catcherCanMoveToPosition(Point2D pos) const;
  bool catWinsOnSpace(Point2D point) const;

  static std::vector<Point2D> neighbors(Point2D point) { return {NE(point), NW(point), E(point), W(point), SW(point), SE(point)}; }
};

#endif  // WORLD_H
