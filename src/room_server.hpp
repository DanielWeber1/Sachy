#pragma once

#include "chess_engine.hpp"

#include <SFML/Network.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
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

  unsigned short port_;
  sf::TcpListener listener_;
  std::atomic<bool> running_{true};

  std::mutex gameMutex_;
  chess::State state_;

  void handleConnection(std::unique_ptr<sf::TcpSocket> socket);
  HttpRequest readRequest(sf::TcpSocket &socket);
  bool sendResponse(sf::TcpSocket &socket, const std::string &response);

  void resetGame();
  bool applyBotMove();

  static std::string urlDecode(const std::string &value);
  static std::vector<std::pair<std::string, std::string>> parseParams(const std::string &value);
  static std::string getParam(const std::vector<std::pair<std::string, std::string>> &params, const std::string &key);
  static std::string getHeader(const HttpRequest &request, const std::string &key);
  static std::string statusLine(int code);
  static std::string mimeTypeFor(const std::filesystem::path &path);
  static std::string pageHtml();
  static std::string jsonEscape(const std::string &value);
  static std::string statusTextFor(const chess::State &state);
  static std::string stateEnvelopeJson(const chess::State &state);
  static std::string movesResponseJson(const std::vector<chess::Move> &moves);
  static bool startsWith(const std::string &value, const std::string &prefix);
  static std::string readFileText(const std::filesystem::path &path, bool binary = false);
  static bool writeAll(sf::TcpSocket &socket, const std::string &data);
  static std::string findAssetPath(const std::string &requestPath);
};
