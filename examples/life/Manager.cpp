#include "Manager.h"
#include "ColorT.h"
#include "rules/JohnConway.h"

Manager::Manager(Engine* pEngine) : GameObject(pEngine) {
  world.Resize(sideSize);
  rules.push_back(new JohnConway());
}

void Manager::Start(){

}

void Manager::OnGui(ImGuiContext *context){
  ImGui::SetCurrentContext(context);

  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS",
              ImGui::GetIO().DeltaTime * 1000,
              1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate,
              ImGui::GetIO().Framerate);

  static auto newSize = sideSize;

  if(ImGui::SliderInt("Side Size", &newSize, 32, 256)) {
    if(newSize!=sideSize) {
      sideSize = newSize;
      world.Resize(newSize);
    }
  }

  ImGui::Text("Generator: %s", rules[ruleId]->GetName().c_str());
  if (ImGui::BeginCombo("##combo", rules[ruleId]->GetName().c_str())) // The second parameter is the label previewed before opening the combo.
  {
    for (int n = 0; n < rules.size(); n++) {
      bool is_selected =
          (rules[ruleId]->GetName() ==
           rules[n]->GetName());  // You can store your selection however you want, outside or inside your objects
      if (ImGui::Selectable(rules[n]->GetName().c_str(), is_selected)) {
        ruleId = n;
        clear();
      }
      if (is_selected)
        ImGui::SetItemDefaultFocus();  // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
    }
    ImGui::EndCombo();
  }

  ImGui::Text("Simulation");
  if(ImGui::Button("Step")) {
    isSimulating = false;
    accumulatedTime += ImGui::GetIO().DeltaTime;
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

  ImGui::Text("TimeToNextStep: %.3f", (timeBetweenSteps - accumulatedTime));

  ImGui::End(); // end settings
}
void Manager::OnDraw(SDL_Renderer* renderer){
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x/2, windowSize.y/2);
  float minDimension = std::min(windowSize.x, windowSize.y)*0.99f;
  auto lineColor = Color::LightGray;
  auto squareSide = minDimension/sideSize;
  auto sideSideOver2 = sideSize/2.0f;

  // draw cells
  auto liveCell = Color::Red;
  auto emptyCell = Color::DarkGray.Dark();
  for(int l = 0; l<sideSize; l++){
    for(int c = 0; c<sideSize; c++){
      auto state = world.Get({l,c});
      if(state)
        SDL_SetRenderDrawColor(renderer,liveCell.r,liveCell.g, liveCell.b,SDL_ALPHA_OPAQUE);
      else
        SDL_SetRenderDrawColor(renderer,emptyCell.r,emptyCell.g, emptyCell.b,SDL_ALPHA_OPAQUE);

      SDL_Rect rect = {
          static_cast<int>(ceil(center.x + (c-sideSideOver2)*squareSide)),
          static_cast<int>(ceil(center.y + (l-sideSideOver2)*squareSide)),
          static_cast<int>(squareSide),
          static_cast<int>(squareSide)};
      SDL_RenderFillRect(renderer, &rect);
    }
  }

  // Draw line matrix
  SDL_SetRenderDrawColor(renderer,lineColor.r,lineColor.g, lineColor.b, 200);
  for(int i = 0; i<=sideSize;i++){
    SDL_RenderDrawLine(renderer,
                       (int) (center.x - minDimension/2),
                       (int) (center.y - (i-sideSideOver2)*squareSide),
                       (int) (center.x + minDimension/2),
                       (int) (center.y - (i-sideSideOver2)*squareSide));
    SDL_RenderDrawLine(renderer,
                       (int) (center.x - (i-sideSideOver2)*squareSide),
                       (int) (center.y - minDimension/2),
                       (int) (center.x - (i-sideSideOver2)*squareSide),
                       (int) (center.y +  minDimension/2));
  }
}

void Manager::Update(float deltaTime){
  if(isSimulating) {
    accumulatedTime += deltaTime;
    if(accumulatedTime>timeBetweenSteps) {
      step();
      accumulatedTime = 0;
    }
  }
}

void Manager::step() {
  rules[ruleId]->Step(world);
  world.SwapBuffers();
}

Manager::~Manager() {
  for(auto x : rules)
    delete x;
  rules.clear();
}
void Manager::clear() {
  isSimulating = false;
  world.Resize(sideSize);
}
