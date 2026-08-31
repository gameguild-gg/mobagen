#include "Manager.h"
#include "PieceTextures.h"
#include "pieces/Bishop.h"
#include "pieces/King.h"
#include "pieces/Knight.h"
#include "pieces/Pawn.h"
#include "pieces/Queen.h"
#include "pieces/Rook.h"
#include "Search.h"
#include "Heuristics.h"
#include <cmath>
#include <iostream>
#include <unordered_map>

static const char* getPieceLabel(PieceData piece) {
  bool w = piece.Color() == PieceColor::White;
  switch (piece.Piece()) {
    case PieceType::King:
      return w ? "K" : "k";
    case PieceType::Queen:
      return w ? "Q" : "q";
    case PieceType::Rook:
      return w ? "R" : "r";
    case PieceType::Bishop:
      return w ? "B" : "b";
    case PieceType::Knight:
      return w ? "N" : "n";
    case PieceType::Pawn:
      return w ? "P" : "p";
    default:
      return nullptr;
  }
}

void Manager::OnGui() {
  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", ImGui::GetIO().DeltaTime * 1000, 1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
  ImGui::Separator();
  if (ImGui::Button("Reset")) {
    state.Reset();
    score = Heuristics::MaterialScore(&state);
  }
  ImGui::SameLine();
  if (ImGui::Button("Undo") && !previousStates.empty()) {
    validMoves = {};
    selected = {INT32_MIN, INT32_MIN};
    state = previousStates.top();
    previousStates.pop();
  }
  ImGui::Separator();

  ImGui::LabelText("Score", "%.1f", score);
  ImGui::Separator();

  if (ImGui::Checkbox("AI Enabled", &aiEnabled))
    if (aiEnabled == true) aiColor = PieceColor::Black;

  static bool aiIsBlackStatic = true;
  if (aiEnabled) {
    if (ImGui::Checkbox("AI is Black", &aiIsBlackStatic)) {
      if (aiIsBlackStatic)
        aiColor = PieceColor::Black;
      else
        aiColor = PieceColor::White;
    }
  }
  ImGui::LabelText(state.GetTurn() == PieceColor::White ? "White" : "Black", "Turn:");

  ImGui::End();

  static Point2D lastIndexClicked = {INT32_MIN, INT32_MIN};
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    auto mousePos = ImGui::GetMousePos();
    Point2D index = mousePositionToIndex(mousePos);

    if (index == selected) {
      validMoves = {};
      selected = {INT32_MIN, INT32_MIN};
    } else if (lastIndexClicked != index) {
      lastIndexClicked = index;

      auto piece = state.PieceAtPosition(index);

      if (selected.x == INT32_MIN || !validMoves.contains(index)) {
        selected = index;
        if (piece.Piece() != PieceType::NONE && piece.Color() == state.GetTurn()) {
          validMoves = getMoves(piece.Piece(), index);
          if (validMoves.empty()) {
            validMoves = {};
            selected = {INT32_MIN, INT32_MIN};
          }
        } else {
          validMoves = {};
          selected = {INT32_MIN, INT32_MIN};
        }
      } else if (validMoves.contains(index)) {
        previousStates.push(state);
        state.Move(selected, index);
        score = Heuristics::MaterialScore(&state);
        cout << state.toString() << endl;
        validMoves = {};
        selected = {INT32_MIN, INT32_MIN};
      } else {
        validMoves = {};
        selected = {INT32_MIN, INT32_MIN};
      }
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    lastIndexClicked = {INT32_MIN, INT32_MIN};
  }
}

Point2D Manager::mousePositionToIndex(ImVec2& pos) {
  ImVec2 ws = ImGui::GetIO().DisplaySize;
  glm::vec2 center(ws.x / 2.0f, ws.y / 2.0f);
  float minDimension = std::min(ws.x, ws.y) * 0.99f;
  float squareSide = minDimension / 8.0f;

  glm::vec2 rel(pos.x - center.x, pos.y - center.y);
  rel *= 0.99f;
  rel += glm::vec2(minDimension / 2.0f, minDimension / 2.0f);
  rel /= squareSide;

  return {static_cast<int>(rel.x), static_cast<int>(8 - rel.y)};
}

void Manager::OnDraw() {
  auto* dl = ImGui::GetBackgroundDrawList();
  ImVec2 ws = ImGui::GetIO().DisplaySize;
  glm::vec2 center(ws.x / 2.0f, ws.y / 2.0f);
  float minDimension = std::min(ws.x, ws.y) * 0.99f;
  float squareSide = minDimension / 8.0f;
  float squareSideOver2 = squareSide / 2.0f;
  float sideSideOver2 = 8 / 2.0f;

  const ImU32 whiteCell = IM_COL32(230, 230, 250, 255);
  const ImU32 blackCell = IM_COL32(140, 90, 50, 255);
  const ImU32 movesCell = IM_COL32(180, 150, 0, 255);
  const ImU32 selectedCell = IM_COL32(240, 240, 0, 255);

  for (int line = 0; line < 8; line++) {
    for (int column = 0; column < 8; column++) {
      float rx = std::ceil(center.x + (column - sideSideOver2) * squareSide);
      float ry = std::ceil(center.y + (-line - 1 + sideSideOver2) * squareSide);
      ImVec2 rmin(rx, ry);
      ImVec2 rmax(rx + std::ceil(squareSide), ry + std::ceil(squareSide));

      if (selected.y == line && selected.x == column)
        drawSquare(selectedCell, rmin, rmax);
      else if (validMoves.contains(Point2D(column, line)))
        drawSquare(movesCell, rmin, rmax);
      else if ((line + column) % 2 == 0)
        drawSquare(blackCell, rmin, rmax);
      else
        drawSquare(whiteCell, rmin, rmax);

      drawPiece(state.PieceAtPosition({column, line}), ImVec2(rx + squareSideOver2, ry + squareSideOver2), squareSide);
    }
  }
}

unordered_set<Point2D> Manager::getMoves(PieceType t, Point2D point) {
  switch (t) {
    case PieceType::Pawn:
      return Pawn::PossibleMoves(state, point);
    case PieceType::Rook:
      return Rook::AttackMoves(state, point);
    case PieceType::Knight:
      return Knight::AttackMoves(state, point);
    case PieceType::Bishop:
      return Bishop::AttackMoves(state, point);
    case PieceType::Queen:
      return Queen::AttackMoves(state, point);
    case PieceType::King:
      return King::AttackMoves(state, point);
    default:
      return {};
  }
}

void Manager::drawSquare(ImU32 color, ImVec2 min, ImVec2 max) { ImGui::GetBackgroundDrawList()->AddRectFilled(min, max, color); }

void Manager::drawPiece(PieceData piece, ImVec2 center, float size) {
  auto* dl = ImGui::GetBackgroundDrawList();

  if (pieceArt) {
    ImTextureID tex = pieceArt->texture(piece);
    if (tex) {
      const float half = size * 0.5f;
      dl->AddImage(tex, ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half));
      return;
    }
  }

  const char* text = getPieceLabel(piece);
  if (!text) return;
  ImVec2 textSize = ImGui::CalcTextSize(text);
  ImVec2 pos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);

  bool isWhite = piece.Color() == PieceColor::White;
  ImU32 outline = isWhite ? IM_COL32(0, 0, 0, 200) : IM_COL32(255, 255, 255, 200);
  ImU32 fill = isWhite ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255);

  dl->AddText(ImVec2(pos.x - 1, pos.y - 1), outline, text);
  dl->AddText(ImVec2(pos.x + 1, pos.y - 1), outline, text);
  dl->AddText(ImVec2(pos.x - 1, pos.y + 1), outline, text);
  dl->AddText(ImVec2(pos.x + 1, pos.y + 1), outline, text);
  dl->AddText(pos, fill, text);
}

Manager::Manager() {
  state.Reset();
  cout << state.toString() << endl;
  score = Heuristics::MaterialScore(&state);
}

Manager::~Manager() {}

void Manager::Start() {}

void Manager::Update(float deltaTime) {
  (void)deltaTime;
  if (aiEnabled && aiColor == state.GetTurn()) {
    auto move = Search::NextMove(state);
    state.Move(move.From(), move.To());
    score = Heuristics::MaterialScore(&state);
  }
}
