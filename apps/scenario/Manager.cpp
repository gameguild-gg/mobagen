#include "Manager.h"
#include "generators/ParticleGenerator.h"
#include "generators/RandomGenerator.h"

#include <algorithm>

Manager::Manager() {
  // todo: add your generator
  generators.push_back(new ParticleGenerator());
  generators.push_back(new RandomScenarioGenerator());
  pixels.assign(static_cast<size_t>(sideSize) * sideSize, Color32(0, 0, 0, 1));
}

Manager::~Manager() {
  for (auto* g : generators) delete g;
  generators.clear();
}

void Manager::Start() { pixels.assign(static_cast<size_t>(sideSize) * sideSize, Color32(0, 0, 0, 1)); }

void Manager::SetPixels(std::vector<Color32>& input) { pixels = input; }

void Manager::OnDraw() {
  if (pixels.empty()) return;

  auto* dl = ImGui::GetBackgroundDrawList();
  const ImVec2 winSize = ImGui::GetIO().DisplaySize;
  const float minDim = std::min(winSize.x, winSize.y);
  const float startX = (winSize.x - minDim) * 0.5f;
  const float startY = (winSize.y - minDim) * 0.5f;
  const float pixelSize = minDim / static_cast<float>(sideSize);

  for (int y = 0; y < sideSize; y++) {
    for (int x = 0; x < sideSize; x++) {
      const Color32& c = pixels[static_cast<size_t>(y) * sideSize + x];
      auto clamp255 = [](float f) -> int { return f < 0.f ? 0 : (f > 1.f ? 255 : static_cast<int>(f * 255.f)); };
      ImU32 col = IM_COL32(clamp255(c.r), clamp255(c.g), clamp255(c.b), clamp255(c.a));
      const float px = startX + static_cast<float>(x) * pixelSize;
      const float py = startY + static_cast<float>(y) * pixelSize;
      // +0.5f overlap removes hairline gaps between adjacent cells
      dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pixelSize + 0.5f, py + pixelSize + 0.5f), col);
    }
  }
}

void Manager::OnGui() {
  const float deltaTime = ImGui::GetIO().DeltaTime;

  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", deltaTime * 1000, 1.0f / deltaTime, 1000.0f / ImGui::GetIO().Framerate,
              ImGui::GetIO().Framerate);

  static int newSize = sideSize;
  if (ImGui::SliderInt("Side Size", &newSize, 5, 512)) {
    if (newSize != sideSize) {
      sideSize = newSize;
      Clear();
    }
  }

  ImGui::Text("Generator: %s", generators[generatorId]->GetName().c_str());
  if (ImGui::BeginCombo("##combo", generators[generatorId]->GetName().c_str())) {
    for (int n = 0; n < static_cast<int>(generators.size()); n++) {
      bool is_selected = (generators[generatorId]->GetName() == generators[n]->GetName());
      if (ImGui::Selectable(generators[n]->GetName().c_str(), is_selected)) {
        generatorId = n;
        Clear();
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (ImGui::Button("Generate")) {
    accumulatedTime += deltaTime;
    step();
  }

  ImGui::Text("Simulation");
  if (ImGui::Button("Step")) {
    isSimulating = false;
    accumulatedTime += deltaTime;
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

  ImGui::End();
}

void Manager::Update(float deltaTime) {
  if (isSimulating) {
    accumulatedTime += deltaTime;
    step();
  }
}

void Manager::Clear() { pixels.assign(static_cast<size_t>(sideSize) * sideSize, Color32(0, 0, 0, 1)); }

int Manager::GetSize() const { return sideSize; }

void Manager::step() {
  auto px = generators[generatorId]->Generate(sideSize, accumulatedTime);
  SetPixels(px);
}
