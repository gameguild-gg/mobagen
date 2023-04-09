#include "Manager.h"
#include "engine/Engine.h"
#include "scene/GameObject.h"
#include <iostream>
#include "math/ColorT.h"
#include "Random.h"

Manager::Manager(Engine* pEngine) : GameObject(pEngine) {}

void Manager::Start() {
  Reset();
}

void Manager::OnDraw(SDL_Renderer* renderer){
  float minDimension = std::min(engine->window->size().x, engine->window->size().y);
  float cellSize = minDimension / sideSize;
  Vector2f center = {static_cast<float>(engine->window->size().x / 2), static_cast<float>(engine->window->size().y / 2)};
  for (int line = 0; line < sideSize; line++) {
    for (int column = 0; column < sideSize; column++) {
      SDL_Rect rect = {(int)(ceil(center.x + (column - sideSize / 2.0f) * cellSize)),
                       (int)(ceil(center.y + (-line - 1 + sideSize / 2.0f) * cellSize)), (int)cellSize - 1, (int)cellSize -1};
      Color32 color;
      switch (grid(column, line).type) {
        case SquareType::Empty:
          color = Color::DarkGray;
          break;
        case SquareType::Wall:
          color = Color::Yellow;
          break;
        case SquareType::Player:
          color = Color::Green;
          break;
        case SquareType::Enemy:
          color = Color::Red;
          break;
      }
      // if it is not visible, make it darker
      if(showHiddenObjects) {
        if (!grid(column, line).visible)
          color = color.Dark();
      } else {
        if (!grid(column, line).visible)
          color = Color::DarkGray;
      }

      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
      SDL_RenderFillRect(renderer, &rect);
    }
  }
}
Point2D Manager::screenSpaceToGridIndex(ImVec2& pos) {
  float minDimension = std::min(engine->window->size().x, engine->window->size().y);
  float cellSize = minDimension / sideSize;
  Vector2f center = {static_cast<float>(engine->window->size().x / 2), static_cast<float>(engine->window->size().y / 2)};
  return {(int)((pos.x - center.x) / cellSize + sideSize / 2.0f), (int)(-((pos.y - center.y) / cellSize) + sideSize / 2.0f)};
}

void Manager::OnGui(ImGuiContext* context) {
  ImGui::SetCurrentContext(context);
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowMinSize = {300,100};
  ImGui::Begin("Hide and Seek Squared");
  ImGui::Text("Click on the grid to change the type of the square");

  ImGui::Separator();

  // board size
  static auto newSize = sideSize;
  if (ImGui::SliderInt("Side Size", &newSize, 5, 256)) {
    newSize = (newSize / 4) * 4 + 1;
    if (newSize != sideSize) {
      sideSize = newSize;
      grid.Resize(newSize, newSize);
      Reset();
    }
  }

  // enemy tick size
  static auto newTickSize = enemyTickSize;
  if (ImGui::SliderFloat("Enemy Tick Size", &newTickSize, 0.1f, 1.0f))
    enemyTickSize = newTickSize;

  ImGui::Checkbox("Show Hidden Objects", &showHiddenObjects);

  ImGui::End();
}

void Manager::Update(float deltaTime) {
  auto mousePos = ImGui::GetMousePos();
  auto gridIndex = screenSpaceToGridIndex(mousePos);
  Point2D center = {engine->window->size().x/2, engine->window->size().y/2};
  auto minDimension = std::min(engine->window->size().x, engine->window->size().y);
  static Point2D lastDraggedGridIndex = {-1, -1};

  // if the mousePos distance from the center is less than the minDimension/2, then it is inside the grid
  if(abs(center.x-mousePos.x)<minDimension/2 && abs(center.y-mousePos.y)<minDimension/2){
    // if it is dragging the player from a square to a new one, then move the player
    if(ImGui::IsMouseDragging(0)) {
      if(lastDraggedGridIndex != Point2D(-1,-1)) {
        // if the last dragged index changes, then move the player
        if (lastDraggedGridIndex.x != gridIndex.x || lastDraggedGridIndex.y != gridIndex.y) {
          // if the player is in the grid, then move it
          if (grid(lastDraggedGridIndex.x, lastDraggedGridIndex.y).type == SquareType::Player
              && grid(gridIndex.x, gridIndex.y).type == SquareType::Empty) {
            grid(gridIndex.x, gridIndex.y).type = SquareType::Player;
            grid(lastDraggedGridIndex.x, lastDraggedGridIndex.y).type = SquareType::Empty;
          } else if (grid(lastDraggedGridIndex.x, lastDraggedGridIndex.y).type == SquareType::Enemy
                     && grid(gridIndex.x, gridIndex.y).type == SquareType::Empty) {
            grid(gridIndex.x, gridIndex.y).type = SquareType::Enemy;
            grid(lastDraggedGridIndex.x, lastDraggedGridIndex.y).type = SquareType::Empty;
          } else if (grid(gridIndex.x, gridIndex.y).type == SquareType::Wall) {
            grid(gridIndex.x, gridIndex.y).type = SquareType::Empty;
          } else if (grid(gridIndex.x, gridIndex.y).type == SquareType::Empty) {
            grid(gridIndex.x, gridIndex.y).type = SquareType::Wall;
          }
        }
      }
      // update the dragged index
      lastDraggedGridIndex = gridIndex;
    }
    else if(ImGui::IsMouseClicked(0)){
        if (grid(gridIndex.x, gridIndex.y).type == SquareType::Wall) {
          grid(gridIndex.x, gridIndex.y).type = SquareType::Empty;
        } else if (grid(gridIndex.x, gridIndex.y).type == SquareType::Empty) {
          grid(gridIndex.x, gridIndex.y).type = SquareType::Wall;
        }
      }
    else
      // reset the drag state
      lastDraggedGridIndex = {-1, -1};
  }

  // enemy tick
  timeTimeRemaining -= deltaTime;
  if(timeTimeRemaining<0){
      timeTimeRemaining += enemyTickSize;
      EnemyTick();
  }

  // update the visibility of the squares
  ShadowCast();
}
void Manager::Reset() {
  // resize the grid
  grid.Resize(sideSize, sideSize);

  // find player and enemy in the grid
  Point2D playerPosition = {-1, -1};  // -1, -1 means not found
  Point2D enemyPosition = {-1, -1};   // -1, -1 means not found
  for (int line = 0; line < sideSize; line++) {
    for (int column = 0; column < sideSize; column++) {
      if (grid(column, line).type == SquareType::Player) {
        playerPosition = {column, line};
      }
      if (grid(column, line).type == SquareType::Enemy) {
        enemyPosition = {column, line};
      }
    }
  }
  // reset player position to the center of the grid
  if (playerPosition.x == -1 && playerPosition.y == -1) {
    grid(sideSize / 2, sideSize / 2).type = SquareType::Player;
  } else {
    grid(playerPosition.x, playerPosition.y).type = SquareType::Empty;
    grid(sideSize / 2, sideSize / 2).type = SquareType::Player;
  }
  // reset enemy position to random position not on the player
  if (enemyPosition.x == -1 && enemyPosition.y == -1) {
    int enemyX = Random::Range(0, sideSize - 1);
    int enemyY = Random::Range(0, sideSize - 1);
    while (enemyX == sideSize / 2 && enemyY == sideSize / 2) {
      enemyX = Random::Range(0, sideSize - 1);
      enemyY = Random::Range(0, sideSize - 1);
    }
    grid(enemyX, enemyY).type = SquareType::Enemy;
  } else {
    grid(enemyPosition.x, enemyPosition.y).type = SquareType::Empty;
    int enemyX = Random::Range(0, sideSize - 1);
    int enemyY = Random::Range(0, sideSize - 1);
    while (enemyX == sideSize / 2 && enemyY == sideSize / 2) {
      enemyX = Random::Range(0, sideSize - 1);
      enemyY = Random::Range(0, sideSize - 1);
    }
    grid(enemyX, enemyY).type = SquareType::Enemy;
  }
}
void Manager::EnemyTick() {
  // todo: optionally implement the enemy tick
  std::cout << "Enemy Tick" << std::endl;
}
void Manager::ShadowCast() {
  // todo: implement the shadow cast
  // change the variable visible in the grid to true or false depending on the visibility from the player
  // ex.: grid(i,j).visible = true;
  // The easiest way to implement is to follow this tutorial: https://www.albertford.com/shadowcasting/
  // But you can the algorithm following this tutorial to follow raycast or use polygons to do the shadow cast: https://www.redblobgames.com/articles/visibility/

  Point2D playerPosition, enemyPosition;
  // reset the visibility of all the squares and find the player position and enemy position
  for(int line = 0; line < sideSize; line++) {
    for (int column = 0; column < sideSize; column++) {
      grid(column, line).visible = false;
      if (grid(column, line).type == SquareType::Player) {
        playerPosition = {column, line};
        grid(column, line).visible = true;
      }
      if (grid(column, line).type == SquareType::Enemy) {
        enemyPosition = {column, line};
      }
    }
  }

  // dummy implementation. every square is visible
  for(int line = 0; line < sideSize; line++) {
    for (int column = 0; column < sideSize; column++) {
      grid(column, line).visible = true;
    }
  }
}
