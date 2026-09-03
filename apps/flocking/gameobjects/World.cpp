#include "World.h"
#include "imgui.h"
#include "../utils/ImGuiExtra.h"
#include "Random.h"

#include "../behaviours/SeparationRule.h"
#include "../behaviours/CohesionRule.h"
#include "../behaviours/AlignmentRule.h"
#include "../behaviours/MouseInfluenceRule.h"
#include "../behaviours/BoundedAreaRule.h"
#include "../behaviours/WindRule.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#  include "Windows.h"
#  include "Psapi.h"
#endif

FlockingManager::FlockingManager(ecs::World& world, jobs::Scheduler& sched) : ecs_(world), sched_(sched) {}

void FlockingManager::initializeRules() {
  boidsRules.emplace_back(std::make_unique<SeparationRule>(25.f, 90.f));
  boidsRules.emplace_back(std::make_unique<CohesionRule>(60.f));
  boidsRules.emplace_back(std::make_unique<AlignmentRule>(2.9f));
  boidsRules.emplace_back(std::make_unique<MouseInfluenceRule>(2.f));
  boidsRules.emplace_back(std::make_unique<BoundedAreaRule>(20, 8.f, false));
  boidsRules.emplace_back(std::make_unique<WindRule>(1.f, 6.f, false));

  defaultWeights.clear();
  for (const auto& rule : boidsRules) defaultWeights.push_back(rule->weight);

  SetupImGuiStyle();
}

void FlockingManager::randomizeBoidPosVel(ecs::Entity e) {
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  float w = displaySize.x > 0.f ? displaySize.x : 1280.f;
  float h = displaySize.y > 0.f ? displaySize.y : 800.f;

  ecs_.get<BoidPos>(e).pos.x = Random::Range(0.f, w);
  ecs_.get<BoidPos>(e).pos.y = Random::Range(0.f, h);
  float angle = Random::Range(0.f, 6.28318530718f);
  ecs_.get<BoidVel>(e).vel = glm::vec2(std::cos(angle), std::sin(angle)) * desiredSpeed;
}

ecs::Entity FlockingManager::createBoid() {
  ecs::Entity e = ecs_.create();
  ecs_.add<BoidPos>(e);
  ecs_.add<BoidVel>(e);
  ecs_.add<BoidAcc>(e);
  ecs_.add<BoidForceCache>(e);

  BoidConfig& cfg = ecs_.add<BoidConfig>(e);
  cfg.detectionRadius = detectionRadius;
  cfg.speed = desiredSpeed;
  cfg.hasConstantSpeed = hasConstantSpeed;
  cfg.maxAcceleration = hasMaxAcceleration ? maxAcceleration : 10000.f;

  BoidDebug& dbg = ecs_.add<BoidDebug>(e);
  dbg.drawDebugRadius = showRadius;
  dbg.drawDebugRules = showRules;
  dbg.drawAcceleration = showAcceleration;
  dbg.color = Color32::RandomColor(31, 255);

  randomizeBoidPosVel(e);
  return e;
}

void FlockingManager::setNumberOfBoids(int number) {
  int diff = static_cast<int>(boidEntities.size()) - number;
  if (diff == 0) return;

  if (diff < 0) {
    for (int i = 0; i < -diff; i++) boidEntities.push_back(createBoid());
  } else {
    for (int i = 0; i < diff; i++) {
      ecs_.destroy(boidEntities.back());
      boidEntities.pop_back();
    }
  }
}

void FlockingManager::warpIfOutOfBounds(BoidPos& p) {
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  float w = displaySize.x > 0.f ? displaySize.x : 1280.f;
  float h = displaySize.y > 0.f ? displaySize.y : 800.f;

  if (p.pos.x < 0.f)
    p.pos.x += w;
  else if (p.pos.x > w)
    p.pos.x -= w;
  if (p.pos.y < 0.f)
    p.pos.y += h;
  else if (p.pos.y > h)
    p.pos.y -= h;
}

void FlockingManager::Start() {
  initializeRules();
  setNumberOfBoids(nbBoids);
}

void FlockingManager::Update(float deltaTime) {
  const int n = static_cast<int>(boidEntities.size());
  if (n == 0) return;

  std::vector<BoidView> snapshot(n);
  for (int i = 0; i < n; i++) {
    snapshot[i].position = ecs_.get<BoidPos>(boidEntities[i]).pos;
    snapshot[i].velocity = ecs_.get<BoidVel>(boidEntities[i]).vel;
  }

  glm::vec2 inputArrow(0.f);
  if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) inputArrow.y -= 1.f;
  if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) inputArrow.y += 1.f;
  if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) inputArrow.x -= 1.f;
  if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) inputArrow.x += 1.f;
  if (glm::length(inputArrow) > 0.f) {
    ecs_.get<BoidAcc>(boidEntities[0]).acc += inputArrow * 20.f;
    ecs_.get<BoidDebug>(boidEntities[0]).drawDebugRadius = true;
    ecs_.get<BoidDebug>(boidEntities[0]).color = Color::Red;
  }

  const auto& rules = boidsRules;
  jobs::WaitGroup wg;
  sched_.parallel_for(
      static_cast<std::size_t>(n), 16,
      [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; i++) {
          ecs::Entity e = boidEntities[i];
          BoidPos& pos = ecs_.get<BoidPos>(e);
          BoidVel& vel = ecs_.get<BoidVel>(e);
          BoidAcc& acc = ecs_.get<BoidAcc>(e);
          BoidConfig& cfg = ecs_.get<BoidConfig>(e);
          BoidForceCache& fc = ecs_.get<BoidForceCache>(e);

          std::vector<BoidView> neighborhood;
          const float r2 = cfg.detectionRadius * cfg.detectionRadius;
          for (int j = 0; j < n; j++) {
            if (static_cast<std::size_t>(j) == i) continue;
            glm::vec2 d = snapshot[j].position - snapshot[i].position;
            if (glm::dot(d, d) <= r2) neighborhood.push_back(snapshot[j]);
          }

          fc.forces.resize(rules.size());
          for (std::size_t ri = 0; ri < rules.size(); ri++) {
            glm::vec2 f = rules[ri]->computeWeightedForce(neighborhood, snapshot[i]);
            fc.forces[ri] = f;
            acc.acc += f;
          }

          float mag = glm::length(acc.acc);
          if (mag > cfg.maxAcceleration && mag > 0.0001f) acc.acc = acc.acc * (cfg.maxAcceleration / mag);

          glm::vec2 newVel = vel.vel + acc.acc * deltaTime;
          acc.prevAcc = acc.acc;
          acc.acc = glm::vec2(0.f);

          float speed = glm::length(newVel);
          if (cfg.hasConstantSpeed || speed > cfg.speed) {
            if (speed > 0.0001f) newVel = newVel * (cfg.speed / speed);
          }

          vel.vel = newVel;
          pos.pos += vel.vel * deltaTime;
        }
      },
      wg);
  sched_.wait(wg);

  for (auto e : boidEntities) warpIfOutOfBounds(ecs_.get<BoidPos>(e));
}

void FlockingManager::OnGui() { showConfigurationWindow(ImGui::GetIO().DeltaTime); }

void FlockingManager::OnDraw() {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();

  for (int i = 0; i < static_cast<int>(boidEntities.size()); i++) {
    ecs::Entity e = boidEntities[i];
    BoidPos& pos = ecs_.get<BoidPos>(e);
    BoidVel& vel = ecs_.get<BoidVel>(e);
    BoidAcc& acc = ecs_.get<BoidAcc>(e);
    BoidConfig& cfg = ecs_.get<BoidConfig>(e);
    BoidDebug& dbg = ecs_.get<BoidDebug>(e);

    glm::vec2 p = pos.pos;
    glm::vec2 v = vel.vel;

    float len = glm::length(v);
    glm::vec2 fwd = len > 0.0001f ? v / len : glm::vec2(0.f, -1.f);
    glm::vec2 perp(-fwd.y, fwd.x);
    ImVec2 tip = {p.x + fwd.x * 9.f, p.y + fwd.y * 9.f};
    ImVec2 left = {p.x - perp.x * 4.5f - fwd.x * 4.f, p.y - perp.y * 4.5f - fwd.y * 4.f};
    ImVec2 right = {p.x + perp.x * 4.5f - fwd.x * 4.f, p.y + perp.y * 4.5f - fwd.y * 4.f};
    ImU32 col = IM_COL32(static_cast<int>(dbg.color.r * 255), static_cast<int>(dbg.color.g * 255), static_cast<int>(dbg.color.b * 255),
                         static_cast<int>(dbg.color.a * 255));
    dl->AddTriangleFilled(tip, left, right, col);

    if (showRadius || dbg.drawDebugRadius) {
      dl->AddCircle({p.x, p.y}, cfg.detectionRadius,
                    IM_COL32(static_cast<int>(dbg.color.r * 255), static_cast<int>(dbg.color.g * 255), static_cast<int>(dbg.color.b * 255), 64), 32);
    }

    if (showAcceleration || dbg.drawAcceleration) {
      glm::vec2 end = p + acc.prevAcc * 0.08f;
      dl->AddLine({p.x, p.y}, {end.x, end.y}, IM_COL32(128, 0, 128, 220), 1.5f);
    }

    if (showRules || dbg.drawDebugRules) {
      BoidForceCache& fc = ecs_.get<BoidForceCache>(e);
      BoidView bv{p, v};
      for (std::size_t ri = 0; ri < boidsRules.size() && ri < fc.forces.size(); ri++) {
        if (boidsRules[ri]->isEnabled) boidsRules[ri]->draw(bv, dl, fc.forces[ri]);
      }
    }
  }

  if (showRules) {
    for (const auto& rule : boidsRules) {
      if (rule->isEnabled) rule->drawWorldOverlay(dl);
    }
  }
}

void FlockingManager::drawGeneralUI() {
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::CollapsingHeader("General")) {
    if (ImGui::DragInt("Number of Boids", &nbBoids)) {
      if (nbBoids < 0) nbBoids = 0;
      setNumberOfBoids(nbBoids);
    }
    ImGui::SameLine();
    HelpMarker("Drag to change the weight's value or CTRL+Click to input a new value.");

    if (ImGui::SliderFloat("Neighborhood Radius", &detectionRadius, 0.0f, 250.0f, "%.f"))
      for (auto e : boidEntities) ecs_.get<BoidConfig>(e).detectionRadius = detectionRadius;

    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::TreeNode("Movement Settings")) {
      if (ImGui::Checkbox("Has Constant Speed", &hasConstantSpeed))
        for (auto e : boidEntities) ecs_.get<BoidConfig>(e).hasConstantSpeed = hasConstantSpeed;

      const char* speedLabel = hasConstantSpeed ? "Speed" : "Max Speed";
      if (ImGui::SliderFloat(speedLabel, &desiredSpeed, 0.0f, 300.0f, "%.f"))
        for (auto e : boidEntities) ecs_.get<BoidConfig>(e).speed = desiredSpeed;

      if (ImGui::Checkbox("Has Max Acceleration", &hasMaxAcceleration)) {
        for (auto e : boidEntities) ecs_.get<BoidConfig>(e).maxAcceleration = hasMaxAcceleration ? maxAcceleration : 10000.f;
      }
      ImguiTooltip("Boids keeps more momentum when the acceleration is capped.");

      if (hasMaxAcceleration)
        if (ImGui::SliderFloat("Max Acceleration", &maxAcceleration, 0.0f, 35.0f, "%.f"))
          for (auto e : boidEntities) ecs_.get<BoidConfig>(e).maxAcceleration = maxAcceleration;

      ImGui::TreePop();
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("Display Settings")) {
      if (ImGui::Checkbox("Show Acceleration", &showAcceleration))
        for (auto e : boidEntities) ecs_.get<BoidDebug>(e).drawAcceleration = showAcceleration;
      if (ImGui::Checkbox("Show Radius", &showRadius))
        for (auto e : boidEntities) ecs_.get<BoidDebug>(e).drawDebugRadius = showRadius;
      if (ImGui::Checkbox("Show Rules", &showRules))
        for (auto e : boidEntities) ecs_.get<BoidDebug>(e).drawDebugRules = showRules;
      ImGui::TreePop();
    }

    if (ImGui::Button("Randomize Boids position and velocity"))
      for (auto e : boidEntities) randomizeBoidPosVel(e);
  }
}

void FlockingManager::drawRulesUI() {
  if (ImGui::CollapsingHeader("Rules")) {
    for (auto& rule : boidsRules) {
      rule->drawImguiRule();
      ImGui::Separator();
    }
    if (ImGui::Button("Restore Default Weights")) {
      int i = 0;
      for (auto& rule : boidsRules) rule->weight = defaultWeights[i++];
    }
    ImGui::Spacing();
  }
}

void FlockingManager::showConfigurationWindow(float deltaTime) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      ImGui::EndMenu();
    }
    ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", ImGui::GetIO().DeltaTime * 1000, 1.0f / ImGui::GetIO().DeltaTime,
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::EndMainMenuBar();
  }

  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
  ImGui::SetNextWindowSize(ImVec2(320, 550), ImGuiCond_Once);
  if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
    ImGui::Text("Control the simulation with those settings.");
    ImGui::Spacing();
    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.45f);

    drawGeneralUI();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    drawRulesUI();
    drawPerformanceUI(deltaTime);

    ImGui::End();
  }
}

void FlockingManager::drawPerformanceUI(float deltaTime) {
#if defined(_WIN32)
  if (ImGui::CollapsingHeader("Performance")) {
    ImGui::Text("Frames Per Second (FPS) : %.f", 1.f / deltaTime);
    PlotVar("Frame duration (ms)", deltaTime * 1000);
    ImGui::Separator();

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

    const int div = 1048576;
    ImGui::Text("Total Virtual Memory : %uMb", (unsigned)(memInfo.ullTotalPageFile / div));
    ImGui::Text("Total RAM : %uMb", (unsigned)(memInfo.ullTotalPhys / div));
    ImGui::Separator();
    ImGui::Text("Virtual Memory used by process : %uMb", (unsigned)(pmc.PrivateUsage / div));
    PlotVar("Virtual Memory Consumption (Mb)", (float)(pmc.PrivateUsage / div));
    ImGui::Text("RAM used by process : %uMb", (unsigned)(pmc.WorkingSetSize / div));
    PlotVar("Ram Consumption (Mb)", (float)(pmc.WorkingSetSize / div));
  }
#else
  (void)deltaTime;
#endif
}
