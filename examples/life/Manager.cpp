#include "Manager.h"
#include "ColorT.h"
#include "rules/JohnConway.h"
#include "rules/HexagonGameOfLife.h"
#include <iostream>

Manager::Manager(Engine* pEngine1, Engine* pEngine) : GameObject(pEngine) {
  world.Resize(sideSize);
  rules.push_back(new HexagonGameOfLife());
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
  if(ImGui::SliderInt("Side Size", &newSize, 5, 256)) {
    newSize = (newSize/4)*4 + 1;
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
  static auto newTime = timeBetweenSteps;
  if(ImGui::SliderFloat("Time Between Steps", &newTime, 0.0001f, 1.0f)) {
    if(newTime!=timeBetweenSteps)
      timeBetweenSteps = newTime;
  }

  if(ImGui::Button("Randomize")) {
    isSimulating = false;
    world.Randomize();
  }

  ImGui::End(); // end settings

  static Point2D lastIndexClicked = {INT32_MAX, INT32_MAX};
  if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
    auto mousePos = ImGui::GetMousePos();
    Point2D index;
    if(rules[ruleId]->GetTileSet()==GameOfLifeTileSetEnum::Square)
      index = mousePositionToIndex(mousePos);
    else if(rules[ruleId]->GetTileSet()==GameOfLifeTileSetEnum::Hexagon) {
      auto windowSize = engine->window->size();
      auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
      float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
      auto squareSide = minDimension / sideSize;
      auto sideSideOver2 = sideSize / 2.0f;
      index = mousePositionToIndex(mousePos);
      float displacement = abs(index.y-(int)sideSideOver2)%2 == 1 ? squareSide/2 : 0;
      mousePos.x-=displacement;
      index = mousePositionToIndex(mousePos);
    }

    std::cout << index.to_string() << std::endl;

    if(lastIndexClicked!=index) {
      lastIndexClicked = index;
      std::cout << "MatrixPos: " << index.to_string() << std::endl;
      if (index.x >= 0 && index.x < sideSize && index.y >= 0 && index.y < sideSize) {
        world.SetCurrent(index, !world.Get(index)); // to be visible
        world.SetNext(index, !world.Get(index)); // to be used next time
      }
    }
  } if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
    lastIndexClicked = {INT32_MAX, INT32_MAX};
  }
}

void Manager::OnDraw(SDL_Renderer* renderer){
  if(rules[ruleId]->GetTileSet()==GameOfLifeTileSetEnum::None) {
    std::cout << "your rule should explicitly say which board you want to use";
    return;
  }
  if(rules[ruleId]->GetTileSet()==GameOfLifeTileSetEnum::Square) {
    auto windowSize = engine->window->size();
    auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
    float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
    auto squareSide = minDimension / sideSize;
    auto sideSideOver2 = sideSize / 2.0f;

    // draw cells
    auto liveCell = Color::Yellow.Dark();
    auto emptyCell = Color::DarkGray.Dark().Dark().Dark();
    for (int l = 0; l < sideSize; l++) {
      for (int c = 0; c < sideSize; c++) {
        auto state = world.Get({c, l});
        if (state)
          SDL_SetRenderDrawColor(renderer, liveCell.r, liveCell.g, liveCell.b, SDL_ALPHA_OPAQUE);
        else
          SDL_SetRenderDrawColor(renderer, emptyCell.r, emptyCell.g, emptyCell.b, SDL_ALPHA_OPAQUE);

        SDL_Rect rect = {
            static_cast<int>(ceil(center.x + (c - sideSideOver2) * squareSide)),
            static_cast<int>(ceil(center.y + (l - sideSideOver2) * squareSide)),
            static_cast<int>(squareSide),
            static_cast<int>(squareSide)};
        SDL_RenderFillRect(renderer, &rect);
      }
    }

    // Draw line matrix
    auto lineColor = Color32(50, 50, 50, 50);
    SDL_SetRenderDrawColor(renderer, lineColor.r, lineColor.g, lineColor.b, 10);
    for (int i = 0; i <= sideSize; i++) {
      if (sideSize < 50 || i == 0 || i == sideSize) {
        SDL_RenderDrawLine(renderer,
                           (int) (center.x - minDimension / 2),
                           (int) (center.y - (i - sideSideOver2) * squareSide),
                           (int) (center.x + minDimension / 2),
                           (int) (center.y - (i - sideSideOver2) * squareSide));
        SDL_RenderDrawLine(renderer,
                           (int) (center.x - (i - sideSideOver2) * squareSide),
                           (int) (center.y - minDimension / 2),
                           (int) (center.x - (i - sideSideOver2) * squareSide),
                           (int) (center.y + minDimension / 2));
      }
    }
  }
  else if (rules[ruleId]->GetTileSet()==GameOfLifeTileSetEnum::Hexagon){
    auto windowSize = engine->window->size();
    auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
    float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
    auto squareSide = minDimension / sideSize;
    auto sideSideOver2 = sideSize / 2.0f;

    // draw cells
    auto liveCell = Color::Yellow.Dark();
    auto emptyCell = Color::DarkGray.Dark().Dark().Dark();
    for (int l = 0; l < sideSize; l++) {
      float displacement = abs(l-(int)sideSideOver2)%2 == 1 ? squareSide/2 : 0;
      for (int c = 0; c < sideSize; c++) {
        auto state = world.Get({c, l});
        if (state)
          SDL_SetRenderDrawColor(renderer, liveCell.r, liveCell.g, liveCell.b, SDL_ALPHA_OPAQUE);
        else
          SDL_SetRenderDrawColor(renderer, emptyCell.r, emptyCell.g, emptyCell.b, SDL_ALPHA_OPAQUE);

        SDL_Rect rect = {
            static_cast<int>(ceil(center.x + displacement + (c - sideSideOver2) * squareSide)),
            static_cast<int>(ceil(center.y + (l - sideSideOver2) * squareSide)),
            static_cast<int>(squareSide),
            static_cast<int>(squareSide)};
        SDL_RenderFillRect(renderer, &rect);
      }
    }
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
Point2D Manager::mousePositionToIndex(ImVec2& mousePos) {
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x/2, windowSize.y/2);
  float minDimension = std::min(windowSize.x, windowSize.y)*0.99f;
  auto lineColor = Color::LightGray;
  auto squareSide = minDimension/sideSize;
  auto sideSideOver2 = sideSize/2.0f;

  Vector2 relativePosFloat(mousePos.x - center.x, mousePos.y-center.y);

  relativePosFloat*=0.99f;
  relativePosFloat+=Vector2{minDimension/2, minDimension/2};
  relativePosFloat/=squareSide;

  return Point2D(relativePosFloat.x, relativePosFloat.y);
}
