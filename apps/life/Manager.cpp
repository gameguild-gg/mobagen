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

    // std::cout << "(" << index.x << "," << index.y << ")" << std::endl;

    if (lastIndexClicked != index) {
      lastIndexClicked = index;
      // std::cout << "MatrixPos: (" << index.x << "," << index.y << ")" << std::endl;
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

  // High-contrast palette: vivid live cells, dark dead ones, no borders -
  // cells are exactly sized so they tile edge to edge.
  const ImU32 liveFill = IM_COL32(120, 215, 60, 255);
  const ImU32 deadFill = IM_COL32(28, 28, 34, 255);

  if (rules[ruleId]->GetTileSet() == GameOfLifeTileSetEnum::Square) {
    for (int l = 0; l < sideSize; l++) {
      for (int c = 0; c < sideSize; c++) {
        bool alive = world.Get({c, l});
        float rx = std::ceil(cx + (c - sideSideOver2) * squareSide);
        float ry = std::ceil(cy + (l - sideSideOver2) * squareSide);
        dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + squareSide, ry + squareSide), alive ? liveFill : deadFill);
      }
    }
  } else if (rules[ruleId]->GetTileSet() == GameOfLifeTileSetEnum::Hexagon) {
    // True pointy-top hex tiling (interlocking, no gaps): vertices at top and
    // bottom, flat edges left and right. Row parity keeps the same odd-row
    // shift direction the mouse picking uses. The circumradius solves so the
    // grid fits the viewport on both axes (squareSide alone would draw 2x).
    const float sqrt3 = 1.7320508f;
    const float radius = std::min(minDimension / (sqrt3 * (sideSize + 0.5f)), minDimension / (1.5f * (sideSize - 1) + 2.0f));
    const float width = sqrt3 * radius;    // flat-to-flat, in-row pitch
    const float rowPitch = 1.5f * radius;  // distance between row centers
    const float startX = cx - (width * (sideSize + 0.5f)) * 0.5f + width * 0.5f;
    const float startY = cy - (rowPitch * (sideSize - 1) + 2.0f * radius) * 0.5f + radius;
    for (int l = 0; l < sideSize; l++) {
      float displacement = std::abs(l - (int)sideSideOver2) % 2 == 1 ? width * 0.5f : 0.0f;
      for (int c = 0; c < sideSize; c++) {
        bool alive = world.Get({c, l});
        float centerX = startX + c * width + displacement;
        float centerY = startY + l * rowPitch;
        ImVec2 points[6];
        for (int v = 0; v < 6; v++) {
          float angle = v * (3.14159265f / 3.0f) + 3.14159265f / 6.0f;  // pointy-top: vertices at 30,90,...,330 degrees
          points[v] = ImVec2(centerX + radius * std::cos(angle), centerY + radius * std::sin(angle));
        }
        dl->AddConvexPolyFilled(points, 6, alive ? liveFill : deadFill);
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
