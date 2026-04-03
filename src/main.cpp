#include <SFML/Graphics.hpp>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace sf;
using namespace std;
namespace fs = std::filesystem;

Color light = Color(255, 232, 207);
Color dark = Color(133, 68, 0);


int board[8][8] = 
{
  {-2, -3, -4, -5, -6, -4, -3, -2},
  {-1, -1, -1, -1, -1, -1, -1, -1},
  { 0,  0,  0,  0,  0,  0,  0,  0},
  { 0,  0,  0,  0,  0,  0,  0,  0},
  { 0,  0,  0,  0,  0,  0,  0,  0},
  { 0,  0,  0,  0,  0,  0,  0,  0},
  { 1,  1,  1,  1,  1,  1,  1,  1},
  { 2,  3,  4,  5,  6,  4,  3,  2},
};

int main(int argc, char *argv[])
{
  Texture whitePieces[7];
  Texture blackPieces[7];
  const float squareSize = 100.f;
  const float boardSize = squareSize * 8.f;
  const Vector2f closeButtonSize(52.f, 52.f);
  const float closeButtonMargin = 18.f;

  const fs::path executablePath = argc > 0 ? fs::absolute(argv[0]) : fs::current_path();
  const fs::path executableDirectory = executablePath.has_parent_path() ? executablePath.parent_path() : fs::current_path();
  const vector<fs::path> assetDirectories = {
      executableDirectory / "assets",
      executableDirectory / ".." / "assets",
      executableDirectory / ".." / "src" / "assets",
      fs::current_path() / "assets",
      fs::current_path() / "src" / "assets"
    };

  auto loadTexture = [&](Texture &texture, const string &fileName)
  {
    for (const fs::path &assetDirectory : assetDirectories)
    {
      const fs::path texturePath = assetDirectory / fileName;

      if (fs::exists(texturePath) && texture.loadFromFile(texturePath.string()))
      {
        return true;
      }
    }

    cerr << "Nepodarilo se nacist texturu: " << fileName << endl;
    cerr << "Hledano v:" << endl;

    for (const fs::path &assetDirectory : assetDirectories)
    {
      cerr << "  " << (assetDirectory / fileName).string() << endl;
    }

    return false;
  };

  if (!loadTexture(whitePieces[1], "bily-pesak.png") || !loadTexture(whitePieces[2], "bila-vez.png") || !loadTexture(whitePieces[3], "bily-kun.png") || !loadTexture(whitePieces[4], "bily-strelec.png") || !loadTexture(whitePieces[5], "bila-kralovna.png") || !loadTexture(whitePieces[6], "bily-kral.png") || !loadTexture(blackPieces[1], "cerny-pesak.png") || !loadTexture(blackPieces[2], "cerna-vez.png") || !loadTexture(blackPieces[3], "cerny-kun.png") || !loadTexture(blackPieces[4], "cerny-strelec.png") || !loadTexture(blackPieces[5], "cerna-kralovna.png") || !loadTexture(blackPieces[6], "cerny-kral.png"))
  {
    return 1;
  }

  RenderWindow window(VideoMode(), "Chess project", Style::Fullscreen);
  
  RectangleShape closeButton(closeButtonSize);
  closeButton.setFillColor(Color(190, 40, 40));

  RectangleShape closeLineA(Vector2f(28.f, 4.f));
  closeLineA.setOrigin(14.f, 2.f);
  closeLineA.setRotation(45.f);
  closeLineA.setFillColor(Color::White);

  RectangleShape closeLineB(Vector2f(28.f, 4.f));
  closeLineB.setOrigin(14.f, 2.f);
  closeLineB.setRotation(-45.f);
  closeLineB.setFillColor(Color::White);

  while (window.isOpen())
  {
    const Vector2u windowSize = window.getSize();
    const Vector2f boardOffset(
      (windowSize.x - boardSize) / 2.f,
      (windowSize.y - boardSize) / 2.f
    );
    const Vector2f closeButtonPosition(windowSize.x - closeButtonSize.x - closeButtonMargin, closeButtonMargin);
    closeButton.setPosition(closeButtonPosition);

    const Vector2f closeIconCenter(closeButtonPosition.x + closeButtonSize.x / 2.f, closeButtonPosition.y + closeButtonSize.y / 2.f);
    closeLineA.setPosition(closeIconCenter);
    closeLineB.setPosition(closeIconCenter);

    Event event;

    while (window.pollEvent(event))
    {
      if (event.type == Event::Closed)
        window.close();

      if (event.type == Event::MouseButtonPressed && event.mouseButton.button == Mouse::Left)
      {
        const Vector2f mousePosition(static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y));

        if (closeButton.getGlobalBounds().contains(mousePosition))
        {
          window.close();
        }
      }
    }

    const Vector2i mousePixelPosition = Mouse::getPosition(window);
    const Vector2f mousePosition(static_cast<float>(mousePixelPosition.x), static_cast<float>(mousePixelPosition.y));

    window.clear();

    RectangleShape square(Vector2f(squareSize, squareSize));

    for (int i = 1; i < 9; i++)
    {
      for (int j = 1; j < 9; j++)
      {
        square.setPosition(boardOffset.x + (i - 1) * squareSize, boardOffset.y + (j - 1) * squareSize);

        if ((i + j) % 2 == 0)
        {
          square.setFillColor(light);
        }
        else
        {
          square.setFillColor(dark);
        }

        window.draw(square);

        int pieceValue = board[j - 1][i - 1];

        if (pieceValue != 0)
        {
          Sprite piece;

          if (pieceValue > 0)
          {
            piece.setTexture(whitePieces[pieceValue]);
          }
          else
          {
            piece.setTexture(blackPieces[-pieceValue]);
          }
          piece.setPosition(boardOffset.x + (i - 1) * squareSize, boardOffset.y + (j - 1) * squareSize);
          window.draw(piece);
        }
      }
    }

    window.draw(closeButton);
    window.draw(closeLineA);
    window.draw(closeLineB);

    window.display();
  }

  return 0;
}