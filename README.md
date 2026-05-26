# Sachy Local

This workspace runs as a local terminal chess game with C++ move validation and a built-in bot opponent.

## Run locally

Build it with the VS Code build task or with this command:

```bash
g++ -std=c++17 -g -DSFML_STATIC -static-libgcc -static-libstdc++ src\main.cpp src\chess_engine.cpp src\room_server.cpp -o bin\sachy.exe -I C:\msys64\ucrt64\include -L C:\msys64\ucrt64\lib -lsfml-network-s -lsfml-system-s -lws2_32
```

Run `bin\sachy.exe` in a terminal and play as white against the black bot.

Commands:

- Enter moves in coordinate format like `e2e4`.
- Enter `new` to reset the game.
- Enter `quit` to exit.

## Files

- `src/main.cpp` runs the local terminal game loop and bot move selection.
- `src/chess_engine.cpp` contains the chess rules and move validation.
- `src/room_server.cpp` and `public/client.js` are no longer required for local play.