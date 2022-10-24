#include "World.h"
#include "MazeGenerator.h"

World::World(Engine* pEngine, int size=11): GameObject(pEngine), sideSize(size) {}

Node World::GetNode(const Point2D& point) {
  auto index = Point2DtoIndex(point);
  // todo: not tested!!
  return {data[index],data[index+3],data[index+(sideSize+1)*2],data[index+1]};
}
void World::SetNode(const Point2D& point, const Node& node) {
  // todo implement this
}
void World::SetNorth(const Point2D& point, const bool& state) {
  // todo implement this
}
void World::SetEast(const Point2D& point, const bool& state) {
  // todo implement this
}
void World::SetSouth(const Point2D& point, const bool& state) {
  // todo implement this
}
void World::SetWest(const Point2D& point, const bool& state) {
  // todo implement this
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

  if(ImGui::Button("Generate")){
    generator.Generate(this);
  }
}

void World::OnDraw(SDL_Renderer* renderer){
  auto windowSize = engine->window->size();
  float linesize = (std::min(windowSize.x, windowSize.y) / (float)sideSize);

  Vector2 displacement = {(windowSize.x/2) - linesize*(sideSize/2), (windowSize.y/2) - linesize*(sideSize/2) - linesize/2};

  SDL_SetRenderDrawColor(renderer,SDL_ALPHA_OPAQUE,SDL_ALPHA_OPAQUE, SDL_ALPHA_OPAQUE,SDL_ALPHA_OPAQUE);
  for (int i = 0; i < data.size(); i+=2) {
    Vector2 pos = {(float)((i/2)%(sideSize+1)), (float)((i/2)/(sideSize+1))};
    pos *= linesize;
    pos += displacement;

    // top
    if(data[i])
      SDL_RenderDrawLine(renderer,(int) pos.x,(int) pos.y,(int) (pos.x + linesize),(int) pos.y);
    // left
    if(data[i+1])
      SDL_RenderDrawLine(renderer,(int) pos.x,(int) pos.y,(int) pos.x,(int) (pos.y + linesize));
  }
}

void World::Update(float deltaTime){

}

void World::Clear() {
  data.clear();
  data.resize((size_t)(sideSize+1)*(sideSize+1)*2);
  for (int i = 0; i < data.size(); ++i) {
    if(i%((sideSize+1)*2)==(sideSize+1)*2-2) // remove left elements on the last column
      data[i] = false;
    else
      data[i] = true; // todo: remove the left elements from the bottom points
  }
}
