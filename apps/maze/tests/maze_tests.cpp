#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "../World.h"
#include "../SeededRandom.h"
#include "../generators/RecursiveBacktrackerExample.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Pass/reject per fixture, recorded by fixturePasses() and printed by the custom main below.
std::map<std::string, bool> fixtureResults;

// Helper function to normalize line endings (convert \r\n and \r to the standard linux style \n)
std::string normalizeLineEndings(const std::string& str) {
  std::string result = str;
  size_t pos = 0;
  while ((pos = result.find("\r\n", pos)) != std::string::npos) {
    result.replace(pos, 2, "\n");
    pos += 1;
  }
  pos = 0;
  while ((pos = result.find('\r', pos)) != std::string::npos) {
    result[pos] = '\n';
    pos += 1;
  }
  return result;
}

// Helper function to trim whitespace from both ends of a string
std::string trim(const std::string& str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// Helper function to normalize string by removing trailing spaces and empty lines with only spaces
std::string normalizeSpaces(const std::string& str) {
  std::istringstream iss(str);
  std::string line;
  std::vector<std::string> lines;

  while (std::getline(iss, line)) {
    size_t end = line.find_last_not_of(" \t");
    if (end != std::string::npos) {
      line = line.substr(0, end + 1);
      lines.push_back(line);
    } else if (line.empty()) {
      lines.push_back("");
    }
  }

  while (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }

  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) result += "\n";
    result += lines[i];
  }

  return result;
}

// Helper function to compare output strings (maze text: exact characters, no float tolerance)
bool compareOutputs(const std::string& actual, const std::string& expected) {
  return normalizeSpaces(normalizeLineEndings(actual)) == normalizeSpaces(normalizeLineEndings(expected));
}

// Renders the World as the classic ascii maze in formal units: (0, 0) at the
// top-left, "_" for horizontal walls, "|" for vertical walls. Trailing
// newlines and spaces are stripped from the result.
std::string renderMaze(World& world) {
  const int width = world.GetWidth();
  const int height = world.GetHeight();

  std::string result = " ";
  for (int c = 0; c < width; c++) {
    result += world.GetNorth(world.ToWorldCoords({c, 0})) ? "_" : " ";
    result += " ";
  }
  result += " \n";

  for (int r = 0; r < height; r++) {
    result += "|";
    for (int c = 0; c < width; c++) {
      Point2D cell = world.ToWorldCoords({c, r});
      result += world.GetSouth(cell) ? "_" : " ";
      result += world.GetEast(cell) ? "|" : " ";
    }
    result += " \n";
  }

  while (!result.empty() && (result.back() == '\n' || result.back() == ' ')) {
    result.pop_back();
  }

  return result;
}

// Runs the deterministic maze generation for one fixture input: `width height randomIndex`
std::string runMazeGeneration(const std::string& input) {
  std::istringstream inputStream(input);

  size_t width, height;
  size_t index;
  inputStream >> width >> height >> index;

  World world;
  world.Resize(static_cast<int>(width), static_cast<int>(height));

  RecursiveBacktrackerExample generator;
  generator.Clear(&world);
  SeededRandom::setIndex(static_cast<uint8_t>(index));
  while (generator.Step(&world)) {
  }

  return renderMaze(world);
}

struct FixtureFiles {
  std::string name;
  std::string input;
  std::string output;
};

std::vector<FixtureFiles> findFixtures(const fs::path& testsDir) {
  std::vector<FixtureFiles> fixtures;

  if (!fs::exists(testsDir) || !fs::is_directory(testsDir)) return fixtures;

  try {
    for (const auto& entry : fs::directory_iterator(testsDir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".in") {
        fs::path outputFile = testsDir / (entry.path().stem().string() + ".out");
        if (fs::exists(outputFile)) {
          fixtures.push_back({entry.path().stem().string(), entry.path().string(), outputFile.string()});
        }
      }
    }
  } catch (const fs::filesystem_error& ex) {
    std::cerr << "Filesystem error: " << ex.what() << std::endl;
  }

  std::sort(fixtures.begin(), fixtures.end(), [](const FixtureFiles& a, const FixtureFiles& b) { return a.name < b.name; });

  return fixtures;
}

bool fixturePasses(const FixtureFiles& fixture) {
  std::ifstream inFile(fixture.input);
  std::ifstream outFile(fixture.output);

  bool passed = false;
  if (inFile.is_open() && outFile.is_open()) {
    std::string input((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    std::string expectedOutput((std::istreambuf_iterator<char>(outFile)), std::istreambuf_iterator<char>());

    input = normalizeLineEndings(input);
    // note: no trim() here - the maze's leading space is significant; normalizeSpaces
    // inside compareOutputs handles trailing whitespace and empty lines on both sides.
    expectedOutput = normalizeLineEndings(expectedOutput);

    std::string actualOutput;
    try {
      actualOutput = runMazeGeneration(input);
    } catch (const std::exception& e) {
      std::cerr << "Exception during maze generation (" << fixture.name << "): " << e.what() << std::endl;
      actualOutput.clear();
    }
    passed = compareOutputs(actualOutput, expectedOutput);
  }

  fixtureResults[fixture.name] = passed;
  return passed;
}

TEST_CASE("Maze formal fixtures") {
  static const auto fixtures = findFixtures(fs::path(MAZE_TESTS_DIR));

  for (const auto& fixture : fixtures) {
    SUBCASE(fixture.name.c_str()) { CHECK(fixturePasses(fixture)); }
  }
}

int main(int argc, char** argv) {
  doctest::Context ctx(argc, argv);
  int res = ctx.run();

  if (fixtureResults.empty()) {
    std::cout << "Maze formal tests: fixtures not found at " << MAZE_TESTS_DIR << std::endl;
    return 2;
  }

  std::size_t passedCount = 0;
  std::vector<std::string> rejected;
  for (const auto& [name, passed] : fixtureResults) {
    if (passed) {
      passedCount++;
    } else {
      rejected.push_back(name);
    }
    const int dots = std::max(3, 25 - static_cast<int>(name.length()));
    std::cout << "[fixture] " << name << " " << std::string(dots, '.') << " " << (passed ? "PASS" : "REJECT") << std::endl;
  }

  const double percentage = 100.0 * static_cast<double>(passedCount) / static_cast<double>(fixtureResults.size());
  std::cout << std::fixed << std::setprecision(1);
  std::cout << "Maze formal tests: " << passedCount << "/" << fixtureResults.size() << " passed (" << percentage << "%)" << std::endl;
  // Grade-ready counts: parse-friendly lines for scripts and the CI summary.
  std::cout << "Passed: " << passedCount << std::endl;
  std::cout << "Failed: " << fixtureResults.size() - passedCount << std::endl;

  if (!rejected.empty()) {
    std::cout << "Rejected: ";
    for (std::size_t i = 0; i < rejected.size(); i++) {
      if (i > 0) std::cout << ", ";
      std::cout << rejected[i];
    }
    std::cout << std::endl;
  }

  return !rejected.empty() ? 1 : (res == 0 ? 0 : 1);
}
