#include "Manager.h"
#include "ColorT.h"
#include "Point2D.h"
#include "pieces/Bishop.h"
#include "pieces/King.h"
#include "pieces/Knight.h"
#include "pieces/Pawn.h"
#include "pieces/Queen.h"
#include "pieces/Rook.h"
#include <unordered_map>

void Manager::OnGui(ImGuiContext* context) {
  ImGui::SetCurrentContext(context);

  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS",
              ImGui::GetIO().DeltaTime * 1000,
              1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate,
              ImGui::GetIO().Framerate);

  ImGui::End(); // end settings

  static Point2D lastIndexClicked = {INT32_MIN, INT32_MIN};
  if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
    auto mousePos = ImGui::GetMousePos();
    Point2D index = mousePositionToIndex(mousePos);

    if(lastIndexClicked!=index) {
      lastIndexClicked = index;

      auto piece = state.PieceAtPosition(index);

      if(selected.x == INT32_MIN || !validMoves.contains(index)) {  // if not selected
        selected = index;
        if (piece.piece != PieceType::NONE && piece.color == state.GetTurn())
          validMoves = getMoves(piece.piece, index);
        else {
          validMoves = {};
          selected = {INT32_MIN,INT32_MIN};
        }
      }
      else if(validMoves.contains(index)) {
        // move!
        cout << "move!" << endl;
        state.SetPieceAtPosition(state.PieceAtPosition(selected), index);
        state.SetPieceAtPosition({PieceType::NONE, PieceColor::NONE}, selected);
        state.EndTurn();
        cout << state.toString() << endl;
        validMoves = {};
        selected = {INT32_MIN, INT32_MIN};
      }
      else {
        validMoves = {};
        selected = {INT32_MIN, INT32_MIN};
      }
    }
  } if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
    lastIndexClicked = {INT32_MIN, INT32_MIN};
  }
}

Point2D Manager::mousePositionToIndex(ImVec2& pos) {
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x/2, windowSize.y/2);
  float minDimension = std::min(windowSize.x, windowSize.y)*0.99f;
  auto lineColor = Color::LightGray;
  auto squareSide = minDimension/8;
  auto sideSideOver2 = 8/2.0f;

  Vector2 relativePosFloat(pos.x - center.x, pos.y-center.y);

  relativePosFloat*=0.99f;
  relativePosFloat+=Vector2{minDimension/2, minDimension/2};
  relativePosFloat/=squareSide;

  return Point2D(relativePosFloat.x, 8-relativePosFloat.y);
}

void Manager::OnDraw(SDL_Renderer* renderer) {
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
  float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
  auto squareSide = minDimension / 8;
  auto squareSideOver2 = squareSide/2;
  auto sideSideOver2 = 8 / 2.0f;

  auto whiteCell = Color::LightBlue.Light();
  auto blackCell = Color::DarkBlue.Dark();
  auto movesCell = Color::Yellow.Dark();
  auto selectedCell = Color::Yellow.Light();
  for (int line = 0; line < 8; line++) {
    for (int column = 0; column < 8; column++) {
      SDL_Rect rect = {
        (int)(ceil(center.x + (column - sideSideOver2) * squareSide)),
        (int)(ceil(center.y + (-line - 1 + sideSideOver2) * squareSide)),
        (int)(squareSide), (int)(squareSide)};

      if(selected.y==line && selected.x==column)
        drawSquare(renderer, selectedCell, rect);
      else if(validMoves.contains(Point2D(column,line)))
        drawSquare(renderer, movesCell, rect);
      else if ((line + column) % 2 == 0)
        drawSquare(renderer, blackCell, rect);
      else
        drawSquare(renderer, whiteCell, rect);

      drawPiece(renderer,
                state.PieceAtPosition({column,line}),
                {rect.x+squareSideOver2,rect.y+squareSideOver2},
                Vector2::identity()*squareSideOver2);
      drawPiece(renderer,
                state.PieceAtPosition({column,line}),
                {rect.x+squareSideOver2,rect.y+squareSideOver2},
                Vector2::identity()*squareSideOver2*0.95);
    }
  }

}

unordered_set<Point2D> Manager::getMoves(PieceType t, Point2D point) {
  switch (t) {
    case PieceType::Pawn:
      return Pawn::PossibleMoves(state, point);
    case PieceType::Rook:
      return Rook::PossibleMoves(state, point);
    case PieceType::Knight:
      return Knight::PossibleMoves(state, point);
    case PieceType::Bishop:
      return Bishop::PossibleMoves(state, point);
    case PieceType::Queen:
      return Queen::PossibleMoves(state, point);
    case PieceType::King:
      return King::PossibleMoves(state, point);
    default:
      return {};
  }
}
void Manager::drawSquare(SDL_Renderer* renderer, Color32& color, SDL_Rect& rect) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
                         SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(renderer, &rect);
}
void Manager::drawPiece(SDL_Renderer* renderer, PieceData piece,
                        Vector2 location, Vector2 scale) {
  static auto blackColor = Color::DarkGreen;
  static auto whiteColor = Color::DarkMagenta;
  switch (piece.piece) {
    case PieceType::Pawn:
      return Pawn::polygon.Draw(renderer,location,scale,Vector2::zero(), piece.color==PieceColor::White?whiteColor:blackColor);
//    case PieceType::Rook:
//      return Rook::PossibleMoves(state, point);
//    case PieceType::Knight:
//      return Knight::PossibleMoves(state, point);
//    case PieceType::Bishop:
//      return Bishop::PossibleMoves(state, point);
//    case PieceType::Queen:
//      return Queen::PossibleMoves(state, point);
//    case PieceType::King:
//      return King::PossibleMoves(state, point);
  }
}
