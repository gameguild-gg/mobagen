#include "World.h"
#include <chrono>

World::World(Engine* pEngine, int size=11): GameObject(pEngine), sideSize(size) {}

Node World::GetNode(const Point2D& point) {
  auto index = Point2DtoIndex(point);
  // todo: not tested!!
  return {data[index],
          data[index+3],
          data[index+(sideSize+1)*2],
          data[index+1]};
}

bool World::GetNorth(const Point2D& point) {
  return data[Point2DtoIndex(point)];
}

bool World::GetEast(const Point2D& point) {
  return data[Point2DtoIndex(point)+3];
}

bool World::GetSouth(const Point2D& point) {
  return data[Point2DtoIndex(point)+(sideSize+1)*2];
}

bool World::GetWest(const Point2D& point) {
  return data[Point2DtoIndex(point)+1];
}

void World::SetNode(const Point2D& point, const Node& node) {
  data[Point2DtoIndex(point)] = node.GetNorth();
  data[Point2DtoIndex(point)+3] = node.GetEast();
  data[Point2DtoIndex(point)+(sideSize+1)*2] = node.GetSouth();
  data[Point2DtoIndex(point)+1] = node.GetWest();
}
void World::SetNorth(const Point2D& point, const bool& state) {
  data[Point2DtoIndex(point)] = state;
}
void World::SetEast(const Point2D& point, const bool& state) {
  data[Point2DtoIndex(point)+3] = state;
}
void World::SetSouth(const Point2D& point, const bool& state) {
  data[Point2DtoIndex(point)+(sideSize+1)*2] = state;
}
void World::SetWest(const Point2D& point, const bool& state) {
  data[Point2DtoIndex(point)+1] = state;
}

void World::Start() {
  this->Clear();
}

void World::OnGui(ImGuiContext *context){
  ImGui::SetCurrentContext(context);
  float deltaTime = ImGui::GetIO().DeltaTime;

  ImGui::SetCurrentContext(context);
  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS",
              ImGui::GetIO().DeltaTime * 1000,
              1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate,
              ImGui::GetIO().Framerate);
  static auto newSize = sideSize;

  if(ImGui::SliderInt("Side Size", &newSize, 5, 29)) {
    newSize = (newSize/4)*4 + 1;
    if(newSize!=sideSize) {
      sideSize = newSize;
      Clear();
    }
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
  ImGui::Text("Move duration: %lli", moveDuration);
  ImGui::SliderFloat("Turn Duration", &timeBetweenAITicks, 0.1, 30);
  ImGui::Text("Next turn in %.1f", timeForNextTick);
}

void World::OnDraw(SDL_Renderer* renderer){
  auto windowSize = engine->window->size();
  float linesize = (std::min(windowSize.x, windowSize.y) / (float)sideSize)*0.9f;

  Vector2 displacement = {(windowSize.x/2) - linesize*(sideSize/2) - linesize/2, (windowSize.y/2) - linesize*(sideSize/2) - linesize/2};

  SDL_SetRenderDrawColor(renderer,SDL_ALPHA_OPAQUE,SDL_ALPHA_OPAQUE, SDL_ALPHA_OPAQUE,SDL_ALPHA_OPAQUE);
  for (int i = 0; i < data.size(); i+=2) {
    Vector2 pos = {(float)((i/2)%(sideSize+1)), (float)((i/2)/(sideSize+1))};
    pos *= linesize;
    pos += displacement;

    // north
    if(data[i])
      SDL_RenderDrawLine(renderer,(int) pos.x,(int) pos.y,(int) (pos.x + linesize),(int) pos.y);
    // west
    if(data[i+1])
      SDL_RenderDrawLine(renderer,(int) pos.x,(int) pos.y,(int) pos.x,(int) (pos.y + linesize));
  }

  for(int i=0; i<sideSize*sideSize; i++) {
    auto c = colors[i];
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);

    Vector2 pos = {(float)(i%sideSize), (float)(i/sideSize)};
    pos *= linesize;
    pos += displacement;
    SDL_Rect rect = {(int)(pos.x+1),(int)(pos.y+1), (int)(linesize-1), (int)(linesize-1)};
    SDL_RenderFillRect(renderer, &rect);
  }
}

void World::Update(float deltaTime){
  if(isSimulating) {
    // update timer
    timeForNextTick -= deltaTime;
    if (timeForNextTick < 0) {
      step();
      timeForNextTick = timeBetweenAITicks;
    }
  }
}

void World::Clear() {
  data.clear();
  data.resize((size_t)(sideSize+1)*(sideSize+1)*2);
  for (int i = 0; i < data.size(); ++i) {
    if(i%((sideSize+1)*2)==(sideSize+1)*2-2 || // remove north elements on the last column
       (i/((sideSize+1)*2)==sideSize && i%2==1)) // remove west elements on the last line
      data[i] = false;
    else
      data[i] = true;
  }

  colors.clear();
  colors.resize(sideSize*sideSize);
  for(int i=0; i<sideSize*sideSize; i++)
    colors[i] = (Color::Gray).Dark();
}

void World::step() {
  auto start = std::chrono::high_resolution_clock::now();
  if(generator.Step(this) == false) {
    isSimulating = false;
  }
  auto stop = std::chrono::high_resolution_clock::now();
  moveDuration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
}
void World::SetNodeColor(const Point2D& node, const Color32& color) {
  colors[(node.y+sideSize/2)*sideSize+node.x+sideSize/2] = color;
}

int World::GetSize() const {
  return sideSize;
}
