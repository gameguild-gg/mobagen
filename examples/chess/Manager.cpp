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

  static Point2D lastIndexClicked = {INT32_MIN, INT32_MIN};
  if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
    auto mousePos = ImGui::GetMousePos();
    Point2D index = mousePositionToIndex(mousePos);

    if(lastIndexClicked!=index) {
      lastIndexClicked = index;
      std::cout << "MatrixPos: " << index.to_string() << std::endl;
      selected = index;

      validMoves =

      // if the user clicks on a valid move, move it!
      // if the user clicks on the current element, or other element not valid, unselect it
    }
  } if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
    lastIndexClicked = {INT32_MIN, INT32_MIN};
  }
}

Point2D Manager::mousePositionToIndex(ImVec2& pos) {
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x/2, windowSize.y/2);
  float minDimension = std::min(windowSize.x, windowSize.y)*0.99f;
  auto lineColor = Color::LightGray;
  auto squareSide = minDimension/8;
  auto sideSideOver2 = 8/2.0f;

  Vector2 relativePosFloat(pos.x - center.x, pos.y-center.y);

  relativePosFloat*=0.99f;
  relativePosFloat+=Vector2{minDimension/2, minDimension/2};
  relativePosFloat/=squareSide;

  return Point2D(relativePosFloat.x, 8-relativePosFloat.y);
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