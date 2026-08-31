#ifndef CHESS_MANAGER_H
#define CHESS_MANAGER_H

#include "imgui.h"
#include "WorldState.h"
#include <glm/glm.hpp>
#include <iostream>
#include <map>
#include <stack>
#include <unordered_set>

class PieceTextures;

class Manager {
private:
  WorldState state;
  stack<WorldState> previousStates;
  Point2D selected = {INT32_MIN, INT32_MIN};
  unordered_set<Point2D> validMoves;
  PieceColor aiColor = PieceColor::Black;
  bool aiEnabled = false;
  PieceTextures* pieceArt = nullptr;

public:
  double score = 0.0;
  Manager();
  ~Manager();

  void SetPieceArt(PieceTextures* art) { pieceArt = art; }
  void Start();
  void OnGui();
  void OnDraw();
  void Update(float deltaTime);

private:
  Point2D mousePositionToIndex(ImVec2& pos);
  unordered_set<Point2D> getMoves(PieceType t, Point2D point);
  void drawSquare(ImU32 color, ImVec2 min, ImVec2 max);
  void drawPiece(PieceData piece, ImVec2 center, float size);
};

#endif  // CHESS_MANAGER_H
