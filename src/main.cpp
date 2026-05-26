#include "room_server.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <windows.h>

int main()
{
  {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    const std::filesystem::path projectRoot =
      std::filesystem::path(exePath).parent_path().parent_path();

    std::filesystem::current_path(projectRoot);
  }

  try
  {
    RoomServer server(3000);
    std::system("start http://localhost:3000");
    server.run();
  }
  catch (const std::exception &error)
  {
    std::cerr << error.what() << std::endl;
    return 1;
  }

  return 0;
}
