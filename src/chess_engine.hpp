#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace chess
{
  enum class Color
  {
    White,
    Black,
  };

  enum class PieceType
  {
    Pawn,
    Rook,
    Knight,
    Bishop,
    Queen,
    King,
  };

  struct Piece
  {
    Color color;
    PieceType type;
  };

  struct Position
  {
    int x;
    int y;
  };

  struct CastlingRights
  {
    bool kingSide = true;
    bool queenSide = true;
  };

  struct Move
  {
    int fromX = 0;
    int fromY = 0;
    int toX = 0;
    int toY = 0;
    std::string promotion = "queen";
    std::string castle;
    bool enPassant = false;
  };

  using BoardCell = std::optional<Piece>;
  using Board = std::array<std::array<BoardCell, 8>, 8>;

  struct State
  {
    Board board{};
    Color turn = Color::White;
    std::array<CastlingRights, 2> castling{};
    std::optional<Position> enPassant;
    std::optional<Move> lastMove;
    bool check = false;
    bool draw = false;
    std::optional<Color> winner;
    std::string status = "active";
    int moveCount = 1;
  };

  State createInitialState();
  State applyMove(const State &state, const Move &move);
  std::vector<Move> legalMovesForPiece(const State &state, int x, int y);
  std::vector<Move> legalMovesForColor(const State &state, Color color);

  std::string colorToString(Color color);
  std::string pieceTypeToString(PieceType type);
  std::string stateToJson(const State &state);
  std::string movesToJson(const std::vector<Move> &moves);
  std::string moveToJson(const Move &move);
}