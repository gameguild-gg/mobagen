#include "World.h"

World::World(Engine* pEngine, int size=11): GameObject(pEngine), sideSize(size) {
  data.clear();
  data.resize((size_t)(size+1)*(size+1)*2);
}
Node World::GetNode(const Point2D& point) {
  // todo: implement this
  return Node();
}
// todo implement this
void World::SetNode(const Point2D& point, const Node& node) {}
void World::SetNorth(const Point2D& point, bool state) {}
void World::SetEast(const Point2D& point, bool state) {}
void World::SetSouth(const Point2D& point, bool state) {}
void World::SetWest(const Point2D& point, bool state) {}

void Start() {

}
void OnGui(ImGuiContext *context){
  ImGui::SetCurrentContext(context);
  float deltaTime = ImGui::GetIO().DeltaTime;
};
void OnDraw(SDL_Renderer* renderer){

}
void Update(float deltaTime){

}