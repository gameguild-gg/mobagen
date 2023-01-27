#include "Manager.h"

Manager::Manager(Engine* pEngine) : GameObject(pEngine) {
  world.Resize(sideSize);
}

void Manager::Start(){

}
void Manager::OnGui(ImGuiContext *context){

}
void Manager::OnDraw(SDL_Renderer* renderer){

}
void Manager::Update(float deltaTime){

}
