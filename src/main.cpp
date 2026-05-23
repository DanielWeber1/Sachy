#include "room_server.hpp"

#include <iostream>

int main()
{
  try
  {
    RoomServer server(3000);
    server.run();
  }
  catch (const std::exception &error)
  {
    std::cerr << error.what() << std::endl;
    return 1;
  }

  return 0;
}