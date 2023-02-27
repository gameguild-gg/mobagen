#ifndef CHESS_WORLDSTATE_H
#define CHESS_WORLDSTATE_H

#include "Point2D.h"
#include <vector>
using namespace std;

enum class PieceType: char {
  None = 0, // debug purposes
  King = 1,
  Queen = 2,
  Bishop = 3,
  Knight = 4,
  Tower = 5,
  Pawn = 6,
};

enum class Liveness: char {
  None = 0, // debug purposes
  Alive = 1,
  Dead = 2,
};

enum class CColor: char { // chess color to avoid colliding with namespace from engine
  None = 0, // debug purposes
  Black = 1,
  White = 2,
};

struct PieceState {
  PieceState(PieceType type, Liveness liveness, CColor color, Point2D position):
      type(type), color(color), position(position), liveness(liveness){}

  PieceType type;
  CColor color;
  Point2D position;
  Liveness liveness;
};

struct WorldState{
 private:
  bool isWhiteTurn; // true is white
  float score; // to store evaluation heuristics to be used in AI branch and cut algorithms
  vector<PieceState> board;

 public:
  // bool inline IsWhiteTurn(){return isWhiteTurn;}
  // get the current turn king. Pass false to get the enemy king
  PieceState* PieceAtPosition(Point2D pos);
};

#endif  // CHESS_WORLDSTATE_H
