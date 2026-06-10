#ifndef CHESS_MANAGER_H
#define CHESS_MANAGER_H

#include "Texture.h"
#include "Renderer2D.h"
#include "WorldState.h"
#include "math/ColorT.h"
#include "engine/Engine.h"
#include "scene/GameObject.h"
#include <iostream>
#include <map>
#include <unordered_set>
#include <stack>

class Manager : GameObject {
private:
  WorldState state;
  stack<WorldState> previousStates;
  Point2D selected = {INT32_MIN, INT32_MIN};
  unordered_set<Point2D> validMoves;
  map<uint8_t, Texture*> piecePackedToTexture;
  PieceColor aiColor = PieceColor::Black;
  bool aiEnabled = false;

public:
  double score;
  explicit Manager(Engine* pEngine);
  void Start() override;
  ~Manager();
  void OnGui(ImGuiContext* context) override;
  void OnDraw(Renderer2D& r) override;
  void Update(float deltaTime) override;

private:
  Point2D mousePositionToIndex(ImVec2& pos);
  unordered_set<Point2D> getMoves(PieceType t, Point2D point);
  void drawSquare(Renderer2D& r, Color32& color, Rect2D& rect);
  void drawPiece(Renderer2D& r, PieceData piece, Vector2f location, Vector2f scale);
};

#endif  // CHESS_MANAGER_H
