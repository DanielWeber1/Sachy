#include "room_server.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace std;
namespace fs = filesystem;

namespace
{
  bool parseIntStrict(const string &value, int &output)
  {
    try
    {
      size_t processed = 0;
      const int parsed = stoi(value, &processed);
      if (processed != value.size())
      {
        return false;
      }

      output = parsed;
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  string trim(const string &value)
  {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == string::npos)
    {
      return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
  }

  string toLower(string value)
  {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
      return static_cast<char>(tolower(ch));
    });
    return value;
  }

  int pieceValue(chess::PieceType type)
  {
    switch (type)
    {
      case chess::PieceType::Pawn: return 100;
      case chess::PieceType::Knight: return 320;
      case chess::PieceType::Bishop: return 330;
      case chess::PieceType::Rook: return 500;
      case chess::PieceType::Queen: return 900;
      case chess::PieceType::King: return 20000;
    }

    return 0;
  }

  int evaluateForBlack(const chess::State &state)
  {
    if (state.winner)
    {
      return *state.winner == chess::Color::Black ? 1000000 : -1000000;
    }

    if (state.draw)
    {
      return 0;
    }

    int score = 0;
    for (int y = 0; y < 8; ++y)
    {
      for (int x = 0; x < 8; ++x)
      {
        const auto &cell = state.board[y][x];
        if (!cell)
        {
          continue;
        }

        const int value = pieceValue(cell->type);
        score += (cell->color == chess::Color::Black ? value : -value);
      }
    }

    return score;
  }

  optional<chess::Move> chooseBotMove(const chess::State &state)
  {
    const vector<chess::Move> moves = chess::legalMovesForColor(state, chess::Color::Black);
    if (moves.empty())
    {
      return nullopt;
    }

    static random_device device;
    static mt19937 rng(device());

    int bestScore = numeric_limits<int>::min();
    vector<chess::Move> bestMoves;

    for (const auto &move : moves)
    {
      try
      {
        const chess::State next = chess::applyMove(state, move);
        const int score = evaluateForBlack(next);

        if (score > bestScore)
        {
          bestScore = score;
          bestMoves.clear();
          bestMoves.push_back(move);
        }
        else if (score == bestScore)
        {
          bestMoves.push_back(move);
        }
      }
      catch (...)
      {
      }
    }

    if (bestMoves.empty())
    {
      return nullopt;
    }

    uniform_int_distribution<size_t> pick(0, bestMoves.size() - 1);
    return bestMoves[pick(rng)];
  }
}

RoomServer::RoomServer(unsigned short port) : port_(port), state_(chess::createInitialState())
{
  if (listener_.listen(port_) != sf::Socket::Done)
  {
    throw runtime_error("Unable to listen on port " + to_string(port_));
  }
}

void RoomServer::resetGame()
{
  lock_guard<mutex> guard(gameMutex_);
  state_ = chess::createInitialState();
}

bool RoomServer::applyBotMove()
{
  lock_guard<mutex> guard(gameMutex_);

  if (state_.winner || state_.draw || state_.turn != chess::Color::Black)
  {
    return false;
  }

  const optional<chess::Move> move = chooseBotMove(state_);
  if (!move)
  {
    return false;
  }

  state_ = chess::applyMove(state_, *move);
  return true;
}

string RoomServer::urlDecode(const string &value)
{
  string result;
  result.reserve(value.size());

  for (size_t i = 0; i < value.size(); ++i)
  {
    const char ch = value[i];
    if (ch == '+')
    {
      result.push_back(' ');
    }
    else if (ch == '%' && i + 2 < value.size())
    {
      const string hex = value.substr(i + 1, 2);
      const char decoded = static_cast<char>(strtol(hex.c_str(), nullptr, 16));
      result.push_back(decoded);
      i += 2;
    }
    else
    {
      result.push_back(ch);
    }
  }

  return result;
}

vector<pair<string, string>> RoomServer::parseParams(const string &value)
{
  vector<pair<string, string>> params;
  size_t start = 0;

  while (start <= value.size())
  {
    const size_t end = value.find('&', start);
    const string part = value.substr(start, end == string::npos ? string::npos : end - start);
    const size_t equals = part.find('=');
    if (equals == string::npos)
    {
      params.emplace_back(urlDecode(part), string());
    }
    else
    {
      params.emplace_back(urlDecode(part.substr(0, equals)), urlDecode(part.substr(equals + 1)));
    }

    if (end == string::npos)
    {
      break;
    }

    start = end + 1;
  }

  return params;
}

string RoomServer::getParam(const vector<pair<string, string>> &params, const string &key)
{
  const auto it = find_if(params.begin(), params.end(), [&](const auto &item)
  {
    return item.first == key;
  });

  return it == params.end() ? string() : it->second;
}

string RoomServer::getHeader(const HttpRequest &request, const string &key)
{
  const string needle = toLower(key);
  const auto it = find_if(request.headers.begin(), request.headers.end(), [&](const auto &item)
  {
    return toLower(item.first) == needle;
  });

  return it == request.headers.end() ? string() : it->second;
}

string RoomServer::statusLine(int code)
{
  switch (code)
  {
    case 200: return "HTTP/1.1 200 OK";
    case 204: return "HTTP/1.1 204 No Content";
    case 400: return "HTTP/1.1 400 Bad Request";
    case 404: return "HTTP/1.1 404 Not Found";
    default: return "HTTP/1.1 500 Internal Server Error";
  }
}

string RoomServer::mimeTypeFor(const fs::path &path)
{
  const string extension = toLower(path.extension().string());
  if (extension == ".html") return "text/html; charset=utf-8";
  if (extension == ".css") return "text/css; charset=utf-8";
  if (extension == ".js") return "application/javascript; charset=utf-8";
  if (extension == ".png") return "image/png";
  if (extension == ".ico") return "image/x-icon";
  if (extension == ".svg") return "image/svg+xml";
  return "application/octet-stream";
}

string RoomServer::jsonEscape(const string &value)
{
  ostringstream out;
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

string RoomServer::pageHtml()
{
  ostringstream out;
  out << "<!doctype html>"
      << "<html lang=\"en\">"
      << "<head>"
      << "<meta charset=\"utf-8\" />"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />"
      << "<title>Sachy vs Bot</title>"
      << "<link rel=\"stylesheet\" href=\"/public/style.css\" />"
      << "</head>"
      << "<body>"
      << "<main class=\"app-shell\">"
      << "<section class=\"status-bar\">"
      << "<div><div class=\"eyebrow\">Online chess</div><h1>Sachy vs Bot</h1></div>"
      << "<div style=\"display:flex;gap:0.5rem;align-items:center;\">"
      << "<button id=\"newGameButton\" type=\"button\">New game</button>"
      << "<div id=\"statusText\" class=\"status-text\">Connecting...</div>"
      << "</div>"
      << "</section>"
      << "<section class=\"board-wrap\"><div id=\"board\" class=\"board\" aria-label=\"Chess board\"></div></section>"
      << "<section id=\"announcement\" class=\"announcement\" aria-live=\"polite\" aria-atomic=\"true\" hidden>"
      << "<div class=\"announcement-card\">"
      << "<div class=\"announcement-kicker\">Game over</div>"
      << "<h2 id=\"announcementTitle\">Checkmate</h2>"
      << "<p id=\"announcementDetail\" class=\"announcement-detail\"></p>"
      << "</div></section>"
      << "</main>"
      << "<script type=\"module\" src=\"/public/client.js\"></script>"
      << "</body></html>";
  return out.str();
}

string RoomServer::statusTextFor(const chess::State &state)
{
  if (state.winner)
  {
    return chess::colorToString(*state.winner) + " wins by checkmate";
  }

  if (state.draw)
  {
    return "Draw by stalemate";
  }

  if (state.check)
  {
    return chess::colorToString(state.turn) + " is in check";
  }

  return chess::colorToString(state.turn) + " to move";
}

string RoomServer::stateEnvelopeJson(const chess::State &state)
{
  ostringstream out;
  out << "{"
      << "\"playerColor\":\"white\"," 
      << "\"state\":" << chess::stateToJson(state) << ","
      << "\"status\":\"" << jsonEscape(statusTextFor(state)) << "\""
      << "}";
  return out.str();
}

string RoomServer::movesResponseJson(const vector<chess::Move> &moves)
{
  return chess::movesToJson(moves);
}

bool RoomServer::startsWith(const string &value, const string &prefix)
{
  return value.rfind(prefix, 0) == 0;
}

string RoomServer::readFileText(const fs::path &path, bool binary)
{
  ifstream file(path, binary ? ios::binary : ios::in);
  if (!file)
  {
    return {};
  }

  ostringstream out;
  out << file.rdbuf();
  return out.str();
}

bool RoomServer::writeAll(sf::TcpSocket &socket, const string &data)
{
  size_t sent = 0;
  while (sent < data.size())
  {
    size_t current = 0;
    const sf::Socket::Status status = socket.send(data.data() + sent, data.size() - sent, current);
    if (status != sf::Socket::Done && status != sf::Socket::Partial)
    {
      return false;
    }

    sent += current;
    if (current == 0)
    {
      break;
    }
  }

  return sent == data.size();
}

string RoomServer::findAssetPath(const string &requestPath)
{
  if (startsWith(requestPath, "/public/"))
  {
    const fs::path candidate = fs::path("public") / requestPath.substr(string("/public/").size());
    if (fs::exists(candidate))
    {
      return candidate.string();
    }
  }

  if (startsWith(requestPath, "/assets/"))
  {
    const fs::path candidate = fs::path("src") / "assets" / requestPath.substr(string("/assets/").size());
    if (fs::exists(candidate))
    {
      return candidate.string();
    }
  }

  return {};
}

RoomServer::HttpRequest RoomServer::readRequest(sf::TcpSocket &socket)
{
  string raw;
  char buffer[4096];
  size_t received = 0;
  size_t headerEnd = string::npos;

  while (headerEnd == string::npos)
  {
    const sf::Socket::Status status = socket.receive(buffer, sizeof(buffer), received);
    if (status != sf::Socket::Done && status != sf::Socket::Partial)
    {
      break;
    }

    if (received == 0)
    {
      break;
    }

    raw.append(buffer, received);
    headerEnd = raw.find("\r\n\r\n");
  }

  if (headerEnd == string::npos)
  {
    return {};
  }

  const string headerPart = raw.substr(0, headerEnd);
  string body = raw.substr(headerEnd + 4);

  HttpRequest request;
  istringstream lines(headerPart);
  string line;
  if (!getline(lines, line))
  {
    return {};
  }

  if (!line.empty() && line.back() == '\r')
  {
    line.pop_back();
  }

  {
    istringstream requestLine(line);
    requestLine >> request.method >> request.target >> request.version;
  }

  while (getline(lines, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }

    const size_t colon = line.find(':');
    if (colon == string::npos)
    {
      continue;
    }

    request.headers.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
  }

  const string contentLengthValue = getHeader(request, "Content-Length");
  const size_t contentLength = contentLengthValue.empty() ? 0 : static_cast<size_t>(stoul(contentLengthValue));

  while (body.size() < contentLength)
  {
    const sf::Socket::Status status = socket.receive(buffer, sizeof(buffer), received);
    if (status != sf::Socket::Done && status != sf::Socket::Partial)
    {
      break;
    }

    if (received == 0)
    {
      break;
    }

    body.append(buffer, received);
  }

  request.body = body.substr(0, contentLength);

  const size_t queryPos = request.target.find('?');
  if (queryPos == string::npos)
  {
    request.path = request.target;
  }
  else
  {
    request.path = request.target.substr(0, queryPos);
    request.query = request.target.substr(queryPos + 1);
  }

  return request;
}

bool RoomServer::sendResponse(sf::TcpSocket &socket, const string &response)
{
  return writeAll(socket, response);
}

void RoomServer::handleConnection(unique_ptr<sf::TcpSocket> socket)
{
  socket->setBlocking(true);
  const HttpRequest request = readRequest(*socket);
  if (request.method.empty())
  {
    return;
  }

  if (request.method == "GET" && request.path == "/")
  {
    const string html = pageHtml();
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: text/html; charset=utf-8\r\n"
             << "Content-Length: " << html.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << html;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "GET" && request.path == "/public/client.js")
  {
    const string text = readFileText("public/client.js");
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/javascript; charset=utf-8\r\n"
             << "Content-Length: " << text.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << text;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "GET" && request.path == "/public/style.css")
  {
    const string text = readFileText("public/style.css");
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: text/css; charset=utf-8\r\n"
             << "Content-Length: " << text.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << text;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "GET" && startsWith(request.path, "/assets/"))
  {
    const string assetPath = findAssetPath(request.path);
    if (assetPath.empty())
    {
      const string body = "Not found";
      ostringstream response;
      response << statusLine(404) << "\r\n"
               << "Content-Type: text/plain; charset=utf-8\r\n"
               << "Content-Length: " << body.size() << "\r\n"
               << "Connection: close\r\n\r\n"
               << body;
      sendResponse(*socket, response.str());
      return;
    }

    const string binary = readFileText(assetPath, true);
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: " << mimeTypeFor(assetPath) << "\r\n"
             << "Content-Length: " << binary.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << binary;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "GET" && request.path == "/api/game/state")
  {
    string body;
    {
      lock_guard<mutex> guard(gameMutex_);
      body = stateEnvelopeJson(state_);
    }

    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "GET" && request.path == "/api/game/moves")
  {
    const auto params = parseParams(request.query);
    int x = 0;
    int y = 0;

    if (!parseIntStrict(getParam(params, "x"), x) || !parseIntStrict(getParam(params, "y"), y))
    {
      const string body = "{\"error\":\"Invalid square.\"}";
      ostringstream response;
      response << statusLine(400) << "\r\n"
               << "Content-Type: application/json; charset=utf-8\r\n"
               << "Content-Length: " << body.size() << "\r\n"
               << "Connection: close\r\n\r\n"
               << body;
      sendResponse(*socket, response.str());
      return;
    }

    vector<chess::Move> moves;
    {
      lock_guard<mutex> guard(gameMutex_);
      if (!state_.winner && !state_.draw && state_.turn == chess::Color::White)
      {
        const auto &piece = state_.board[y][x];
        if (piece && piece->color == chess::Color::White)
        {
          moves = chess::legalMovesForPiece(state_, x, y);
        }
      }
    }

    const string body = movesResponseJson(moves);
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "POST" && request.path == "/api/game/new")
  {
    resetGame();

    string body;
    {
      lock_guard<mutex> guard(gameMutex_);
      body = stateEnvelopeJson(state_);
    }

    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "POST" && request.path == "/api/game/move")
  {
    const auto params = parseParams(request.body);
    chess::Move move;

    if (!parseIntStrict(getParam(params, "fromX"), move.fromX) ||
        !parseIntStrict(getParam(params, "fromY"), move.fromY) ||
        !parseIntStrict(getParam(params, "toX"), move.toX) ||
        !parseIntStrict(getParam(params, "toY"), move.toY))
    {
      const string body = "{\"error\":\"Invalid move parameters.\"}";
      ostringstream response;
      response << statusLine(400) << "\r\n"
               << "Content-Type: application/json; charset=utf-8\r\n"
               << "Content-Length: " << body.size() << "\r\n"
               << "Connection: close\r\n\r\n"
               << body;
      sendResponse(*socket, response.str());
      return;
    }

    move.promotion = getParam(params, "promotion").empty() ? string("queen") : getParam(params, "promotion");
    move.castle = getParam(params, "castle");
    move.enPassant = getParam(params, "enPassant") == "true";

    {
      lock_guard<mutex> guard(gameMutex_);
      if (state_.winner || state_.draw)
      {
        const string body = "{\"error\":\"Game is already over.\"}";
        ostringstream response;
        response << statusLine(400) << "\r\n"
                 << "Content-Type: application/json; charset=utf-8\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        sendResponse(*socket, response.str());
        return;
      }

      if (state_.turn != chess::Color::White)
      {
        const string body = "{\"error\":\"Wait for bot move.\"}";
        ostringstream response;
        response << statusLine(400) << "\r\n"
                 << "Content-Type: application/json; charset=utf-8\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        sendResponse(*socket, response.str());
        return;
      }

      try
      {
        state_ = chess::applyMove(state_, move);
      }
      catch (const exception &error)
      {
        const string body = string("{\"error\":\"") + jsonEscape(error.what()) + "\"}";
        ostringstream response;
        response << statusLine(400) << "\r\n"
                 << "Content-Type: application/json; charset=utf-8\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        sendResponse(*socket, response.str());
        return;
      }
    }

    string body;
    {
      lock_guard<mutex> guard(gameMutex_);
      body = string("{\"ok\":true,\"state\":") + chess::stateToJson(state_) + "}";
    }

    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "POST" && request.path == "/api/game/bot-move")
  {
    applyBotMove();

    string body;
    {
      lock_guard<mutex> guard(gameMutex_);
      body = string("{\"ok\":true,\"state\":") + chess::stateToJson(state_) + "}";
    }

    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "GET" && request.path == "/health")
  {
    const string body = "{\"ok\":true,\"mode\":\"singleplayer\"}";
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  const string body = "Not found";
  ostringstream response;
  response << statusLine(404) << "\r\n"
           << "Content-Type: text/plain; charset=utf-8\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
  sendResponse(*socket, response.str());
}

void RoomServer::run()
{
  cout << "Sachy server running on http://localhost:" << port_ << endl;

  while (running_)
  {
    auto socket = make_unique<sf::TcpSocket>();
    if (listener_.accept(*socket) != sf::Socket::Done)
    {
      continue;
    }

    thread(&RoomServer::handleConnection, this, move(socket)).detach();
  }
}
