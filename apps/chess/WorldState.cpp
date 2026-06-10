#include <iostream>
#include "WorldState.h"
PieceData WorldState::PieceAtPosition(Point2D pos) {
  if (pos.x < 0 || pos.x > 7 || pos.y < 0 || pos.y > 7) return PieceData::Wrong();

  auto index = (pos.y * 8 + pos.x) / 2;
  auto value = state[index];
  if (pos.x % 2 == 0)
    value &= 0b00001111;
  else
    value = value >> 4;

  return PieceData::UnPack(value);
}

void WorldState::Reset() {
  turn = PieceColor::White;

  for (int i = 0; i < 32; i++)  // probably memset is better
    state[i] = 0;

  SetPieceAtPosition({PieceColor::White, PieceType::Rook}, {0, 0});
  SetPieceAtPosition({PieceColor::White, PieceType::Knight}, {1, 0});
  SetPieceAtPosition({PieceColor::White, PieceType::Bishop}, {2, 0});
  SetPieceAtPosition({PieceColor::White, PieceType::Queen}, {3, 0});
  SetPieceAtPosition({PieceColor::White, PieceType::King}, {4, 0});
  SetPieceAtPosition({PieceColor::White, PieceType::Bishop}, {5, 0});
  SetPieceAtPosition({PieceColor::White, PieceType::Knight}, {6, 0});
  SetPieceAtPosition({PieceColor::White, PieceType::Rook}, {7, 0});

  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {0, 1});
  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {1, 1});
  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {2, 1});
  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {3, 1});
  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {4, 1});
  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {5, 1});
  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {6, 1});
  SetPieceAtPosition({PieceColor::White, PieceType::Pawn}, {7, 1});

  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {0, 6});
  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {1, 6});
  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {2, 6});
  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {3, 6});
  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {4, 6});
  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {5, 6});
  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {6, 6});
  SetPieceAtPosition({PieceColor::Black, PieceType::Pawn}, {7, 6});

  SetPieceAtPosition({PieceColor::Black, PieceType::Rook}, {0, 7});
  SetPieceAtPosition({PieceColor::Black, PieceType::Knight}, {1, 7});
  SetPieceAtPosition({PieceColor::Black, PieceType::Bishop}, {2, 7});
  SetPieceAtPosition({PieceColor::Black, PieceType::Queen}, {3, 7});
  SetPieceAtPosition({PieceColor::Black, PieceType::King}, {4, 7});
  SetPieceAtPosition({PieceColor::Black, PieceType::Bishop}, {5, 7});
  SetPieceAtPosition({PieceColor::Black, PieceType::Knight}, {6, 7});
  SetPieceAtPosition({PieceColor::Black, PieceType::Rook}, {7, 7});
}
void WorldState::SetPieceAtPosition(PieceData piece, Point2D pos) {
  auto packed = PieceData::Pack(piece);

  auto index = (pos.y * 8 + pos.x) / 2;
  auto value = state[index];

  if (pos.x % 2 == 0)
    value = (value & 0b11110000) | packed;
  else
    value = (value & 0b00001111) | (packed << 4);

  state[index] = value;
}
string WorldState::toString() {
  string str;
  for (int line = 7; line >= 0; line--) {
    str += ('1' + line);
    for (int col = 0; col < 8; col++) {
      str += ' ';
      str += PieceAtPosition({col, line}).toChar();
    }
    str += '\n';
  }
  str += "  A B C D E F G H\n";
  return str;
}
void WorldState::Move(Point2D from, Point2D to) {
  auto pieceFrom = PieceAtPosition(from);
  auto pieceTo = PieceAtPosition(to);

  if (pieceFrom.Piece() == PieceType::WRONG) {
    std::cout << "Wrong FROM piece at position: " << from.to_string() << endl;
    return;
  }

  if (pieceFrom.Color() != turn) {
    std::cout << "Piece color does not match the turn at position: " << from.to_string() << endl;
    return;
  }

  if (pieceTo.Piece() == PieceType::NONE || (pieceFrom.Color() != pieceTo.Color())) {
    this->SetPieceAtPosition(pieceFrom, to);
    this->SetPieceAtPosition(PieceData::Empty(), from);
    EndTurn();
    return;
  }

  std::cout << "WRONG piece at position: " << from.to_string() << endl;
}

char PieceData::toChar() {
  char c = '.';
  switch (piece) {
    case PieceType::Pawn:
      c = 'p';
      break;
    case PieceType::Rook:
      c = 'r';
      break;
    case PieceType::Knight:
      c = 'n';
      break;
    case PieceType::Bishop:
      c = 'b';
      break;
    case PieceType::Queen:
      c = 'q';
      break;
    case PieceType::King:
      c = 'k';
      break;
    default:
      c = '.';
      break;
  }
  if (color == PieceColor::White)
    return (char)toupper(c);
  else
    return c;
}
bool PieceData::operator==(const PieceData& rhs) const { return this->color == rhs.color && this->piece == rhs.piece; }

vector<Move> Move::GenerateListOfMoves(PieceData piece, Point2D from, unordered_set<Point2D> to) {
  vector<Move> moves;
  for (auto t : to) moves.emplace_back(from, t, piece, MoveType::Normal);
  return moves;
}
