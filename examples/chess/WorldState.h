#ifndef CHESS_WORLDSTATE_H
#define CHESS_WORLDSTATE_H

#include "Point2D.h"
#include <vector>
#include <unordered_set>
using namespace std;

enum class PieceType: uint8_t {
  NONE =      0b0000, // debug purposes
  King =      0b0001,
  Queen =     0b0010,
  Bishop =    0b0011,
  Knight =    0b0100,
  Rook  =     0b0101,
  Pawn =      0b0110,
  WRONG     = 0b0111, // debug purposes
  PIECEMASK = 0b0111
};

enum class PieceColor: uint8_t { // chess color to avoid colliding with namespace from engine
  Black =     0b0000,
  White =     0b1000,
  COLORMASK = 0b1000,
  NONE =      0b0000
};

struct PieceData;

struct WorldState {
 private:
  PieceColor turn = PieceColor::White;
  uint16_t score; // to store evaluation heuristics to be used in AI branch and cut algorithms
//  vector<PieceState> board;
  uint8_t state[32];

 public:
  PieceColor GetTurn(){return turn;};
  void EndTurn(){turn = (PieceColor) ((uint8_t)PieceColor::COLORMASK^(uint8_t)turn);};
  PieceData PieceAtPosition(Point2D pos);
  void SetPieceAtPosition(PieceData piece, Point2D pos);
  void Reset();
  string toString();
};


class PieceData {
 public:
  PieceData():color(PieceColor::NONE), piece(PieceType::NONE){};
  PieceData(PieceType type, PieceColor color): color(color), piece(type){}
  
  PieceType piece;
  PieceColor color;

  static inline PieceData Empty() {return {PieceType::NONE, PieceColor::NONE};}
  static inline uint8_t   Pack(PieceData piece){return (uint8_t)piece.piece | (uint8_t)piece.color;};
  inline uint8_t Pack(){return (uint8_t)piece | (uint8_t)color;};
  static inline PieceData UnPack(uint8_t data){return {(PieceType)(data & (uint8_t)PieceType::PIECEMASK), (PieceColor)(data & (uint8_t)PieceColor::COLORMASK)};};
  char toChar();
};

struct PieceBehavior {
 public:
  // ListPossibleMoves to be used in the interface to draw the possible moves
  virtual vector<Point2D> PossibleMoves(WorldState* world, const Point2D& origin)=0;
  // GenerateStates to be used when we explore
  virtual vector<WorldState> GenerateStates(WorldState* world, const Point2D& origin)=0;
};

#endif  // CHESS_WORLDSTATE_H
