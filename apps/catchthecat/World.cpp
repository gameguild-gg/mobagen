#include "World.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

void CatWorld::print() const {
  auto catposid = (catPosition_.y + sideSize_ / 2) * sideSize_ + (catPosition_.x + sideSize_ / 2);
  for (int i = 0; i < static_cast<int>(worldState_.size());) {
    std::cout << ((i == catposid) ? 'C' : (worldState_[i] ? '#' : '.'));
    i++;
    if ((i + sideSize_) % (2 * sideSize_) == 0)
      std::cout << std::endl << " ";
    else if (i % sideSize_ == 0)
      std::cout << std::endl;
    else
      std::cout << " ";
  }
}

CatWorld::CatWorld(int size) : sideSize_(size) {
  if (size % 2 == 0) throw std::invalid_argument("CatWorld size must be odd");
  clearWorld();
}

CatWorld::CatWorld(int mapSideSize, bool isCatTurn, Point2D catPos, std::vector<bool> map)
    : sideSize_(mapSideSize), catPosition_(catPos), catTurn_(isCatTurn), worldState_(std::move(map)) {}

void CatWorld::clearWorld() {
  worldState_.clear();
  worldState_.resize(sideSize_ * sideSize_);
  for (auto&& i : worldState_) i = false;
  for (int i = 0; i < static_cast<int>(sideSize_ * sideSize_ * 0.05); i++)
    worldState_[Random::Range(0, static_cast<int>(worldState_.size()) - 1)] = true;
  catPosition_ = {0, 0};
  worldState_[static_cast<int>(worldState_.size()) / 2] = false;  // clear cat
  isSimulating_ = false;
  catTurn_ = true;
  timeForNextTick_ = timeBetweenAITicks_;
  catWon_ = false;
  catcherWon_ = false;
}

Point2D CatWorld::E(const Point2D& p) { return {p.x + 1, p.y}; }
Point2D CatWorld::W(const Point2D& p) { return {p.x - 1, p.y}; }

Point2D CatWorld::NE(const Point2D& p) {
  if (p.y % 2) return {p.x + 1, p.y - 1};
  return {p.x, p.y - 1};
}

Point2D CatWorld::NW(const Point2D& p) {
  if (p.y % 2) return {p.x, p.y - 1};
  return {p.x - 1, p.y - 1};
}

Point2D CatWorld::SE(const Point2D& p) {
  if (p.y % 2) return {p.x, p.y + 1};
  return {p.x - 1, p.y + 1};
}

Point2D CatWorld::SW(const Point2D& p) {
  if (p.y % 2) return {p.x + 1, p.y + 1};
  return {p.x, p.y + 1};
}

bool CatWorld::isValidPosition(const Point2D& p) const {
  auto sideOver2 = sideSize_ / 2;
  return (p.x >= -sideOver2) && (p.x <= sideOver2) && (p.y <= sideOver2) && (p.y >= -sideOver2);
}

bool CatWorld::isNeighbor(const Point2D& p1, const Point2D& p2) {
  return NE(p1) == p2 || NW(p1) == p2 || E(p1) == p2 || W(p1) == p2 || SE(p1) == p2 || SW(p1) == p2;
}

void CatWorld::update(float deltaTime) {
  if (isSimulating_) {
    timeForNextTick_ -= deltaTime;
    if (timeForNextTick_ < 0.0f) {
      step();
      timeForNextTick_ = timeBetweenAITicks_;
    }
  }
}

void CatWorld::step() {
  if (catWon_ || catcherWon_) {
    clearWorld();
    return;
  }

  auto start = std::chrono::high_resolution_clock::now();

  if (catTurn_) {
    auto move = cat_.Move(this);
    if (catCanMoveToPosition(move)) {
      catPosition_ = move;
      catWon_ = catWinVerification();
    } else {
      isSimulating_ = false;
      catcherWon_ = true;  // cat made a bad move
    }
  } else {
    auto move = catcher_.Move(this);
    if (catcherCanMoveToPosition(move)) {
      worldState_[(move.y + sideSize_ / 2) * sideSize_ + move.x + sideSize_ / 2] = true;
      catcherWon_ = catcherWinVerification();
    } else {
      isSimulating_ = false;
      catWon_ = true;  // catcher made a bad move
    }
  }

  auto stop = std::chrono::high_resolution_clock::now();
  moveDuration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
  catTurn_ = !catTurn_;
}

bool CatWorld::catWinVerification() const {
  auto sideOver2 = sideSize_ / 2;
  return std::abs(catPosition_.x) == sideOver2 || std::abs(catPosition_.y) == sideOver2;
}

bool CatWorld::catcherWinVerification() const {
  return getContent(NE(catPosition_)) && getContent(NW(catPosition_)) && getContent(E(catPosition_)) && getContent(W(catPosition_))
         && getContent(SE(catPosition_)) && getContent(SW(catPosition_));
}

bool CatWorld::catCanMoveToPosition(Point2D p) const { return isNeighbor(catPosition_, p) && !getContent(p); }

bool CatWorld::catcherCanMoveToPosition(Point2D p) const {
  auto sideOver2 = sideSize_ / 2;
  return (p.x != catPosition_.x || p.y != catPosition_.y) && std::abs(p.x) <= sideOver2 && std::abs(p.y) <= sideOver2;
}

bool CatWorld::catWinsOnSpace(Point2D point) const {
  auto sideOver2 = sideSize_ / 2;
  return std::abs(point.x) == sideOver2 || std::abs(point.y) == sideOver2;
}
