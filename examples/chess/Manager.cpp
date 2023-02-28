#include "Manager.h"
#include "ColorT.h"
#include "Point2D.h"

void Manager::OnGui(ImGuiContext* context) {
  ImGui::SetCurrentContext(context);

  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS",
              ImGui::GetIO().DeltaTime * 1000,
              1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate,
              ImGui::GetIO().Framerate);

  ImGui::End(); // end settings

//  static Point2D lastIndexClicked = {INT32_MAX, INT32_MAX};
//  if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
//    auto mousePos = ImGui::GetMousePos();
//    Point2D index;
//    if(rules[ruleId]->GetTileSet()==GameOfLifeTileSetEnum::Square)
//      index = mousePositionToIndex(mousePos);
//    else if(rules[ruleId]->GetTileSet()==GameOfLifeTileSetEnum::Hexagon) {
//      auto windowSize = engine->window->size();
//      auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
//      float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
//      auto squareSide = minDimension / sideSize;
//      auto sideSideOver2 = sideSize / 2.0f;
//      index = mousePositionToIndex(mousePos);
//      float displacement = abs(index.y-(int)sideSideOver2)%2 == 1 ? squareSide/2 : 0;
//      mousePos.x-=displacement;
//      index = mousePositionToIndex(mousePos);
//    }
//
//    std::cout << index.to_string() << std::endl;
//
//    if(lastIndexClicked!=index) {
//      lastIndexClicked = index;
//      std::cout << "MatrixPos: " << index.to_string() << std::endl;
//      if (index.x >= 0 && index.x < sideSize && index.y >= 0 && index.y < sideSize) {
//        world.SetCurrent(index, !world.Get(index)); // to be visible
//        world.SetNext(index, !world.Get(index)); // to be used next time
//      }
//    }
//  } if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
//    lastIndexClicked = {INT32_MAX, INT32_MAX};
//  }
}

void Manager::OnDraw(SDL_Renderer* renderer){
auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
  float minDimension = std::min(windowSize.x, windowSize.y) * 0.99f;
  auto squareSide = minDimension / 8;
  auto sideSideOver2 = 8 / 2.0f;

  auto whiteCell = Color::LightBlue.Light();
  auto blackCell = Color::DarkBlue.Dark();
  for(int line=0;line<8; line++) {
    for(int column=0; column<8; column++) {
      if((line+column)%2==0)
        SDL_SetRenderDrawColor(renderer, blackCell.r, blackCell.g, blackCell.b, SDL_ALPHA_OPAQUE);
      else
        SDL_SetRenderDrawColor(renderer, whiteCell.r, whiteCell.g, whiteCell.b, SDL_ALPHA_OPAQUE);

      SDL_Rect rect = {
            static_cast<int>(ceil(center.x + (column - sideSideOver2) * squareSide)),
            static_cast<int>(ceil(center.y + (-line -1 + sideSideOver2) * squareSide)),
            static_cast<int>(squareSide),
            static_cast<int>(squareSide)};
        SDL_RenderFillRect(renderer, &rect);
    }
  }
}