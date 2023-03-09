#include <iostream>
#include "WorldState.h"
PieceData WorldState::PieceAtPosition(Point2D pos) {
  if (pos.x < 0 || pos.x > 7 || pos.y < 0 || pos.y > 7) return {PieceType::WRONG, PieceColor::NONE};

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

  SetPieceAtPosition({PieceType::Rook, PieceColor::White}, {0, 0});
  SetPieceAtPosition({PieceType::Knight, PieceColor::White}, {1, 0});
  SetPieceAtPosition({PieceType::Bishop, PieceColor::White}, {2, 0});
  SetPieceAtPosition({PieceType::Queen, PieceColor::White}, {3, 0});
  SetPieceAtPosition({PieceType::King, PieceColor::White}, {4, 0});
  SetPieceAtPosition({PieceType::Bishop, PieceColor::White}, {5, 0});
  SetPieceAtPosition({PieceType::Knight, PieceColor::White}, {6, 0});
  SetPieceAtPosition({PieceType::Rook, PieceColor::White}, {7, 0});

  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {0, 1});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {1, 1});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {2, 1});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {3, 1});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {4, 1});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {5, 1});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {6, 1});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::White}, {7, 1});

  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {0, 6});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {1, 6});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {2, 6});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {3, 6});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {4, 6});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {5, 6});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {6, 6});
  SetPieceAtPosition({PieceType::Pawn, PieceColor::Black}, {7, 6});

  SetPieceAtPosition({PieceType::Rook, PieceColor::Black}, {0, 7});
  SetPieceAtPosition({PieceType::Knight, PieceColor::Black}, {1, 7});
  SetPieceAtPosition({PieceType::Bishop, PieceColor::Black}, {2, 7});
  SetPieceAtPosition({PieceType::Queen, PieceColor::Black}, {3, 7});
  SetPieceAtPosition({PieceType::King, PieceColor::Black}, {4, 7});
  SetPieceAtPosition({PieceType::Bishop, PieceColor::Black}, {5, 7});
  SetPieceAtPosition({PieceType::Knight, PieceColor::Black}, {6, 7});
  SetPieceAtPosition({PieceType::Rook, PieceColor::Black}, {7, 7});
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

  if(pieceFrom.piece==PieceType::WRONG) {
    std::cout << "Wrong FROM piece at position: " << from.to_string() << endl;
    return;
  }

  if(pieceFrom.color != turn) {
    std::cout << "Piece color does not match the turn at position: " << from.to_string() << endl;
    return;
  }

  if(pieceTo.piece == PieceType::NONE || (pieceFrom.color != pieceTo.color)){
    this->SetPieceAtPosition(pieceFrom, to);
    this->SetPieceAtPosition({PieceType::NONE, PieceColor::NONE}, from);
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
