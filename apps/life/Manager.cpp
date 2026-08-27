#include "Manager.h"
#include "rules/JohnConway.h"
#include "rules/HexagonGameOfLife.h"
#include <iostream>
#include <cmath>

Manager::Manager() {
  world.Resize(sideSize);
  rules.push_back(new HexagonGameOfLife());
  rules.push_back(new JohnConway());
}

void Manager::Start() {}

void Manager::OnGui() {
  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", ImGui::GetIO().DeltaTime * 1000, 1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

  static auto newSize = sideSize;
  if (ImGui::SliderInt("Side Size", &newSize, 5, 256)) {
    newSize = (newSize / 4) * 4 + 1;
    if (newSize != sideSize) {
      sideSize = newSize;
      world.Resize(newSize);
    }
  }

  ImGui::Text("Generator: %s", rules[ruleId]->GetName().c_str());
  if (ImGui::BeginCombo("##combo", rules[ruleId]->GetName().c_str())) {
    for (int n = 0; n < (int)rules.size(); n++) {
      bool is_selected = (rules[ruleId]->GetName() == rules[n]->GetName());
      if (ImGui::Selectable(rules[n]->GetName().c_str(), is_selected)) {
        ruleId = n;
        clear();
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Text("Simulation");
  if (ImGui::Button("Step")) {
    isSimulating = false;
    accumulatedTime += ImGui::GetIO().DeltaTime;
    step();
  }
  ImGui::SameLine();
  if (ImGui::Button("Start")) {
    isSimulating = true;
  }

  ImGui::SameLine();
  if (ImGui::Button("Pause")) {
    isSimulating = false;
  }

  ImGui::Text("TimeToNextStep: %.3f", (timeBetweenSteps - accumulatedTime));
  static auto newTime = timeBetweenSteps;
  if (ImGui::SliderFloat("Time Between Steps", &newTime, 0.0001f, 1.0f)) {
    if (newTime != timeBetweenSteps) timeBetweenSteps = newTime;
  }

  if (ImGui::Button("Randomize")) {
    isSimulating = false;
    world.Randomize();
  }

  ImGui::End();  // end settings

  static glm::ivec2 lastIndexClicked = {INT32_MAX, INT32_MAX};
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    auto mousePos = ImGui::GetMousePos();
    glm::ivec2 index;
    if (rules[ruleId]->GetTileSet() == GameOfLifeTileSetEnum::Square) {
      index = mousePositionToIndex(mousePos);
    } else if (rules[ruleId]->GetTileSet() == GameOfLifeTileSetEnum::Hexagon) {
      ImVec2 winSize = ImGui::GetIO().DisplaySize;
      float minDimension = std::min(winSize.x, winSize.y) * 0.99f;
      float squareSide = minDimension / sideSize;
      float sideSideOver2 = sideSize / 2.0f;
      index = mousePositionToIndex(mousePos);
      float displacement = std::abs(index.y - (int)sideSideOver2) % 2 == 1 ? squareSide / 2.0f : 0.0f;
      mousePos.x -= displacement;
      index = mousePositionToIndex(mousePos);
    }

    std::cout << "(" << index.x << "," << index.y << ")" << std::endl;

    if (lastIndexClicked != index) {
      lastIndexClicked = index;
      std::cout << "MatrixPos: (" << index.x << "," << index.y << ")" << std::endl;
      if (index.x >= 0 && index.x < sideSize && index.y >= 0 && index.y < sideSize) {
        world.SetCurrent(index, !world.Get(index));  // to be visible
        world.SetNext(index, !world.Get(index));     // to be used next time
      }
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    lastIndexClicked = {INT32_MAX, INT32_MAX};
  }
}

void Manager::OnDraw() {
  if (rules[ruleId]->GetTileSet() == GameOfLifeTileSetEnum::None) {
    std::cout << "your rule should explicitly say which board you want to use";
    return;
  }

  auto* dl = ImGui::GetBackgroundDrawList();
  ImVec2 winSize = ImGui::GetIO().DisplaySize;
  float cx = winSize.x * 0.5f;
  float cy = winSize.y * 0.5f;
  float minDimension = std::min(winSize.x, winSize.y) * 0.99f;
  float squareSide = minDimension / sideSize;
  float sideSideOver2 = sideSize / 2.0f;

  const ImU32 liveColor = IM_COL32(180, 180, 0, 255);
  const ImU32 emptyColor = IM_COL32(20, 20, 20, 255);
  const ImU32 lineColor = IM_COL32(50, 50, 50, 10);

  if (rules[ruleId]->GetTileSet() == GameOfLifeTileSetEnum::Square) {
    // Draw cells
    for (int l = 0; l < sideSize; l++) {
      for (int c = 0; c < sideSize; c++) {
        ImU32 color = world.Get({c, l}) ? liveColor : emptyColor;
        float rx = std::ceil(cx + (c - sideSideOver2) * squareSide);
        float ry = std::ceil(cy + (l - sideSideOver2) * squareSide);
        dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + squareSide, ry + squareSide), color);
      }
    }

    // Draw grid lines (only when grid is small enough, or for borders)
    for (int i = 0; i <= sideSize; i++) {
      if (sideSize < 50 || i == 0 || i == sideSize) {
        float offset = (i - sideSideOver2) * squareSide;
        dl->AddLine(ImVec2(cx - minDimension / 2.0f, cy - offset), ImVec2(cx + minDimension / 2.0f, cy - offset), lineColor);
        dl->AddLine(ImVec2(cx - offset, cy - minDimension / 2.0f), ImVec2(cx - offset, cy + minDimension / 2.0f), lineColor);
      }
    }
  } else if (rules[ruleId]->GetTileSet() == GameOfLifeTileSetEnum::Hexagon) {
    // Draw cells with per-row horizontal displacement for hex layout
    for (int l = 0; l < sideSize; l++) {
      float displacement = std::abs(l - (int)sideSideOver2) % 2 == 1 ? squareSide / 2.0f : 0.0f;
      for (int c = 0; c < sideSize; c++) {
        ImU32 color = world.Get({c, l}) ? liveColor : emptyColor;
        float rx = std::ceil(cx + displacement + (c - sideSideOver2) * squareSide);
        float ry = std::ceil(cy + (l - sideSideOver2) * squareSide);
        dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + squareSide, ry + squareSide), color);
      }
    }
  }
}

void Manager::Update(float deltaTime) {
  if (isSimulating) {
    accumulatedTime += deltaTime;
    if (accumulatedTime > timeBetweenSteps) {
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
  for (auto x : rules) delete x;
  rules.clear();
}

void Manager::clear() {
  isSimulating = false;
  world.Resize(sideSize);
}

glm::ivec2 Manager::mousePositionToIndex(ImVec2& mousePos) {
  ImVec2 winSize = ImGui::GetIO().DisplaySize;
  float cx = winSize.x * 0.5f;
  float cy = winSize.y * 0.5f;
  float minDimension = std::min(winSize.x, winSize.y) * 0.99f;
  float squareSide = minDimension / sideSize;

  glm::vec2 rel(mousePos.x - cx, mousePos.y - cy);
  rel *= 0.99f;
  rel += glm::vec2(minDimension / 2.0f, minDimension / 2.0f);
  rel /= squareSide;

  return glm::ivec2((int)rel.x, (int)rel.y);
}
