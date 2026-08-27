#include "World.h"
#include "generators/HuntAndKillExample.h"
#include "generators/RecursiveBacktrackerExample.h"
#include "generators/PrimExample.h"
#include <algorithm>
#include <chrono>

// Dark gray background color for unvisited cells (169, 169, 169)
static const Color32 kDarkGray = {169.0f / 255.0f, 169.0f / 255.0f, 169.0f / 255.0f, 1.0f};

World::World(int size) : sideSize(size) {
  generators.push_back(new PrimExample());
  generators.push_back(new RecursiveBacktrackerExample());
  generators.push_back(new HuntAndKillExample());
}

World::~World() {
  for (auto g : generators) delete g;
  generators.clear();
}

Node World::GetNode(const Point2D& point) {
  auto index = Point2DtoIndex(point);
  // todo: not tested!!
  return {data[index], data[index + 3], data[index + (sideSize + 1) * 2], data[index + 1]};
}

bool World::GetNorth(const Point2D& point) { return data[Point2DtoIndex(point)]; }

bool World::GetEast(const Point2D& point) { return data[Point2DtoIndex(point) + 3]; }

bool World::GetSouth(const Point2D& point) { return data[Point2DtoIndex(point) + (sideSize + 1) * 2]; }

bool World::GetWest(const Point2D& point) { return data[Point2DtoIndex(point) + 1]; }

void World::SetNode(const Point2D& point, const Node& node) {
  data[Point2DtoIndex(point)] = node.GetNorth();
  data[Point2DtoIndex(point) + 3] = node.GetEast();
  data[Point2DtoIndex(point) + (sideSize + 1) * 2] = node.GetSouth();
  data[Point2DtoIndex(point) + 1] = node.GetWest();
}
void World::SetNorth(const Point2D& point, const bool& state) { data[Point2DtoIndex(point)] = state; }
void World::SetEast(const Point2D& point, const bool& state) { data[Point2DtoIndex(point) + 3] = state; }
void World::SetSouth(const Point2D& point, const bool& state) { data[Point2DtoIndex(point) + (sideSize + 1) * 2] = state; }
void World::SetWest(const Point2D& point, const bool& state) { data[Point2DtoIndex(point) + 1] = state; }

void World::Start() { this->Clear(); }

void World::OnGui() {
  float deltaTime = ImGui::GetIO().DeltaTime;
  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", ImGui::GetIO().DeltaTime * 1000, 1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
  static auto newSize = sideSize;

  if (ImGui::SliderInt("Side Size", &newSize, 5, 29)) {
    newSize = (newSize / 4) * 4 + 1;
    if (newSize != sideSize) {
      sideSize = newSize;
      Clear();
    }
  }

  ImGui::Text("Simulation");
  if (ImGui::Button("Step")) {
    isSimulating = false;
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
  ImGui::SameLine();
  if (ImGui::Button("RESET")) {
    Clear();
  }
  ImGui::Text("Move duration: %lli", moveDuration);
  ImGui::Text("Total duration: %lli", totalTime);
  ImGui::SliderFloat("Turn Duration", &timeBetweenAITicks, 0.00, 30);
  ImGui::Text("Next turn in %.1f", timeForNextTick);

  ImGui::Text("Generator: %s", generators[generatorId]->GetName().c_str());
  if (ImGui::BeginCombo("##combo",
                        generators[generatorId]->GetName().c_str()))  // The second parameter is the label previewed before opening the combo.
  {
    for (int n = 0; n < (int)generators.size(); n++) {
      bool is_selected = (generators[generatorId]->GetName()
                          == generators[n]->GetName());  // You can store your selection however you want, outside or inside your objects
      if (ImGui::Selectable(generators[n]->GetName().c_str(), is_selected)) {
        generatorId = n;
        Clear();
      }
      if (is_selected)
        ImGui::SetItemDefaultFocus();  // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
    }
    ImGui::EndCombo();
  }
  ImGui::End();
  (void)deltaTime;
}

void World::OnDraw() {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  float winW = ImGui::GetIO().DisplaySize.x;
  float winH = ImGui::GetIO().DisplaySize.y;

  float linesize = (std::min(winW, winH) / (float)sideSize) * 0.9f;
  float dispX = (winW / 2.0f) - linesize * (sideSize / 2.0f) - linesize / 2.0f;
  float dispY = (winH / 2.0f) - linesize * (sideSize / 2.0f) - linesize / 2.0f;

  const ImU32 wallColor = IM_COL32(255, 255, 255, 255);

  // Draw walls: each pair (data[i] = north wall, data[i+1] = west wall)
  for (int i = 0; i < (int)data.size(); i += 2) {
    float px = (float)((i / 2) % (sideSize + 1)) * linesize + dispX;
    float py = (float)((i / 2) / (sideSize + 1)) * linesize + dispY;

    // north (horizontal line at top of cell)
    if (data[i]) dl->AddLine(ImVec2(px, py), ImVec2(px + linesize, py), wallColor);
    // west (vertical line at left of cell)
    if (data[i + 1]) dl->AddLine(ImVec2(px, py), ImVec2(px, py + linesize), wallColor);
  }

  // Draw cell background colors
  for (int i = 0; i < sideSize * sideSize; i++) {
    const auto& c = colors[i];
    ImU32 cellColor
        = IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f), static_cast<int>(c.a * 255.0f));

    float px = (float)(i % sideSize) * linesize + dispX;
    float py = (float)(i / sideSize) * linesize + dispY;
    dl->AddRectFilled(ImVec2(px + 1.0f, py + 1.0f), ImVec2(px + linesize, py + linesize), cellColor);
  }
}

void World::Update(float deltaTime) {
  if (isSimulating) {
    // update timer
    timeForNextTick -= deltaTime;
    if (timeForNextTick < 0) {
      step();
      timeForNextTick = timeBetweenAITicks;
    }
  }
}

void World::Clear() {
  // stop simulation
  isSimulating = false;

  // clear all the data
  data.clear();
  data.resize((size_t)(sideSize + 1) * (sideSize + 1) * 2);
  for (int i = 0; i < (int)data.size(); ++i) {
    if (i % ((sideSize + 1) * 2) == (sideSize + 1) * 2 - 2 ||  // remove north elements on the last column
        (i / ((sideSize + 1) * 2) == sideSize && i % 2 == 1))  // remove west elements on the last line
      data[i] = false;
    else
      data[i] = true;
  }

  // clear the color of the boxes;
  colors.clear();
  colors.resize(sideSize * sideSize);
  for (int i = 0; i < sideSize * sideSize; i++) colors[i] = kDarkGray;

  // clear maze generators
  for (int i = 0; i < (int)generators.size(); i++) generators[i]->Clear(this);

  // reset timers;
  totalTime = 0;
  moveDuration = 0;
}

void World::step() {
  auto start = std::chrono::high_resolution_clock::now();
  if (!generators[generatorId]->Step(this)) {
    isSimulating = false;
  }
  auto stop = std::chrono::high_resolution_clock::now();
  moveDuration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
  totalTime += moveDuration;
}

void World::SetNodeColor(const Point2D& node, const Color32& color) { colors[(node.y + sideSize / 2) * sideSize + node.x + sideSize / 2] = color; }

Color32 World::GetNodeColor(const Point2D& node) { return colors[(node.y + sideSize / 2) * sideSize + node.x + sideSize / 2]; }

int World::GetSize() const { return sideSize; }
