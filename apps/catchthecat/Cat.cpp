#include "Cat.h"
#include "World.h"

#include <queue>
#include <stdexcept>

Point2D Cat::Move(CatWorld* world) {
  //auto rand = Random::Range(0, 5);
  auto pos = world->getCat();

  //Heuristics
  //Dodge areas with many blocked tiles

  //Dijikstra
  std::priority_queue<
    std::pair<float, int>,
    std::vector<std::pair<float, int>>,
    std::greater<std::pair<float, int>>
  > pq;

  std::unordered_map<int, std::pair<int, float>> cameFromMap;

  //pq.push();

  std::pair<float, int> current;
  while (!pq.empty())
  {
    current = pq.top();

    for (std::pair<float, int> next : world->neighbors(current))
  }

  /*switch (rand) {
    case 0:
      return CatWorld::NE(pos);
    case 1:
      return CatWorld::NW(pos);
    case 2:
      return CatWorld::E(pos);
    case 3:
      return CatWorld::W(pos);
    case 4:
      return CatWorld::SW(pos);
    case 5:
      return CatWorld::SE(pos);
    default:
      throw std::runtime_error("random out of range");
  }
  */
}
