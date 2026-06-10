#include "Manager.h"
#include "math/Point2D.h"
#include "generators/ParticleGenerator.h"
#include "generators/RandomGenerator.h"
#include "engine/Engine.h"

#include <chrono>

Manager::Manager(Engine* engine, int size) : GameObject(engine) {
  // todo: add your generator
  generators.push_back(new ParticleGenerator());
  generators.push_back(new RandomScenarioGenerator());
}

void Manager::SetPixels(std::vector<Color32>& input) {
  if (!texture) return;
  const int w = (int)texture->Width();
  const int h = (int)texture->Height();
  std::vector<uint32_t> packed(w * h);
  for (int line = 0; line < h; line++) {
    for (int column = 0; column < w; column++) {
      packed[line * w + column] = input[line * w + column].GetPacked();
    }
  }
  texture->Upload(packed.data());
}
void Manager::OnDraw(Renderer2D& r) {
  if (!texture) return;
  auto windowSize = engine->window->size();
  auto center = Point2D(windowSize.x / 2, windowSize.y / 2);
  int minDimension = std::min(windowSize.x, windowSize.y);
  Rect2D dst = {(float)(center.x - minDimension / 2),
                (float)(center.y - minDimension / 2),
                (float)minDimension, (float)minDimension};
  r.DrawTexture(*texture, dst);
}
Manager::~Manager() {
  delete texture;
  texture = nullptr;
}
void Manager::Start() {
  texture = Texture::CreateStreaming(sideSize, sideSize);
}
void Manager::OnGui(ImGuiContext* context) {
  ImGui::SetCurrentContext(context);
  float deltaTime = ImGui::GetIO().DeltaTime;

  ImGui::Begin("Settings", nullptr);
  ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", ImGui::GetIO().DeltaTime * 1000, 1.0f / ImGui::GetIO().DeltaTime,
              1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
  static auto newSize = sideSize;

  if (ImGui::SliderInt("Side Size", &newSize, 5, 2048)) {
    // newSize = (newSize/4)*4 + 1;
    if (newSize != sideSize) {
      sideSize = newSize;
      Clear();
    }
  }

  ImGui::Text("Generator: %s", generators[generatorId]->GetName().c_str());
  if (ImGui::BeginCombo("##combo",
                        generators[generatorId]->GetName().c_str()))  // The second parameter is the label previewed before opening the combo.
  {
    for (int n = 0; n < generators.size(); n++) {
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
void Manager::Clear() {
  delete texture;
  texture = Texture::CreateStreaming(sideSize, sideSize);
}
int Manager::GetSize() const { return sideSize; }
void Manager::step() {
  //  auto start = std::chrono::high_resolution_clock::now();
  auto pixels = generators[generatorId]->Generate(sideSize, accumulatedTime);
  //  auto step = std::chrono::high_resolution_clock::now();
  SetPixels(pixels);
  //  auto end = std::chrono::high_resolution_clock::now();
}
