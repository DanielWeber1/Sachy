# Sachy Online

This workspace now runs as a browser-based multiplayer chess game with a native C++ server and C++ move validation.

## Run locally

Build it with the VS Code build task or with this command:

```bash
g++ -std=c++17 -g -DSFML_STATIC -static-libgcc -static-libstdc++ src\main.cpp src\chess_engine.cpp src\room_server.cpp -o bin\sachy.exe -I C:\msys64\ucrt64\include -L C:\msys64\ucrt64\lib -lsfml-network-s -lsfml-system-s -lws2_32
```

Run `bin\sachy.exe`, open `http://localhost:3000`, and copy the room URL from the browser. Anyone who opens the same room URL joins the same game.

## Files

- `src/room_server.cpp` serves the room pages and event stream.
- `src/chess_engine.cpp` contains the chess rules and move validation.
- `public/client.js` renders the board and sends moves.