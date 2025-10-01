#define SDL_MAIN_HANDLED true
#include "engine/Engine.h"
#include "World.h"
#include <string>
#include <iostream>
#include <sstream>

void printUsage() {
  std::cout << "Usage: catchthecat [--headless --turn <cat|catcher> --size <size> --board <board_string>]\n";
  std::cout << "  --headless: Run in headless mode\n";
  std::cout << "  --turn: Specify whose turn it is (cat or catcher)\n";
  std::cout << "  --size: Size of the board (odd number)\n";
  std::cout << "  --board: Board configuration using . (empty), # (blocked), C (cat)\n";
  std::cout << "Example: catchthecat --headless --turn cat --size 5 --board \".....#....C....#.....\"\n";
}

Point2D findCatPosition(const std::string& boardStr, int size) {
  for (int i = 0; i < boardStr.length(); i++) {
    if (boardStr[i] == 'C') {
      int y = i / size;
      int x = i % size;
      // Convert to world coordinates (center at 0,0)
      return {x - size/2, y - size/2};
    }
  }
  return {0, 0}; // Default to center if not found
}

std::vector<bool> parseBoardString(const std::string& boardStr, int size) {
  std::vector<bool> worldState(size * size, false);
  int validCharCount = 0;
  int expectedCount = size * size;
  
  // Parse the string, only counting valid characters
  for (int i = 0; i < boardStr.length() && validCharCount < expectedCount; i++) {
    char c = boardStr[i];
    
    if (c == '#' || c == 'C') {
      worldState[validCharCount] = true;  // blocked
      validCharCount++;
    } else if (c == '.') {
      worldState[validCharCount] = false; // empty (cat position handled separately)
      validCharCount++;
    }
    // Skip whitespace, newlines, and other invalid characters without incrementing validCharCount
  }
  
  // Check if we got the expected number of valid characters
  if (validCharCount != expectedCount) {
    std::cerr << "Error: Found " << validCharCount << " valid characters, but expected " 
              << expectedCount << " for a " << size << "x" << size << " board\n";
    return std::vector<bool>(size * size, false); // Return empty board on error
  }
  
  return worldState;
}

// Configuration structure to hold parsed arguments
struct GameConfig {
  bool headless = false;
  bool isCatTurn = true;
  int size = 21;
  std::string boardStr = "";
};

// Parse command line arguments and return configuration
int parseCommandLineArguments(int argc, char** argv, GameConfig& config) {
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    
    if (arg == "--headless") {
      config.headless = true;
    } else if (arg == "--turn" && i + 1 < argc) {
      std::string turn = argv[++i];
      if (turn == "cat") {
        config.isCatTurn = true;
      } else if (turn == "catcher") {
        config.isCatTurn = false;
      } else {
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
  return -1; // Continue execution
}

// Create and configure the engine based on settings
Engine* createEngine(const GameConfig& config) {
  SDL_Log("Creating Engine");
  
  EngineSettings settings;
  if (config.headless) {
    settings.headless = true;
    settings.debug = true;
  }
  
  auto engine = new Engine(settings);
  SDL_Log("Engine Created");
  return engine;
}

// Run the game in headless mode
int runHeadlessMode(Engine* engine, const GameConfig& config) {
  if (config.boardStr.empty()) {
    std::cerr << "Error: Board string is required for headless mode\n";
    printUsage();
    return 1;
  }
  
  // Parse board and find cat position
  Point2D catPos = findCatPosition(config.boardStr, config.size);
  std::vector<bool> worldState = parseBoardString(config.boardStr, config.size);
  
  // Create world with parsed data
  World* world = new World(engine, config.size, config.isCatTurn, catPos, worldState);
  
  // Print initial board state
  std::cout << "Initial board state:\n";
  world->print();
  std::cout << "\nTurn: " << (config.isCatTurn ? "CAT" : "CATCHER") << "\n\n";
  
  // Execute one turn
  world->step();
  
  // Print final board state
  std::cout << "Board state after turn:\n";
  world->print();
  std::cout << "\n";
  
  delete world;
  return 0;
}

// Run the game in regular windowed mode
int runRegularMode(Engine* engine, const GameConfig& config) {
  new World(engine, config.size);

  SDL_Log("Starting Engine");
  if (engine->Start("Catch The Cat")) {
    SDL_Log("Engine Started");
    engine->Run();
  }

  SDL_Log("Exiting Engine");
  engine->Exit();
  SDL_Log("Engine Exited");
  return 0;
}

// Main function - orchestrates the application flow
int main(int argc, char** argv) {
  GameConfig config;
  
  // Parse command line arguments
  int parseResult = parseCommandLineArguments(argc, argv, config);
  if (parseResult != -1) {
    return parseResult; // Early exit for help or error
  }
  
  // Create and configure engine
  Engine* engine = createEngine(config);
  
  // Run appropriate mode
  int result;
  if (config.headless) {
    result = runHeadlessMode(engine, config);
  } else {
    result = runRegularMode(engine, config);
  }
  
  return result;
}