#include <SFML/Graphics.hpp>

using namespace sf;

Color light = Color(235, 235, 208);
Color dark = Color(119, 149, 86);

int main()
{
  RenderWindow window(VideoMode(1000, 1000), "Chess project");

  while (window.isOpen())
  {
    Event event;
    
    while (window.pollEvent(event))
    {
      if (event.type == Event::Closed)
        window.close();
    }

    window.clear();

    RectangleShape square(Vector2f(100.f, 100.f));

    for (int i = 0; i < 8; i++)
    {
      for (int j = 0; j < 8; j++)
      {
        square.setPosition((i + 1) * 100.f, (j + 1) * 100.f);

        if ((i + j) % 2 == 0)
        {
          square.setFillColor(light);
        }
        else
        {
          square.setFillColor(dark);
        }

        window.draw(square);
      }
    }
    window.display();
  }
  return 0;
}