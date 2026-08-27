#include "Manager.h"
#include "imgui.h"
#include "Random.h"
#include <iostream>
#include <algorithm>
#include <cmath>

void Manager::Start() { Reset(); }

void Manager::OnDraw() {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImVec2 display = ImGui::GetIO().DisplaySize;
  float minDimension = std::min(display.x, display.y);
  float cellSize = minDimension / sideSize;
  float cx = display.x / 2.0f;
  float cy = display.y / 2.0f;
  float cs = static_cast<float>(static_cast<int>(cellSize) - 1);  // rendered cell size

  // Halve each RGB channel, preserve full alpha
  auto darken = [](ImU32 c) -> ImU32 { return IM_COL32((c & 0xFFu) / 2, ((c >> 8) & 0xFFu) / 2, ((c >> 16) & 0xFFu) / 2, 0xFF); };

  for (int line = 0; line < sideSize; line++) {
    for (int column = 0; column < sideSize; column++) {
      float rx = std::ceil(cx + (column - sideSize / 2.0f) * cellSize);
      float ry = std::ceil(cy + (-line - 1 + sideSize / 2.0f) * cellSize);

      ImU32 color;
      switch (grid(column, line).type) {
        case SquareType::Empty:
          color = IM_COL32(64, 64, 64, 255);
          break;
        case SquareType::Wall:
          color = IM_COL32(255, 255, 0, 255);
          break;
        case SquareType::Player:
          color = IM_COL32(0, 200, 0, 255);
          break;
        case SquareType::Enemy:
          color = IM_COL32(200, 0, 0, 255);
          break;
        default:
          color = IM_COL32(0, 0, 0, 255);
          break;
      }

      // dim tiles not visible from the player
      if (showHiddenObjects) {
        if (!grid(column, line).visible) color = darken(color);
      } else {
        if (!grid(column, line).visible) color = IM_COL32(64, 64, 64, 255);
      }

      dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + cs, ry + cs), color);
    }
  }
}

glm::ivec2 Manager::screenSpaceToGridIndex(ImVec2& pos) {
  ImVec2 display = ImGui::GetIO().DisplaySize;
  float minDimension = std::min(display.x, display.y);
  float cellSize = minDimension / sideSize;
  float cx = display.x / 2.0f;
  float cy = display.y / 2.0f;
  return {static_cast<int>((pos.x - cx) / cellSize + sideSize / 2.0f), static_cast<int>(-((pos.y - cy) / cellSize) + sideSize / 2.0f)};
}

void Manager::OnGui() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowMinSize = {300, 100};
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
  if (ImGui::SliderFloat("Enemy Tick Size", &newTickSize, 0.1f, 1.0f)) enemyTickSize = newTickSize;

  ImGui::Checkbox("Show Hidden Objects", &showHiddenObjects);

  ImGui::End();
}

void Manager::Update(float deltaTime) {
  ImVec2 mousePos = ImGui::GetMousePos();
  glm::ivec2 gridIndex = screenSpaceToGridIndex(mousePos);
  ImVec2 display = ImGui::GetIO().DisplaySize;
  float cx = display.x / 2.0f;
  float cy = display.y / 2.0f;
  float minDimension = std::min(display.x, display.y);
  static glm::ivec2 lastDraggedGridIndex = {-1, -1};

  // only interact when cursor is over the grid area
  if (std::abs(cx - mousePos.x) < minDimension / 2.0f && std::abs(cy - mousePos.y) < minDimension / 2.0f) {
    if (ImGui::IsMouseDragging(0)) {
      if (lastDraggedGridIndex != glm::ivec2(-1, -1)) {
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
    } else if (ImGui::IsMouseClicked(0)) {
      if (grid(gridIndex.x, gridIndex.y).type == SquareType::Wall) {
        grid(gridIndex.x, gridIndex.y).type = SquareType::Empty;
      } else if (grid(gridIndex.x, gridIndex.y).type == SquareType::Empty) {
        grid(gridIndex.x, gridIndex.y).type = SquareType::Wall;
      }
    } else {
      // reset the drag state
      lastDraggedGridIndex = {-1, -1};
    }
  }

  // enemy tick
  timeTimeRemaining -= deltaTime;
  if (timeTimeRemaining < 0) {
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
  glm::ivec2 playerPosition = {-1, -1};  // -1, -1 means not found
  glm::ivec2 enemyPosition = {-1, -1};   // -1, -1 means not found
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
  // But you can use the algorithm following this tutorial to follow raycast or use polygons to do the shadow cast:
  // https://www.redblobgames.com/articles/visibility/

  glm::ivec2 playerPosition, enemyPosition;
  // reset the visibility of all the squares and find the player position and enemy position
  for (int line = 0; line < sideSize; line++) {
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

  // dummy implementation: every square is visible
  for (int line = 0; line < sideSize; line++) {
    for (int column = 0; column < sideSize; column++) {
      grid(column, line).visible = true;
    }
  }
}
