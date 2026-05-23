#pragma once

#include "chess_engine.hpp"

#include <SFML/Network.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class RoomServer
{
public:
  explicit RoomServer(unsigned short port = 3000);
  void run();

private:
  struct HttpRequest
  {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string version;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
  };

  struct PlayerSlot
  {
    std::string token;
    std::string joinedAt;
  };

  struct Subscriber
  {
    std::shared_ptr<sf::TcpSocket> socket;
    std::mutex writeMutex;
    std::atomic<bool> alive{true};
    std::string token;
  };

  struct Room
  {
    std::string id;
    chess::State state = chess::createInitialState();
    std::optional<PlayerSlot> white;
    std::optional<PlayerSlot> black;
    std::vector<std::shared_ptr<Subscriber>> subscribers;
    std::mutex mutex;
    std::string createdAt;
    std::string updatedAt;
  };

  unsigned short port_;
  sf::TcpListener listener_;
  std::atomic<bool> running_{true};
  std::mutex roomsMutex_;
  std::vector<std::shared_ptr<Room>> rooms_;

  std::shared_ptr<Room> getRoom(const std::string &roomId);
  void handleConnection(std::unique_ptr<sf::TcpSocket> socket);
  HttpRequest readRequest(sf::TcpSocket &socket);
  bool sendResponse(sf::TcpSocket &socket, const std::string &response);

  static std::string generateToken();
  static std::string makeTimestamp();
  static std::string urlDecode(const std::string &value);
  static std::vector<std::pair<std::string, std::string>> parseParams(const std::string &value);
  static std::string getParam(const std::vector<std::pair<std::string, std::string>> &params, const std::string &key);
  static std::string getHeader(const HttpRequest &request, const std::string &key);
  static std::string statusLine(int code);
  static std::string mimeTypeFor(const std::filesystem::path &path);
  static std::string roomHtml(const std::string &roomId);
  static std::string jsonEscape(const std::string &value);
  static std::string roomStateJson(const Room &room, const std::string &token);
  static std::string joinResponseJson(const Room &room, const std::string &token, const std::string &seat);
  static std::string movesResponseJson(const std::vector<chess::Move> &moves);
  static std::string eventPayloadJson(const Room &room, const std::string &token);
  static std::string seatForToken(const Room &room, const std::string &token);
  static std::string assignSeat(Room &room, const std::string &token);
  static std::string roomStatusText(const chess::State &state);
  static bool startsWith(const std::string &value, const std::string &prefix);
  static std::string readFileText(const std::filesystem::path &path, bool binary = false);
  static bool writeAll(sf::TcpSocket &socket, const std::string &data);
  static std::string findAssetPath(const std::string &requestPath);
  static std::string buildStateEnvelope(const Room &room, const std::string &seat);
  static chess::Move parseMoveParams(const std::vector<std::pair<std::string, std::string>> &params);
  static std::string roomIdFromPath(const std::string &path, const std::string &prefix);
};