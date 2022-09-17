#include "World.h"
#include "Polygon.h"

void World::print() {
  for (int i = 0; i < worldState.size();) {
    std::cout << worldState[i];
    i++;
    if ((i + sideSize) % (2 * sideSize) == 0)
      std::cout << std::endl << " ";
    else if (i % sideSize == 0)
      std::cout << std::endl;
    else
      std::cout << " ";
  }
}

World::World(Engine *pEngine, int size): GameObject(pEngine), sideSize(size){
  if(size%2==0)
    throw;
  cat = new Cat(this);
  catcher = new Catcher(this);

  clearWorld();
}

void World::clearWorld() {
  worldState.clear();
  worldState.resize(sideSize*sideSize);
  for(char &i : worldState) i='.';
  for(int i=0; i<sideSize*1.5; i++)
    worldState[Random::Range(0,(int)worldState.size()-1)]='#';
  worldState[(int)worldState.size()/2] = 'C';
}

Point2D World::E(const Point2D& p) {
  return {p.x-1, p.y};
}

Point2D World::W(const Point2D& p) {
  return {p.x+1, p.y};
}

Point2D World::NE(const Point2D& p) {
  if(p.y%2)
    return {p.x-1, p.y-1};
  return {p.x, p.y-1};
}

Point2D World::NW(const Point2D& p) {
  if(p.y%2)
    return {p.x, p.y-1};
  return {p.x+1, p.y-1};
}

Point2D World::SE(const Point2D& p) {
  if(p.y%2)
    return {p.x-1, p.y+1};
  return {p.x, p.y+1};
}

Point2D World::SW(const Point2D& p) {
  if(p.y%2)
    return {p.x, p.y+1};
  return {p.x+1, p.y+1};
}

bool World::isValidPosition(Point2D p) const {
    auto sideOver2=sideSize/2;
    return
        (p.x>=-sideOver2) &&
        (p.x<=sideOver2) &&
        (p.y<=sideOver2) &&
        (p.y>=-sideOver2);
}

bool World::isEmpty(const Point2D &p) {
    return getContent(p)=='.';
}

bool World::isNeighbor(const Point2D &p1, const Point2D &p2) {
    if(p1.y == p2.y)
        return p1.x-p2.x==1;
    auto delta = abs(p1.y-p2.y);
    if(delta!=1)
        return false;
    if(p1.y%2==0)
        return p1.x == p2.x || p2.x == p1.x-1;
    else
        return p1.x == p2.x || p2.x == p1.x+1;
}

void World::OnDraw(SDL_Renderer* renderer) {
    Hexagon hex;
    Transform t;
    auto windowSize = engine->window->size();
    float minSide = std::min(windowSize.x, windowSize.y);
    t.scale *= (minSide / sideSize)/2;

    t.position = {windowSize.x/2 - (sideSize)*t.scale.x, windowSize.y/2 - (sideSize-1)*t.scale.y};
    for (int i = 0; i < worldState.size();) {
      if(worldState[i]=='#')
        hex.Draw(renderer, t, Color::Blue);
      else if(worldState[i]=='C')
        hex.Draw(renderer, t, Color::Red);
      else
        hex.Draw(renderer, t, Color::Gray);
      i++;
      if ((i) % (2 * sideSize) == 0) {
        t.position.x = windowSize.x / 2 - (sideSize)*t.scale.x;
        t.position.y += 2*t.scale.y;
      }
      else if (i % sideSize == 0) {
        t.position.x = windowSize.x / 2 - (sideSize)*t.scale.x + t.scale.x;
        t.position.y += 2*t.scale.y;
      }
      else
        t.position.x += 2*t.scale.x;
    }
}

void World::OnGui(ImGuiContext *context) {
    ImGui::SetCurrentContext(context);
    ImGui::Begin("Settings", nullptr);
    static auto newSize = sideSize;
    if(ImGui::SliderInt("Side Size", &newSize, 5, 21) && sideSize != (newSize/2)*2 + 1) {
        sideSize = (newSize/2)*2 + 1;
        clearWorld();
    }
    if(ImGui::Button("Randomize")){
      clearWorld();
    }
    ImGui::End();
}

void World::Update(float deltaTime) {
    timeForNextTick-=deltaTime;
    if(timeForNextTick<0) {
        timeForNextTick = timeBetweenAITicks;
        if(catTurn) {

        }
        catTurn^=catTurn;
    }
}
