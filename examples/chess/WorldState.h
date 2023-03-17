#ifndef CHESS_WORLDSTATE_H
#define CHESS_WORLDSTATE_H

#include "Point2D.h"
#include <vector>
#include <unordered_set>
#include "WorldStateFwd.h"
using namespace std;

// todo: make it match the PieceType and optimize
enum class MoveType : uint8_t {
  Normal = 0b000,
  Capture = 0b001,
  EnPassant = 0b010,
  Castling = 0b011,
  PromoteToBishop = 0b100,
  PromoteToKnight = 0b101,
  PromoteToRook = 0b110,
  PromoteToQueen = 0b111,
};

enum class PieceType : uint8_t {
  NONE = 0b0000,  // debug purposes
  King = 0b0001,
  Queen = 0b0010,
  Bishop = 0b0011,
  Knight = 0b0100,
  Rook = 0b0101,
  Pawn = 0b0110,
  WRONG = 0b0111,  // debug purposes
  PIECEMASK = 0b0111
};

enum class PieceColor : bool {  // chess color to avoid colliding with namespace from engine
  Black = false,
  White = true,
};

struct PieceData {
public:
  PieceData() : color(PieceColor::White), piece(PieceType::NONE){};
  PieceData(PieceColor color, PieceType type) : color(color), piece(type) {}

private:
  PieceColor color : 1;  // 0b0001
  PieceType piece : 3;   // 0b1110
  uint8_t empty : 4 = 0;

public:
  inline PieceColor Color() { return color; };
  inline PieceType Piece() { return piece; };

  static inline PieceData Empty() { return {PieceColor::White, PieceType::NONE}; }
  static inline uint8_t Pack(PieceData piece) { return *(uint8_t*)(&piece); };  // unsafe but fast
  inline uint8_t Pack() { return *(uint8_t*)(this); };
  static inline PieceData UnPack(uint8_t data) { return *(PieceData*)(&data); };
  char toChar();
  bool operator==(const PieceData& rhs) const;
  static inline PieceData Wrong() { return {PieceColor::White, PieceType::WRONG}; };
};

struct Move {
private:
  uint8_t from_x : 3;
  uint8_t from_y : 3;
  uint8_t to_x : 3;
  uint8_t to_y : 3;
  PieceColor color : 1;
  PieceType piece : 3;
  MoveType move : 3;

public:
  Move() = default;
  //  explicit Move(uint16_t data) : data(data) {}
  Move(Point2D from, Point2D to, PieceData piece, MoveType move)
      : from_x(from.x), from_y(from.y), to_x(to.x), to_y(to.y), color(piece.Color()), piece(piece.Piece()), move(move) {}
  Point2D From() { return {from_x, from_y}; }
  Point2D To() { return {to_x, to_y}; }
  PieceColor Color() { return color; }
  PieceType Piece() { return piece; }
  PieceData Piecedata() { return {color, piece}; }
  MoveType Movetype() { return move; }

  static vector<Move> GenerateListOfMoves(PieceData piece, Point2D from, unordered_set<Point2D> to);
};

struct WorldState {
private:
  PieceColor turn = PieceColor::White;
  uint16_t score;  // to store evaluation heuristics to be used in AI branch and cut algorithms
                   //  vector<PieceState> board;
  uint8_t state[32];

public:
  PieceColor GetTurn() { return turn; };
  void EndTurn() { turn = (PieceColor)(!((bool)turn)); };
  auto PieceAtPosition(Point2D pos) -> PieceData;
  void SetPieceAtPosition(PieceData piece, Point2D pos);
  void Move(Point2D from, Point2D to);
  void Reset();
  string toString();
};

struct PieceBehavior {
public:
  // ListPossibleMoves to be used in the interface to draw the possible moves
  virtual vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin) = 0;
  // GenerateStates to be used when we explore
  virtual vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin) = 0;
};

#endif  // CHESS_WORLDSTATE_H
