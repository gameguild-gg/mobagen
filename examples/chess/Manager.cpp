#include "Manager.h"
#include "ColorT.h"
#include "Point2D.h"
#include "pieces/Bishop.h"
#include "pieces/King.h"
#include "pieces/Knight.h"
#include "pieces/Pawn.h"
#include "pieces/PieceSvg.h"
#include "pieces/Queen.h"
#include "pieces/Rook.h"
#include <unordered_map>
#include "Search.h"
#include "Heuristics.h"

void Manager::OnGui(ImGuiContext* context) {
  ImGui::SetCurrentContext(context);

  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", ImGui::GetIO().DeltaTime * 1000, 1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
  ImGui::Separator();
  if (ImGui::Button("Reset")) {
    this->state.Reset();
    score = Heuristics::MaterialScore(&state);
  };
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

  ImGui::End();  // end settings

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

      if (selected.x == INT32_MIN || !validMoves.contains(index)) {  // if not selected
        selected = index;
        if (piece.Piece() != PieceType::NONE && piece.Color() == state.GetTurn()) {
          // todo: create isincheck behavior and reject valid moves that maintain king in check
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
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
  float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
  auto lineColor = Color::LightGray;
  auto squareSide = minDimension / 8;
  auto sideSideOver2 = 8 / 2.0f;

  Vector2 relativePosFloat(pos.x - center.x, pos.y - center.y);

  relativePosFloat *= 0.99f;
  relativePosFloat += Vector2{minDimension / 2, minDimension / 2};
  relativePosFloat /= squareSide;

  return {static_cast<int>(relativePosFloat.x), static_cast<int>(8 - relativePosFloat.y)};
}

void Manager::OnDraw(SDL_Renderer* renderer) {
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
  float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
  auto squareSide = minDimension / 8;
  auto squareSideOver2 = squareSide / 2;
  auto sideSideOver2 = 8 / 2.0f;

  auto whiteCell = Color::LightBlue.Light();
  auto blackCell = Color::Brown.Light();
  auto movesCell = Color::Yellow.Dark();
  auto selectedCell = Color::Yellow.Light();
  for (int line = 0; line < 8; line++) {
    for (int column = 0; column < 8; column++) {
      SDL_Rect rect = {(int)(ceil(center.x + (column - sideSideOver2) * squareSide)),
                       (int)(ceil(center.y + (-line - 1 + sideSideOver2) * squareSide)), (int)(ceil(squareSide)), (int)(ceil(squareSide))};

      if (selected.y == line && selected.x == column)
        drawSquare(renderer, selectedCell, rect);
      else if (validMoves.contains(Point2D(column, line)))
        drawSquare(renderer, movesCell, rect);
      else if ((line + column) % 2 == 0)
        drawSquare(renderer, blackCell, rect);
      else
        drawSquare(renderer, whiteCell, rect);

      drawPiece(renderer, state.PieceAtPosition({column, line}), {rect.x + squareSideOver2, rect.y + squareSideOver2},
                Vector2::identity() * squareSide);
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
void Manager::drawSquare(SDL_Renderer* renderer, Color32& color, SDL_Rect& rect) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(renderer, &rect);
}
void Manager::drawPiece(SDL_Renderer* renderer, PieceData piece, Vector2 location, Vector2 scale) {
  if (piecePackedToTexture.contains(piece.Pack())) {
    auto tex = piecePackedToTexture[piece.Pack()];
    tex->Draw(renderer, location, scale / Vector2(tex->dimensions.x, tex->dimensions.y));
  }
}

Manager::Manager(Engine* pEngine) : GameObject(pEngine) {
  state.Reset();
  cout << state.toString() << endl;
  // todo: use asset subsystem loading!
  piecePackedToTexture[PieceData(PieceColor::White, PieceType::Pawn).Pack()] = Texture::LoadSVGFromString(engine->window->sdlRenderer, PawnSvgWhite);
  piecePackedToTexture[PieceData(PieceColor::Black, PieceType::Pawn).Pack()] = Texture::LoadSVGFromString(engine->window->sdlRenderer, PawnSvgBlack);

  piecePackedToTexture[PieceData(PieceColor::White, PieceType::Knight).Pack()]
      = Texture::LoadSVGFromString(engine->window->sdlRenderer, KnightSvgWhite);
  piecePackedToTexture[PieceData(PieceColor::Black, PieceType::Knight).Pack()]
      = Texture::LoadSVGFromString(engine->window->sdlRenderer, KnightSvgBlack);

  piecePackedToTexture[PieceData(PieceColor::White, PieceType::Bishop).Pack()]
      = Texture::LoadSVGFromString(engine->window->sdlRenderer, BishopSvgWhite);
  piecePackedToTexture[PieceData(PieceColor::Black, PieceType::Bishop).Pack()]
      = Texture::LoadSVGFromString(engine->window->sdlRenderer, BishopSvgBlack);

  piecePackedToTexture[PieceData(PieceColor::White, PieceType::Rook).Pack()] = Texture::LoadSVGFromString(engine->window->sdlRenderer, RookSvgWhite);
  piecePackedToTexture[PieceData(PieceColor::Black, PieceType::Rook).Pack()] = Texture::LoadSVGFromString(engine->window->sdlRenderer, RookSvgBlack);

  piecePackedToTexture[PieceData(PieceColor::White, PieceType::Queen).Pack()]
      = Texture::LoadSVGFromString(engine->window->sdlRenderer, QueenSvgWhite);
  piecePackedToTexture[PieceData(PieceColor::Black, PieceType::Queen).Pack()]
      = Texture::LoadSVGFromString(engine->window->sdlRenderer, QueenSvgBlack);

  piecePackedToTexture[PieceData(PieceColor::White, PieceType::King).Pack()] = Texture::LoadSVGFromString(engine->window->sdlRenderer, KingSvgWhite);
  piecePackedToTexture[PieceData(PieceColor::Black, PieceType::King).Pack()] = Texture::LoadSVGFromString(engine->window->sdlRenderer, KingSvgBlack);

  score = Heuristics::MaterialScore(&state);
}

Manager::~Manager() {
  for (auto e : piecePackedToTexture) delete e.second;
}

void Manager::Start() {}

void Manager::Update(float deltaTime) {
  if (aiEnabled && aiColor == state.GetTurn()) {
    auto move = Search::NextMove(state);
    state.Move(move.From(), move.To());
    score = Heuristics::MaterialScore(&state);
  }
}