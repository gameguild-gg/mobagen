// Catch The Cat — hex-grid cat-and-catcher game on the core app host.
//
// Hosted app (core-app-host plan, todo 14): the SDL3 callback host owns the
// window, the WebGPU context, the ImGui layer and the frame loop. The graded
// headless CLI contract is preserved byte-for-byte: parseCommandLineArguments
// and runHeadlessMode run inside on_init — BEFORE any SDL_Init/window/GPU
// object exists — and their exit codes map straight onto SDL_APP_SUCCESS (0)
// / SDL_APP_FAILURE (1), exactly like the old main() dispatch.
#include "World.h"

#include "app/sdl_app.hpp"
#include "imgui/imgui_layer.hpp"

#include "ecs/world.hpp"
#include "imgui.h"
#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// ECS components — DOD representation of the hex grid
// ============================================================
struct HexPosition {
  glm::ivec2 gridPos;
  int linearIdx;
};

struct BlockedState {
  bool blocked = false;
};

struct AgentState {
  bool isCat = false;
  glm::ivec2 pos = {0, 0};
};

// ============================================================
// Hex rendering — ImGui background draw list replaces Renderer2D
// ============================================================
static void FillHexagon(ImDrawList* dl, float cx, float cy, float radius, ImU32 color) {
  constexpr int kSides = 6;
  constexpr float kPi = 3.14159265359f;
  cx = roundf(cx);
  cy = roundf(cy);
  for (int i = 0; i < kSides; ++i) {
    float a1 = (kPi / 3.0f) * i + kPi / 6.0f;
    float a2 = (kPi / 3.0f) * (i + 1) + kPi / 6.0f;
    ImVec2 center{cx, cy};
    ImVec2 p1{cx + cosf(a1) * radius, cy + sinf(a1) * radius};
    ImVec2 p2{cx + cosf(a2) * radius, cy + sinf(a2) * radius};
    dl->AddTriangleFilled(center, p1, p2, color);
  }
}

// Direct translation of original World::OnDraw() to ImGui draw list.
// The ECS view (via HexPosition / BlockedState) is used by the sync path;
// the draw path iterates CatWorld's contiguous worldState for correct order.
static void drawHexGrid(const CatWorld& catWorld, float winW, float winH) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  int sz = catWorld.getWorldSideSize();
  // (std::min): parentheses block the windows.h 'min' macro on MSVC/clang-cl.
  float scale = ((std::min)(winW, winH) / static_cast<float>(sz)) / 2.0f;
  float radius = floorf(scale) - 0.5f;

  float posX = winW / 2.0f - sz * scale;
  float posY = winH / 2.0f - (sz - 1) * scale;
  if (sz % 4 >= 2) posX += scale;

  const auto& state = catWorld.worldState();
  auto catPos = catWorld.getCat();
  int catIdx = (catPos.y + sz / 2) * sz + (catPos.x + sz / 2);
  int total = static_cast<int>(state.size());

  for (int i = 0; i < total;) {
    ImU32 color;
    if (i == catIdx)
      color = IM_COL32(255, 65, 65, 255);  // red   — cat
    else if (state[i])
      color = IM_COL32(65, 128, 255, 255);  // blue  — blocked
    else
      color = IM_COL32(180, 180, 180, 255);  // gray  — open

    FillHexagon(dl, posX, posY, radius, color);
    i++;

    if (i % (2 * sz) == 0) {
      posX = winW / 2.0f - sz * scale + (sz % 4 >= 2 ? 1.0f : 0.0f) * scale;
      posY += 2.0f * scale;
    } else if (i % sz == 0) {
      posX = winW / 2.0f - sz * scale + (sz % 4 <= 1 ? 1.0f : 0.0f) * scale;
      posY += 2.0f * scale;
    } else {
      posX += 2.0f * scale;
    }
  }
}

// ============================================================
// ECS sync helpers — keep DOD entities consistent with game state
// ============================================================
static void rebuildECS(ecs::World& ecsWorld, const CatWorld& catWorld, std::vector<ecs::Entity>& cells, ecs::Entity& catEntity) {
  for (auto e : cells) ecsWorld.destroy(e);
  cells.clear();
  if (ecsWorld.valid(catEntity)) ecsWorld.destroy(catEntity);

  int sz = catWorld.getWorldSideSize();
  int half = sz / 2;
  const auto& state = catWorld.worldState();
  int idx = 0;
  for (int y = -half; y <= half; ++y) {
    for (int x = -half; x <= half; ++x) {
      ecs::Entity e = ecsWorld.create();
      ecsWorld.add<HexPosition>(e, HexPosition{{x, y}, idx});
      ecsWorld.add<BlockedState>(e, BlockedState{state[idx]});
      cells.push_back(e);
      ++idx;
    }
  }
  catEntity = ecsWorld.create();
  ecsWorld.add<AgentState>(catEntity, AgentState{true, catWorld.getCat()});
}

static void syncECS(ecs::World& ecsWorld, const CatWorld& catWorld, const std::vector<ecs::Entity>& cells, ecs::Entity catEntity) {
  const auto& state = catWorld.worldState();
  for (std::size_t i = 0; i < cells.size(); ++i) ecsWorld.get<BlockedState>(cells[i]).blocked = state[i];
  if (ecsWorld.valid(catEntity)) ecsWorld.get<AgentState>(catEntity).pos = catWorld.getCat();
}

// ============================================================
// CLI helpers — preserve original headless-mode interface
// ============================================================
static void printUsage() {
  std::cout << "Usage: catchthecat [--headless --turn <cat|catcher> --size <size> --board <board_string>]\n";
  std::cout << "  --headless: Run in headless mode\n";
  std::cout << "  --turn: Specify whose turn it is (cat or catcher)\n";
  std::cout << "  --size: Size of the board (odd number)\n";
  std::cout << "  --board: Board configuration using . (empty), # (blocked), C (cat)\n";
  std::cout << "Example: catchthecat --headless --turn cat --size 5 --board \".....#....C....#.....\"\n";
}

static Point2D findCatPosition(const std::string& boardStr, int size) {
  int pos = 0;
  for (int i = 0; i < static_cast<int>(boardStr.length()); i++) {
    char c = boardStr[i];
    if (c == '.' || c == '#') {
      pos++;
      continue;
    } else if (c == 'C') {
      int y = pos / size;
      int x = pos % size;
      return {x - size / 2, y - size / 2};
    }
  }
  return {0, 0};
}

static std::vector<bool> parseBoardString(const std::string& boardStr, int size) {
  std::vector<bool> worldState(size * size, false);
  int validCharCount = 0;
  int expectedCount = size * size;

  for (int i = 0; i < static_cast<int>(boardStr.length()) && validCharCount < expectedCount; i++) {
    char c = boardStr[i];
    if (c == '#') {
      worldState[validCharCount++] = true;
    } else if (c == '.' || c == 'C') {
      worldState[validCharCount++] = false;
    }
  }

  if (validCharCount != expectedCount) {
    std::cerr << "Error: Found " << validCharCount << " valid characters, but expected " << expectedCount << " for a " << size << "x" << size
              << " board\n";
    return std::vector<bool>(size * size, false);
  }
  return worldState;
}

struct GameConfig {
  bool headless = false;
  bool isCatTurn = true;
  int size = 21;
  std::string boardStr = "";
};

static int parseCommandLineArguments(int argc, char** argv, GameConfig& config) {
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--headless") {
      config.headless = true;
    } else if (arg == "--turn" && i + 1 < argc) {
      std::string turn = argv[++i];
      if (turn == "cat")
        config.isCatTurn = true;
      else if (turn == "catcher")
        config.isCatTurn = false;
      else {
        std::cerr << "Error: Invalid turn value. Use 'cat' or 'catcher'\n";
        printUsage();
        return 1;
      }
    } else if (arg == "--size" && i + 1 < argc) {
      config.size = std::stoi(argv[++i]);
      if (config.size % 2 == 0 || config.size < 3) {
        std::cerr << "Error: Size must be an odd number >= 3\n";
        printUsage();
        return 1;
      }
    } else if (arg == "--board" && i + 1 < argc) {
      config.boardStr = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown argument " << arg << "\n";
      printUsage();
      return 1;
    }
  }
  return -1;  // continue
}

// ============================================================
// Headless mode: pure game logic, no window / GPU required
// ============================================================
static int runHeadlessMode(const GameConfig& config) {
  if (config.boardStr.empty()) {
    std::cerr << "Error: Board string is required for headless mode\n";
    printUsage();
    return 1;
  }

  Point2D catPos = findCatPosition(config.boardStr, config.size);
  std::vector<bool> wst = parseBoardString(config.boardStr, config.size);

  CatWorld world(config.size, config.isCatTurn, catPos, wst);
  world.step();
  world.print();
  std::cout << world.moveDuration << std::endl;
  std::cout << world.lastMove.x << "," << world.lastMove.y << std::endl;
  return 0;
}

// ============================================================
// Hosted app — windowed mode on the core app host
// ============================================================
struct CatchTheCatApp : app::AppCallbacks {
  app::ImGuiLayer gui;
  CatWorld catWorld{11};
  std::vector<ecs::Entity> hexCells;
  ecs::Entity catEntity = ecs::kInvalidEntity;
  int lastSideSize = 0;
  int smoke_frames = 0;  // --smoke-frames N: exit after N iterates (0 = forever)

  SDL_AppResult on_init(app::App& app, int argc, char** argv) override {
    // Strip host/dev flags before the graded parser runs: it rejects every
    // unknown argument (old binaries exited 1 on --mobagen-* / --smoke-frames).
    std::vector<char*> args;
    args.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--smoke-frames" && i + 1 < argc) {
        smoke_frames = SDL_atoi(argv[++i]);
      } else if (arg.rfind("--mobagen-", 0) != 0) {
        args.push_back(argv[i]);
      }
    }

    // Graded CLI contract, byte-identical to the old main() dispatch; this
    // runs before any SDL/window/GPU object exists.
    GameConfig config;
    int parseResult = parseCommandLineArguments(static_cast<int>(args.size()), args.data(), config);
    if (parseResult != -1) return parseResult == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;

    if (config.headless) {
      int rc = runHeadlessMode(config);
      return rc == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    // Windowed mode: old runRegularMode(config.size) on the host.
    catWorld = CatWorld(config.size);
    lastSideSize = catWorld.getWorldSideSize();
    rebuildECS(app.world, catWorld, hexCells, catEntity);

    app.settings.title = "Catch The Cat";
    app.settings.clear_color[0] = 0.10f;
    app.settings.clear_color[1] = 0.10f;
    app.settings.clear_color[2] = 0.10f;
    app.settings.clear_color[3] = 1.00f;
    app::AppSettings::parse(argc, argv, app.settings);
    // HeadlessNone never opens a render pass and a GUI layer that fails init
    // aborts startup — attach only when a GPU frame can actually exist.
    if (app.settings.render_mode != app::AppSettings::RenderMode::HeadlessNone) app.attach_gui(gui);
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult on_iterate(app::App& app, float dt) override {
    // Game update
    catWorld.update(dt);

    // Sync or rebuild ECS when board size changes
    if (catWorld.getWorldSideSize() != lastSideSize) {
      rebuildECS(app.world, catWorld, hexCells, catEntity);
      lastSideSize = catWorld.getWorldSideSize();
    } else {
      syncECS(app.world, catWorld, hexCells, catEntity);
    }

    if (smoke_frames > 0 && --smoke_frames == 0) app.request_exit();  // consumed next frame start
    return SDL_APP_CONTINUE;
  }

  void on_draw(app::App& app, WGPURenderPassEncoder pass) override {
    (void)pass;
    ImGuiIO& io = ImGui::GetIO();

    // Hex grid rendered behind all ImGui windows via background draw list
    drawHexGrid(catWorld, static_cast<float>(app.width()), static_cast<float>(app.height()));

    // Settings panel (equivalent to original World::OnGui, context-param removed)
    {
      ImGui::Begin("Settings", nullptr);
      ImGui::Text("%.1fms %.0fFPS | AVG: %.2fms %.1fFPS", io.DeltaTime * 1000.0f, 1.0f / io.DeltaTime, 1000.0f / io.Framerate, io.Framerate);

      static int newSize = catWorld.getWorldSideSize();
      if (ImGui::SliderInt("Side Size", &newSize, 5, 29)) {
        newSize = (newSize / 4) * 4 + 1;
        if (newSize != catWorld.getWorldSideSize()) catWorld.setSizeAndReset(newSize);
      }
      if (ImGui::SliderFloat("Turn Duration", &catWorld.timeBetweenAITicksRef(), 0.0f, 30.0f)
          && catWorld.getWorldSideSize() != (newSize / 2) * 2 + 1) {
        catWorld.setSizeAndReset((newSize / 2) * 2 + 1);
      }
      ImGui::Text(catWorld.isCatTurn() ? "Turn: CAT" : "Turn: CATCHER");
      ImGui::Text("Move duration: %lli", catWorld.moveDuration);
      ImGui::Text("Next turn in %.1f", catWorld.timeForNextTick());
      if (ImGui::Button("Randomize")) catWorld.randomize();
      ImGui::Text("Simulation");
      if (ImGui::Button("Step")) {
        catWorld.setSimulating(false);
        catWorld.step();
      }
      ImGui::SameLine();
      if (ImGui::Button("Start")) catWorld.setSimulating(true);
      ImGui::SameLine();
      if (ImGui::Button("Pause")) catWorld.setSimulating(false);
      ImGui::End();
    }

    // Win / loss overlay
    if (catWorld.catcherWon() || catWorld.catWon()) {
      ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      if (catWorld.catcherWon()) {
        ImGui::Begin("Catcher Won");
        if (ImGui::Button("OK", ImVec2(200, 0))) catWorld.randomize();
        ImGui::End();
      }
      if (catWorld.catWon()) {
        ImGui::Begin("Cat Won");
        if (ImGui::Button("OK", ImVec2(200, 0))) catWorld.randomize();
        ImGui::End();
      }
    }
  }
};

MOBAGEN_MAIN(CatchTheCatApp)
