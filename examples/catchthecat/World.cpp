#include "World.h"
#include "Polygon.h"
#include <chrono>

void World::print() {
  auto catposid = catPosition.y*(sideSize/2) + catPosition.x + sideSize*sideSize/2;
  for (int i = 0; i < worldState.size();) {
    std::cout << ((i==catposid)?('C'):(worldState[i]?'#':'.'));
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
  cat = new Cat();
  catcher = new Catcher();

  clearWorld();
}

void World::clearWorld() {
  worldState.clear();
  worldState.resize(sideSize*sideSize);
  for(auto && i : worldState) i= false;
  for(int i=0; i<sideSize*sideSize*0.05; i++)
    worldState[Random::Range(0,(int)worldState.size()-1)]= true;
  catPosition = {0,0};
  worldState[(int)worldState.size()/2] = false; // clear cat
  isSimulating = false;
  catTurn = true;
  timeForNextTick = timeBetweenAITicks;
  catWon = false;
  catcherWon = false;
}

Point2D World::E(const Point2D& p) {
  return {p.x+1, p.y};
}

Point2D World::W(const Point2D& p) {
  return {p.x-1, p.y};
}

Point2D World::NE(const Point2D& p) {
  if(p.y%2)
    return {p.x+1, p.y-1};
  return {p.x, p.y-1};
}

Point2D World::NW(const Point2D& p) {
  if(p.y%2)
    return {p.x, p.y-1};
  return {p.x-1, p.y-1};
}

Point2D World::SE(const Point2D& p) {
  if(p.y%2)
    return {p.x, p.y+1};
  return {p.x-1, p.y+1};
}

Point2D World::SW(const Point2D& p) {
  if(p.y%2)
    return {p.x+1, p.y+1};
  return {p.x, p.y+1};
}

bool World::isValidPosition(const Point2D& p) {
    auto sideOver2=sideSize/2;
    return
        (p.x>=-sideOver2) &&
        (p.x<=sideOver2) &&
        (p.y<=sideOver2) &&
        (p.y>=-sideOver2);
}

bool World::isNeighbor(const Point2D &p1, const Point2D &p2) {
  return NE(p1) == p2 ||
         NW(p1) == p2 ||
         E(p1) == p2 ||
         W(p1) == p2 ||
         SE(p1) == p2 ||
         SW(p1) == p2;
}

void World::OnDraw(SDL_Renderer* renderer) {
    Hexagon hex;
    Transform t;
    auto windowSize = engine->window->size();
    float minSide = std::min(windowSize.x, windowSize.y);
    t.scale *= (minSide / sideSize)/2;

    t.position = {windowSize.x/2 - (sideSize)*t.scale.x, windowSize.y/2 - (sideSize-1)*t.scale.y};
    auto catposid = (catPosition.y + sideSize/2)*(sideSize) + catPosition.x + sideSize/2;
    for (int i = 0; i < worldState.size();) {
      if(catposid==i)
        hex.Draw(renderer, t, Color::Red);
      else if(worldState[i])
        hex.Draw(renderer, t, Color::Blue);
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
    ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS",
                ImGui::GetIO().DeltaTime * 1000,
                1.0f / ImGui::GetIO().DeltaTime,
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);
    static auto newSize = sideSize;
    if(ImGui::SliderInt("Side Size", &newSize, 3, 21) && sideSize != (newSize/2)*2 + 1) {
        sideSize = (newSize/2)*2 + 1;
        clearWorld();
    }
    if(ImGui::SliderFloat("Turn Duration", &timeBetweenAITicks, 0.1, 30) && sideSize != (newSize/2)*2 + 1) {
      sideSize = (newSize/2)*2 + 1;
      clearWorld();
    }
    if(catTurn)
      ImGui::Text("Turn: CAT");
    else
      ImGui::Text("Turn: CATCHER");
    ImGui::Text("Move duration: %lli", moveDuration);
    ImGui::Text("Next turn in %.1f", timeForNextTick);
    if(ImGui::Button("Randomize")){
      clearWorld();
    }
    ImGui::Text("Simulation");
    if(ImGui::Button("Step")) {
      isSimulating = false;
      step();
    }
    ImGui::SameLine();
    if(ImGui::Button("Start")) {
      isSimulating = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("Pause")) {
      isSimulating = false;
    }
    ImGui::End();

    if((catcherWon || catWon)) {
      ImGuiIO& io = ImGui::GetIO();
      ImVec2 pos(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
      ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_AlwaysAutoResize
                               | ImGuiWindowFlags_NoSavedSettings;
      if (ImGui::Begin("Game Over", nullptr, flags)) {
        if (ImGui::Button("OK", ImVec2(200, 0))) {
          clearWorld();
        }
      }
    }
}

void World::Update(float deltaTime) {
  if(isSimulating) {
    // update timer
    timeForNextTick -= deltaTime;
    if (timeForNextTick < 0) {
      step();
      timeForNextTick = timeBetweenAITicks;
    }
  }
}

Point2D World::getCat() {
  return catPosition;
}

void World::step() {
  if(catWon || catcherWon) {
    clearWorld();
    return;
  }

  auto start = std::chrono::high_resolution_clock::now();

  // run the turn
  if (catTurn) {
    auto move = cat->Move(this);
    if(catCanMoveToPosition(move)) {
      catPosition = move;
      catWon = catWinVerification();
    }
    else {
      isSimulating = false;
      catcherWon = true; // cat made a bad move
    }
  }
  else {
    auto move = catcher->Move(this);
    if(catcherCanMoveToPosition(move)) {
      worldState[(move.y + sideSize / 2) * (sideSize) + move.x + sideSize / 2] =
          true;
      catcherWon = catcherWinVerification();
    } else {
      isSimulating = false;
      catWon = true; // catcher made a bad move
    }
  }
  auto stop = std::chrono::high_resolution_clock::now();
  moveDuration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
  // change turn
  catTurn = !catTurn;
}

int World::getWorldSideSize() {
  return sideSize;
}

bool World::catWinVerification() {
  auto sideOver2 = sideSize/2;
  return abs(catPosition.x) == sideOver2 || abs(catPosition.y) == sideOver2;
}

bool World::catcherWinVerification() {
  return getContent(NE(catPosition)) &&
         getContent(NW(catPosition)) &&
         getContent(E(catPosition)) &&
         getContent(W(catPosition)) &&
         getContent(SE(catPosition)) &&
         getContent(SW(catPosition));
}

bool World::catCanMoveToPosition(Point2D p) const {
  return isNeighbor(catPosition, p) && !getContent(p);
}
bool World::catcherCanMoveToPosition(Point2D p) const {
  auto sideOver2 = sideSize/2;
  return p.x!=catPosition.x &&
         p.y!=catPosition.y &&
         abs(p.x) <= sideOver2 &&
         abs(p.y) <= sideOver2;
}
