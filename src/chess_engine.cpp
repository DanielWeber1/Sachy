#include "chess_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace chess
{
  namespace
  {
    int colorIndex(Color color)
    {
      return color == Color::White ? 0 : 1;
    }

    Color oppositeColor(Color color)
    {
      return color == Color::White ? Color::Black : Color::White;
    }

    bool inBounds(int x, int y)
    {
      return x >= 0 && x < 8 && y >= 0 && y < 8;
    }

    Piece makePiece(Color color, PieceType type)
    {
      return Piece{color, type};
    }

    Board emptyBoard()
    {
      Board board{};
      for (auto &row : board)
      {
        row.fill(std::nullopt);
      }
      return board;
    }

    std::string jsonEscape(const std::string &value)
    {
      std::ostringstream out;
      for (char ch : value)
      {
        switch (ch)
        {
          case '"': out << "\\\""; break;
          case '\\': out << "\\\\"; break;
          case '\b': out << "\\b"; break;
          case '\f': out << "\\f"; break;
          case '\n': out << "\\n"; break;
          case '\r': out << "\\r"; break;
          case '\t': out << "\\t"; break;
          default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
              constexpr char hex[] = "0123456789abcdef";
              out << "\\u00" << hex[(static_cast<unsigned char>(ch) >> 4) & 0x0F] << hex[static_cast<unsigned char>(ch) & 0x0F];
            }
            else
            {
              out << ch;
            }
            break;
        }
      }
      return out.str();
    }

    std::string optionalStringJson(const std::string &value)
    {
      if (value.empty())
      {
        return "null";
      }

      return std::string("\"") + jsonEscape(value) + "\"";
    }

    std::string pieceJson(const BoardCell &piece)
    {
      if (!piece)
      {
        return "null";
      }

      std::ostringstream out;
      out << "{\"color\":\"" << colorToString(piece->color)
          << "\",\"type\":\"" << pieceTypeToString(piece->type) << "\"}";
      return out.str();
    }

    std::string positionJson(const std::optional<Position> &position)
    {
      if (!position)
      {
        return "null";
      }

      std::ostringstream out;
      out << "{\"x\":" << position->x << ",\"y\":" << position->y << "}";
      return out.str();
    }

    std::string boardJson(const Board &board)
    {
      std::ostringstream out;
      out << "[";
      for (int y = 0; y < 8; ++y)
      {
        if (y > 0)
        {
          out << ",";
        }

        out << "[";
        for (int x = 0; x < 8; ++x)
        {
          if (x > 0)
          {
            out << ",";
          }

          out << pieceJson(board[y][x]);
        }
        out << "]";
      }
      out << "]";
      return out.str();
    }

    std::string castlingJson(const std::array<CastlingRights, 2> &castling)
    {
      std::ostringstream out;
      out << "{"
          << "\"white\":{\"kingSide\":" << (castling[colorIndex(Color::White)].kingSide ? "true" : "false")
          << ",\"queenSide\":" << (castling[colorIndex(Color::White)].queenSide ? "true" : "false") << "},"
          << "\"black\":{\"kingSide\":" << (castling[colorIndex(Color::Black)].kingSide ? "true" : "false")
          << ",\"queenSide\":" << (castling[colorIndex(Color::Black)].queenSide ? "true" : "false") << "}"
          << "}";
      return out.str();
    }

    std::string moveJson(const Move &move)
    {
      std::ostringstream out;
      out << "{"
          << "\"fromX\":" << move.fromX << ","
          << "\"fromY\":" << move.fromY << ","
          << "\"toX\":" << move.toX << ","
          << "\"toY\":" << move.toY << ","
          << "\"promotion\":\"" << jsonEscape(move.promotion.empty() ? std::string("queen") : move.promotion) << "\","
          << "\"castle\":" << optionalStringJson(move.castle) << ","
          << "\"enPassant\":" << (move.enPassant ? "true" : "false")
          << "}";
      return out.str();
    }

    std::optional<Position> findKing(const Board &board, Color color)
    {
      for (int y = 0; y < 8; ++y)
      {
        for (int x = 0; x < 8; ++x)
        {
          const BoardCell &current = board[y][x];
          if (current && current->color == color && current->type == PieceType::King)
          {
            return Position{x, y};
          }
        }
      }

      return std::nullopt;
    }

    bool isSquareAttacked(const Board &board, int x, int y, Color byColor)
    {
      const int pawnDirection = byColor == Color::White ? -1 : 1;
      const int pawnRow = y - pawnDirection;

      for (int deltaX : {-1, 1})
      {
        const int pawnX = x + deltaX;
        if (inBounds(pawnX, pawnRow))
        {
          const BoardCell &attacker = board[pawnRow][pawnX];
          if (attacker && attacker->color == byColor && attacker->type == PieceType::Pawn)
          {
            return true;
          }
        }
      }

      constexpr std::array<std::pair<int, int>, 8> knightOffsets = {{{-2, -1}, {-1, -2}, {1, -2}, {2, -1}, {2, 1}, {1, 2}, {-1, 2}, {-2, 1}}};
      for (const auto &[dx, dy] : knightOffsets)
      {
        const int knightX = x + dx;
        const int knightY = y + dy;
        if (!inBounds(knightX, knightY))
        {
          continue;
        }

        const BoardCell &attacker = board[knightY][knightX];
        if (attacker && attacker->color == byColor && attacker->type == PieceType::Knight)
        {
          return true;
        }
      }

      const std::array<std::tuple<int, int, std::array<PieceType, 2>>, 8> lineChecks = {{
        std::make_tuple(1, 0, std::array<PieceType, 2>{PieceType::Rook, PieceType::Queen}),
        std::make_tuple(-1, 0, std::array<PieceType, 2>{PieceType::Rook, PieceType::Queen}),
        std::make_tuple(0, 1, std::array<PieceType, 2>{PieceType::Rook, PieceType::Queen}),
        std::make_tuple(0, -1, std::array<PieceType, 2>{PieceType::Rook, PieceType::Queen}),
        std::make_tuple(1, 1, std::array<PieceType, 2>{PieceType::Bishop, PieceType::Queen}),
        std::make_tuple(1, -1, std::array<PieceType, 2>{PieceType::Bishop, PieceType::Queen}),
        std::make_tuple(-1, 1, std::array<PieceType, 2>{PieceType::Bishop, PieceType::Queen}),
        std::make_tuple(-1, -1, std::array<PieceType, 2>{PieceType::Bishop, PieceType::Queen}),
      }};

      for (const auto &[dx, dy, types] : lineChecks)
      {
        int currentX = x + dx;
        int currentY = y + dy;

        while (inBounds(currentX, currentY))
        {
          const BoardCell &attacker = board[currentY][currentX];
          if (attacker)
          {
            if (attacker->color == byColor && (attacker->type == types[0] || attacker->type == types[1]))
            {
              return true;
            }

            break;
          }

          currentX += dx;
          currentY += dy;
        }
      }

      for (int dy = -1; dy <= 1; ++dy)
      {
        for (int dx = -1; dx <= 1; ++dx)
        {
          if (dx == 0 && dy == 0)
          {
            continue;
          }

          const int kingX = x + dx;
          const int kingY = y + dy;
          if (!inBounds(kingX, kingY))
          {
            continue;
          }

          const BoardCell &attacker = board[kingY][kingX];
          if (attacker && attacker->color == byColor && attacker->type == PieceType::King)
          {
            return true;
          }
        }
      }

      return false;
    }

    bool isKingInCheck(const Board &board, Color color)
    {
      const auto kingPosition = findKing(board, color);
      if (!kingPosition)
      {
        return true;
      }

      return isSquareAttacked(board, kingPosition->x, kingPosition->y, oppositeColor(color));
    }

    State makeUncheckedMove(const State &state, const Move &move)
    {
      State nextState = state;
      const BoardCell &pieceValue = nextState.board[move.fromY][move.fromX];

      if (!pieceValue)
      {
        throw std::runtime_error("No piece selected.");
      }

      const BoardCell target = nextState.board[move.toY][move.toX];
      nextState.enPassant = std::nullopt;

      if (!move.castle.empty())
      {
        const int row = move.fromY;
        const bool kingSide = move.castle == "king";
        const int rookFromX = kingSide ? 7 : 0;
        const int rookToX = kingSide ? 5 : 3;

        nextState.board[move.fromY][move.fromX] = std::nullopt;
        nextState.board[move.toY][move.toX] = *pieceValue;
        nextState.board[row][rookFromX] = std::nullopt;
        nextState.board[row][rookToX] = target;

        nextState.castling[colorIndex(pieceValue->color)].kingSide = false;
        nextState.castling[colorIndex(pieceValue->color)].queenSide = false;
      }
      else
      {
        nextState.board[move.fromY][move.fromX] = std::nullopt;

        if (move.enPassant)
        {
          const int direction = pieceValue->color == Color::White ? 1 : -1;
          nextState.board[move.toY + direction][move.toX] = std::nullopt;
        }

        nextState.board[move.toY][move.toX] = *pieceValue;

        if (pieceValue->type == PieceType::Pawn && std::abs(move.toY - move.fromY) == 2)
        {
          nextState.enPassant = Position{move.fromX, (move.fromY + move.toY) / 2};
        }

        if (pieceValue->type == PieceType::King)
        {
          nextState.castling[colorIndex(pieceValue->color)].kingSide = false;
          nextState.castling[colorIndex(pieceValue->color)].queenSide = false;
        }

        if (pieceValue->type == PieceType::Rook)
        {
          if (pieceValue->color == Color::White && move.fromY == 7 && move.fromX == 0)
          {
            nextState.castling[colorIndex(Color::White)].queenSide = false;
          }

          if (pieceValue->color == Color::White && move.fromY == 7 && move.fromX == 7)
          {
            nextState.castling[colorIndex(Color::White)].kingSide = false;
          }

          if (pieceValue->color == Color::Black && move.fromY == 0 && move.fromX == 0)
          {
            nextState.castling[colorIndex(Color::Black)].queenSide = false;
          }

          if (pieceValue->color == Color::Black && move.fromY == 0 && move.fromX == 7)
          {
            nextState.castling[colorIndex(Color::Black)].kingSide = false;
          }
        }

        if (target && target->type == PieceType::Rook)
        {
          if (target->color == Color::White && move.toY == 7 && move.toX == 0)
          {
            nextState.castling[colorIndex(Color::White)].queenSide = false;
          }

          if (target->color == Color::White && move.toY == 7 && move.toX == 7)
          {
            nextState.castling[colorIndex(Color::White)].kingSide = false;
          }

          if (target->color == Color::Black && move.toY == 0 && move.toX == 0)
          {
            nextState.castling[colorIndex(Color::Black)].queenSide = false;
          }

          if (target->color == Color::Black && move.toY == 0 && move.toX == 7)
          {
            nextState.castling[colorIndex(Color::Black)].kingSide = false;
          }
        }

        if (pieceValue->type == PieceType::Pawn && (move.toY == 0 || move.toY == 7))
        {
          nextState.board[move.toY][move.toX] = makePiece(pieceValue->color, PieceType::Queen);
        }
      }

      nextState.turn = oppositeColor(state.turn);
      nextState.lastMove = move;
      nextState.moveCount = state.moveCount + 1;

      return nextState;
    }

    std::vector<Move> generatePseudoMoves(const State &state, int x, int y)
    {
      const BoardCell &pieceValue = state.board[y][x];
      if (!pieceValue)
      {
        return {};
      }

      std::vector<Move> moves;
      const int direction = pieceValue->color == Color::White ? -1 : 1;
      const int startRow = pieceValue->color == Color::White ? 6 : 1;
      const int promotionRow = pieceValue->color == Color::White ? 0 : 7;

      if (pieceValue->type == PieceType::Pawn)
      {
        const int oneStepY = y + direction;
        if (inBounds(x, oneStepY) && !state.board[oneStepY][x])
        {
          moves.push_back(Move{ x, y, x, oneStepY });

          const int twoStepY = y + direction * 2;
          if (y == startRow && inBounds(x, twoStepY) && !state.board[twoStepY][x])
          {
            moves.push_back(Move{ x, y, x, twoStepY });
          }
        }

        for (int deltaX : {-1, 1})
        {
          const int targetX = x + deltaX;
          const int targetY = y + direction;

          if (!inBounds(targetX, targetY))
          {
            continue;
          }

          const BoardCell &target = state.board[targetY][targetX];
          if (target && target->color != pieceValue->color)
          {
            moves.push_back(Move{ x, y, targetX, targetY });
          }

          if (state.enPassant && state.enPassant->x == targetX && state.enPassant->y == targetY)
          {
            Move enPassantMove{ x, y, targetX, targetY };
            enPassantMove.enPassant = true;
            moves.push_back(enPassantMove);
          }
        }

        for (Move &move : moves)
        {
          if (move.toY == promotionRow)
          {
            move.promotion = "queen";
          }
        }

        return moves;
      }

      if (pieceValue->type == PieceType::Knight)
      {
        constexpr std::array<std::pair<int, int>, 8> offsets = {{{-2, -1}, {-1, -2}, {1, -2}, {2, -1}, {2, 1}, {1, 2}, {-1, 2}, {-2, 1}}};
        for (const auto &[dx, dy] : offsets)
        {
          const int targetX = x + dx;
          const int targetY = y + dy;
          if (!inBounds(targetX, targetY))
          {
            continue;
          }

          const BoardCell &target = state.board[targetY][targetX];
          if (!target || target->color != pieceValue->color)
          {
            moves.push_back(Move{ x, y, targetX, targetY });
          }
        }

        return moves;
      }

      std::vector<std::pair<int, int>> directions;
      if (pieceValue->type == PieceType::Bishop || pieceValue->type == PieceType::Queen)
      {
        directions.push_back({1, 1});
        directions.push_back({1, -1});
        directions.push_back({-1, 1});
        directions.push_back({-1, -1});
      }

      if (pieceValue->type == PieceType::Rook || pieceValue->type == PieceType::Queen)
      {
        directions.push_back({1, 0});
        directions.push_back({-1, 0});
        directions.push_back({0, 1});
        directions.push_back({0, -1});
      }

      if (pieceValue->type == PieceType::King)
      {
        for (int dy = -1; dy <= 1; ++dy)
        {
          for (int dx = -1; dx <= 1; ++dx)
          {
            if (dx == 0 && dy == 0)
            {
              continue;
            }

            const int targetX = x + dx;
            const int targetY = y + dy;
            if (!inBounds(targetX, targetY))
            {
              continue;
            }

            const BoardCell &target = state.board[targetY][targetX];
            if (!target || target->color != pieceValue->color)
            {
              moves.push_back(Move{ x, y, targetX, targetY });
            }
          }
        }

        const CastlingRights &rights = state.castling[colorIndex(pieceValue->color)];
        const int homeRow = pieceValue->color == Color::White ? 7 : 0;

        if (y == homeRow && x == 4 && !isKingInCheck(state.board, pieceValue->color))
        {
          const bool kingSideSafe = !state.board[homeRow][5]
            && !state.board[homeRow][6]
            && rights.kingSide
            && state.board[homeRow][7]
            && state.board[homeRow][7]->type == PieceType::Rook
            && state.board[homeRow][7]->color == pieceValue->color
            && !isSquareAttacked(state.board, 5, homeRow, oppositeColor(pieceValue->color))
            && !isSquareAttacked(state.board, 6, homeRow, oppositeColor(pieceValue->color));

          if (kingSideSafe)
          {
            Move castleMove{ x, y, 6, homeRow };
            castleMove.castle = "king";
            moves.push_back(castleMove);
          }

          const bool queenSideSafe = !state.board[homeRow][1]
            && !state.board[homeRow][2]
            && !state.board[homeRow][3]
            && rights.queenSide
            && state.board[homeRow][0]
            && state.board[homeRow][0]->type == PieceType::Rook
            && state.board[homeRow][0]->color == pieceValue->color
            && !isSquareAttacked(state.board, 3, homeRow, oppositeColor(pieceValue->color))
            && !isSquareAttacked(state.board, 2, homeRow, oppositeColor(pieceValue->color));

          if (queenSideSafe)
          {
            Move castleMove{ x, y, 2, homeRow };
            castleMove.castle = "queen";
            moves.push_back(castleMove);
          }
        }

        return moves;
      }

      for (const auto &[dx, dy] : directions)
      {
        int targetX = x + dx;
        int targetY = y + dy;

        while (inBounds(targetX, targetY))
        {
          const BoardCell &target = state.board[targetY][targetX];
          if (!target)
          {
            moves.push_back(Move{ x, y, targetX, targetY });
          }
          else
          {
            if (target->color != pieceValue->color)
            {
              moves.push_back(Move{ x, y, targetX, targetY });
            }

            break;
          }

          targetX += dx;
          targetY += dy;
        }
      }

      return moves;
    }

    bool moveEquivalent(const Move &left, const Move &right)
    {
      return left.fromX == right.fromX
        && left.fromY == right.fromY
        && left.toX == right.toX
        && left.toY == right.toY
        && left.castle == right.castle
        && left.enPassant == right.enPassant;
    }

    std::vector<Move> legalMovesForPieceInternal(const State &state, int x, int y)
    {
      const BoardCell &pieceValue = state.board[y][x];
      if (!pieceValue || pieceValue->color != state.turn || state.winner || state.draw)
      {
        return {};
      }

      const std::vector<Move> pseudoMoves = generatePseudoMoves(state, x, y);
      std::vector<Move> legalMoves;

      for (const Move &move : pseudoMoves)
      {
        const State candidate = makeUncheckedMove(state, move);
        if (!isKingInCheck(candidate.board, pieceValue->color))
        {
          legalMoves.push_back(move);
        }
      }

      return legalMoves;
    }
  }

  std::string colorToString(Color color)
  {
    return color == Color::White ? "white" : "black";
  }

  std::string pieceTypeToString(PieceType type)
  {
    switch (type)
    {
      case PieceType::Pawn: return "pawn";
      case PieceType::Rook: return "rook";
      case PieceType::Knight: return "knight";
      case PieceType::Bishop: return "bishop";
      case PieceType::Queen: return "queen";
      case PieceType::King: return "king";
    }

    return "pawn";
  }

  State createInitialState()
  {
    State state;
    state.board = emptyBoard();
    const std::array<PieceType, 8> backRank = {PieceType::Rook, PieceType::Knight, PieceType::Bishop, PieceType::Queen, PieceType::King, PieceType::Bishop, PieceType::Knight, PieceType::Rook};

    for (int x = 0; x < 8; ++x)
    {
      state.board[0][x] = makePiece(Color::Black, backRank[x]);
      state.board[1][x] = makePiece(Color::Black, PieceType::Pawn);
      state.board[6][x] = makePiece(Color::White, PieceType::Pawn);
      state.board[7][x] = makePiece(Color::White, backRank[x]);
    }

    state.turn = Color::White;
    state.castling = {CastlingRights{}, CastlingRights{}};
    state.status = "active";
    state.moveCount = 1;
    return state;
  }

  std::vector<Move> legalMovesForPiece(const State &state, int x, int y)
  {
    return legalMovesForPieceInternal(state, x, y);
  }

  std::vector<Move> legalMovesForColor(const State &state, Color color)
  {
    State filtered = state;
    filtered.turn = color;

    std::vector<Move> moves;
    for (int y = 0; y < 8; ++y)
    {
      for (int x = 0; x < 8; ++x)
      {
        const BoardCell &current = state.board[y][x];
        if (current && current->color == color)
        {
          const std::vector<Move> pieceMoves = legalMovesForPieceInternal(filtered, x, y);
          moves.insert(moves.end(), pieceMoves.begin(), pieceMoves.end());
        }
      }
    }

    return moves;
  }

  State applyMove(const State &state, const Move &move)
  {
    const int fromX = move.fromX;
    const int fromY = move.fromY;
    const int toX = move.toX;
    const int toY = move.toY;

    if (!inBounds(fromX, fromY) || !inBounds(toX, toY))
    {
      throw std::runtime_error("Move is outside the board.");
    }

    const BoardCell &selected = state.board[fromY][fromX];
    if (!selected)
    {
      throw std::runtime_error("No piece on the selected square.");
    }

    if (selected->color != state.turn)
    {
      throw std::runtime_error("That piece cannot move right now.");
    }

    const std::vector<Move> legalMoves = legalMovesForPieceInternal(state, fromX, fromY);
    const auto normalized = std::find_if(legalMoves.begin(), legalMoves.end(), [&](const Move &candidate)
    {
      return moveEquivalent(candidate, move);
    });

    if (normalized == legalMoves.end())
    {
      throw std::runtime_error("Illegal move.");
    }

    Move normalizedMove = *normalized;
    if (normalizedMove.promotion.empty())
    {
      normalizedMove.promotion = "queen";
    }

    State nextState = makeUncheckedMove(state, normalizedMove);

    nextState.check = isKingInCheck(nextState.board, nextState.turn);
    nextState.draw = false;
    nextState.winner = std::nullopt;

    const std::vector<Move> legalNextMoves = legalMovesForColor(nextState, nextState.turn);
    if (legalNextMoves.empty())
    {
      if (nextState.check)
      {
        nextState.winner = oppositeColor(nextState.turn);
        nextState.status = "checkmate";
      }
      else
      {
        nextState.draw = true;
        nextState.status = "stalemate";
      }
    }
    else
    {
      nextState.status = nextState.check ? "check" : "active";
    }

    return nextState;
  }

  std::string moveToJson(const Move &move)
  {
    return moveJson(move);
  }

  std::string movesToJson(const std::vector<Move> &moves)
  {
    std::ostringstream out;
    out << "{\"moves\":[";
    for (std::size_t i = 0; i < moves.size(); ++i)
    {
      if (i > 0)
      {
        out << ",";
      }

      out << moveJson(moves[i]);
    }
    out << "]}";
    return out.str();
  }

  std::string stateToJson(const State &state)
  {
    std::ostringstream out;
    out << "{"
        << "\"board\":" << boardJson(state.board) << ","
        << "\"turn\":\"" << colorToString(state.turn) << "\","
        << "\"castling\":" << castlingJson(state.castling) << ","
        << "\"enPassant\":" << positionJson(state.enPassant) << ","
        << "\"lastMove\":" << (state.lastMove ? moveJson(*state.lastMove) : std::string("null")) << ","
        << "\"check\":" << (state.check ? "true" : "false") << ","
        << "\"draw\":" << (state.draw ? "true" : "false") << ","
        << "\"winner\":";

    if (state.winner)
    {
      out << "\"" << colorToString(*state.winner) << "\"";
    }
    else
    {
      out << "null";
    }

    out << ",\"status\":\"" << jsonEscape(state.status) << "\","
        << "\"moveCount\":" << state.moveCount
        << "}";
    return out.str();
  }
}