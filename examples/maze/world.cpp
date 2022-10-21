#include "World.h"

World::World(Engine* pEngine, int size=11): GameObject(pEngine), sideSize(size) {
  data.clear();
  data.resize((size_t)(size+1)*(size+1)*2);
}
Node World::GetNode(const Point2D& point) {
  // todo: implement this
  return Node();
}
void World::SetNode(const Point2D& point, const Node& node) {
  // todo implement this
}
void World::SetNorth(const Point2D& point, bool state) {
  // todo implement this
}
void World::SetEast(const Point2D& point, bool state) {
  // todo implement this
}
void World::SetSouth(const Point2D& point, bool state) {
  // todo implement this
}
void World::SetWest(const Point2D& point, bool state) {
  // todo implement this
}

void World::Start() {

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
};
void World::OnDraw(SDL_Renderer* renderer){

}
void World::Update(float deltaTime){

}