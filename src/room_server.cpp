#include "room_server.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
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

  string nowString()
  {
    const auto now = chrono::system_clock::now();
    const auto time = chrono::system_clock::to_time_t(now);
    tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localTime = *localtime(&time);
#endif

    ostringstream out;
    out << put_time(&localTime, "%Y-%m-%dT%H:%M:%S");
    return out.str();
  }
}

RoomServer::RoomServer(unsigned short port)
  : port_(port)
{
  if (listener_.listen(port_) != sf::Socket::Done)
  {
    throw runtime_error("Unable to listen on port " + to_string(port_));
  }
}

shared_ptr<RoomServer::Room> RoomServer::getRoom(const string &roomId)
{
  lock_guard<mutex> guard(roomsMutex_);
  const auto it = find_if(rooms_.begin(), rooms_.end(), [&](const shared_ptr<Room> &room)
  {
    return room->id == roomId;
  });

  if (it != rooms_.end())
  {
    return *it;
  }

  auto room = make_shared<Room>();
  room->id = roomId;
  room->state = chess::createInitialState();
  room->createdAt = makeTimestamp();
  room->updatedAt = room->createdAt;
  rooms_.push_back(room);
  return room;
}

string RoomServer::generateToken()
{
  static random_device device;
  static mt19937_64 engine(device());
  static uniform_int_distribution<unsigned long long> distribution;

  ostringstream out;
  out << hex << distribution(engine) << distribution(engine);
  return out.str();
}

string RoomServer::makeTimestamp()
{
  return nowString();
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
    case 302: return "HTTP/1.1 302 Found";
    case 400: return "HTTP/1.1 400 Bad Request";
    case 403: return "HTTP/1.1 403 Forbidden";
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

string RoomServer::roomHtml(const string &roomId)
{
  ostringstream out;
  out << "<!doctype html>"
      << "<html lang=\"en\">"
      << "<head>"
      << "<meta charset=\"utf-8\" />"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />"
      << "<title>Sachy " << jsonEscape(roomId) << "</title>"
      << "<link rel=\"stylesheet\" href=\"/public/style.css\" />"
      << "</head>"
      << "<body>"
      << "<main class=\"app-shell\">"
      << "<section class=\"status-bar\">"
      << "<div><div class=\"eyebrow\">Online chess</div><h1>Room " << jsonEscape(roomId) << "</h1></div>"
      << "<div id=\"statusText\" class=\"status-text\">Connecting...</div>"
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

string RoomServer::seatForToken(const Room &room, const string &token)
{
  if (!token.empty() && room.white && room.white->token == token)
  {
    return "white";
  }

  if (!token.empty() && room.black && room.black->token == token)
  {
    return "black";
  }

  return "spectator";
}

string RoomServer::assignSeat(Room &room, const string &token)
{
  const string existingSeat = seatForToken(room, token);
  if (existingSeat != "spectator")
  {
    return existingSeat;
  }

  if (!room.white)
  {
    room.white = PlayerSlot{token, makeTimestamp()};
    return "white";
  }

  if (!room.black)
  {
    room.black = PlayerSlot{token, makeTimestamp()};
    return "black";
  }

  return "spectator";
}

string RoomServer::roomStatusText(const chess::State &state)
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

string RoomServer::buildStateEnvelope(const Room &room, const string &seat)
{
  ostringstream out;
  out << "{"
      << "\"roomId\":\"" << jsonEscape(room.id) << "\","
      << "\"color\":";

  if (seat == "white" || seat == "black")
  {
    out << "\"" << seat << "\"";
  }
  else
  {
    out << "null";
  }

  out << ",\"players\":{"
      << "\"white\":" << (room.white ? "true" : "false") << ","
      << "\"black\":" << (room.black ? "true" : "false")
      << "},"
      << "\"state\":" << chess::stateToJson(room.state) << ","
      << "\"shareUrl\":\"/room/" << jsonEscape(room.id) << "\","
      << "\"status\":\"" << jsonEscape(roomStatusText(room.state)) << "\""
      << "}";
  return out.str();
}

string RoomServer::roomStateJson(const Room &room, const string &token)
{
  return buildStateEnvelope(room, seatForToken(room, token));
}

string RoomServer::joinResponseJson(const Room &room, const string &token, const string &seat)
{
  ostringstream out;
  out << "{"
      << "\"roomId\":\"" << jsonEscape(room.id) << "\","
      << "\"token\":\"" << jsonEscape(token) << "\","
      << "\"color\":";

  if (seat == "white" || seat == "black")
  {
    out << "\"" << seat << "\"";
  }
  else
  {
    out << "null";
  }

  out << ",\"status\":\"" << jsonEscape(roomStatusText(room.state)) << "\","
      << "\"state\":" << chess::stateToJson(room.state) << ","
      << "\"shareUrl\":\"/room/" << jsonEscape(room.id) << "\""
      << "}";
  return out.str();
}

string RoomServer::movesResponseJson(const vector<chess::Move> &moves)
{
  return chess::movesToJson(moves);
}

string RoomServer::eventPayloadJson(const Room &room, const string &token)
{
  ostringstream out;
  out << "{\"state\":" << chess::stateToJson(room.state) << ",\"color\":";
  const string seat = seatForToken(room, token);
  if (seat == "white" || seat == "black")
  {
    out << "\"" << seat << "\"";
  }
  else
  {
    out << "null";
  }
  out << ",\"status\":\"" << jsonEscape(roomStatusText(room.state)) << "\"}";
  return out.str();
}

chess::Move RoomServer::parseMoveParams(const vector<pair<string, string>> &params)
{
  chess::Move move;
  int fromX = 0;
  int fromY = 0;
  int toX = 0;
  int toY = 0;

  if (!parseIntStrict(getParam(params, "fromX"), fromX) ||
      !parseIntStrict(getParam(params, "fromY"), fromY) ||
      !parseIntStrict(getParam(params, "toX"), toX) ||
      !parseIntStrict(getParam(params, "toY"), toY))
  {
    throw runtime_error("Invalid move parameters.");
  }

  move.fromX = fromX;
  move.fromY = fromY;
  move.toX = toX;
  move.toY = toY;
  move.promotion = getParam(params, "promotion").empty() ? string("queen") : getParam(params, "promotion");
  move.castle = getParam(params, "castle");
  move.enPassant = getParam(params, "enPassant") == "true";
  return move;
}

string RoomServer::roomIdFromPath(const string &path, const string &prefix)
{
  if (!startsWith(path, prefix))
  {
    return {};
  }

  const size_t suffixStart = prefix.size();
  const size_t suffixEnd = path.find('/', suffixStart);
  if (suffixEnd == string::npos)
  {
    return path.substr(suffixStart);
  }

  return path.substr(suffixStart, suffixEnd - suffixStart);
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
    if (status != sf::Socket::Done)
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
    if (status != sf::Socket::Done)
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
    const string newRoomId = generateToken().substr(0, 8);
    getRoom(newRoomId);
    const string body;
    ostringstream response;
    response << statusLine(302) << "\r\n"
             << "Location: /room/" << newRoomId << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (request.method == "GET" && startsWith(request.path, "/room/"))
  {
    const string pageRoomId = request.path.substr(string("/room/").size());
    getRoom(pageRoomId);
    const string html = roomHtml(pageRoomId);
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

  if (startsWith(request.path, "/api/room/") && request.path.find("/state") != string::npos)
  {
    const string roomId = roomIdFromPath(request.path, "/api/room/");
    auto room = getRoom(roomId);
    const auto params = parseParams(request.query);
    const string token = getParam(params, "token");
    const string body = roomStateJson(*room, token);
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (startsWith(request.path, "/api/room/") && request.path.find("/join") != string::npos)
  {
    const string roomId = roomIdFromPath(request.path, "/api/room/");
    auto room = getRoom(roomId);
    const auto params = parseParams(request.body);
    string token = getParam(params, "token");
    if (token.empty())
    {
      token = generateToken();
    }

    lock_guard<mutex> guard(room->mutex);
    const string seat = assignSeat(*room, token);
    const string body = joinResponseJson(*room, token, seat);
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());
    return;
  }

  if (startsWith(request.path, "/api/room/") && request.path.find("/moves") != string::npos)
  {
    const string roomId = roomIdFromPath(request.path, "/api/room/");
    auto room = getRoom(roomId);
    const auto params = parseParams(request.query);
    const string token = getParam(params, "token");
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

    lock_guard<mutex> guard(room->mutex);
    const string seat = seatForToken(*room, token);
    if (seat != "white" && seat != "black")
    {
      const string body = "{\"moves\":[]}";
      ostringstream response;
      response << statusLine(200) << "\r\n"
               << "Content-Type: application/json; charset=utf-8\r\n"
               << "Content-Length: " << body.size() << "\r\n"
               << "Connection: close\r\n\r\n"
               << body;
      sendResponse(*socket, response.str());
      return;
    }

    const vector<chess::Move> moves = chess::legalMovesForPiece(room->state, x, y);
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

  if (startsWith(request.path, "/api/room/") && request.path.find("/move") != string::npos)
  {
    const string roomId = roomIdFromPath(request.path, "/api/room/");
    auto room = getRoom(roomId);
    const auto params = parseParams(request.body);
    const string token = getParam(params, "token");
    chess::Move move;

    try
    {
      move = parseMoveParams(params);
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

    lock_guard<mutex> guard(room->mutex);
    const string seat = seatForToken(*room, token);
    if (seat != "white" && seat != "black")
    {
      const string body = "{\"error\":\"You are not seated in this room.\"}";
      ostringstream response;
      response << statusLine(403) << "\r\n"
               << "Content-Type: application/json; charset=utf-8\r\n"
               << "Content-Length: " << body.size() << "\r\n"
               << "Connection: close\r\n\r\n"
               << body;
      sendResponse(*socket, response.str());
      return;
    }

    if (room->state.turn != (seat == "white" ? chess::Color::White : chess::Color::Black) || room->state.winner || room->state.draw)
    {
      const string body = "{\"error\":\"It is not your turn.\"}";
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
      room->state = chess::applyMove(room->state, move);
      room->updatedAt = makeTimestamp();
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

    const string body = string("{\"ok\":true,\"state\":") + chess::stateToJson(room->state) + "}";
    ostringstream response;
    response << statusLine(200) << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    sendResponse(*socket, response.str());

    const vector<shared_ptr<Subscriber>> subscribers = room->subscribers;
    for (const auto &subscriber : subscribers)
    {
      if (!subscriber || !subscriber->alive.load())
      {
        continue;
      }

      const string payload = string("data: ") + eventPayloadJson(*room, subscriber->token) + "\n\n";
      lock_guard<mutex> socketGuard(subscriber->writeMutex);
      if (!writeAll(*subscriber->socket, payload))
      {
        subscriber->alive.store(false);
      }
    }

    room->subscribers.erase(remove_if(room->subscribers.begin(), room->subscribers.end(), [](const shared_ptr<Subscriber> &subscriber)
    {
      return !subscriber || !subscriber->alive.load();
    }), room->subscribers.end());

    return;
  }

  if (startsWith(request.path, "/api/room/") && request.path.find("/events") != string::npos)
  {
    const string roomId = roomIdFromPath(request.path, "/api/room/");
    auto room = getRoom(roomId);
    const auto params = parseParams(request.query);
    const string token = getParam(params, "token");

    auto subscriber = make_shared<Subscriber>();
    subscriber->socket = shared_ptr<sf::TcpSocket>(socket.release());
    subscriber->token = token;

    {
      lock_guard<mutex> guard(room->mutex);
      room->subscribers.push_back(subscriber);
    }

    ostringstream headers;
    headers << statusLine(200) << "\r\n"
            << "Content-Type: text/event-stream; charset=utf-8\r\n"
            << "Cache-Control: no-cache, no-transform\r\n"
            << "Connection: keep-alive\r\n"
            << "X-Accel-Buffering: no\r\n\r\n";
    writeAll(*subscriber->socket, headers.str());

    {
      lock_guard<mutex> guard(room->mutex);
      const string payload = string("data: ") + eventPayloadJson(*room, token) + "\n\n";
      writeAll(*subscriber->socket, payload);
    }

    while (running_ && subscriber->alive.load())
    {
      this_thread::sleep_for(chrono::seconds(25));
      lock_guard<mutex> socketGuard(subscriber->writeMutex);
      if (!writeAll(*subscriber->socket, ": keep-alive\n\n"))
      {
        subscriber->alive.store(false);
        break;
      }
    }

    {
      lock_guard<mutex> guard(room->mutex);
      room->subscribers.erase(remove_if(room->subscribers.begin(), room->subscribers.end(), [&](const shared_ptr<Subscriber> &item)
      {
        return item == subscriber || !item || !item->alive.load();
      }), room->subscribers.end());
    }

    return;
  }

  if (request.method == "GET" && request.path == "/health")
  {
    lock_guard<mutex> guard(roomsMutex_);
    const string body = "{\"ok\":true,\"rooms\":" + to_string(rooms_.size()) + "}";
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