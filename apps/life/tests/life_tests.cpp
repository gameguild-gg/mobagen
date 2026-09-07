#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "World.h"
#include "rules/JohnConway.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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
  // Replace \r\n with \n first
  size_t pos = 0;
  while ((pos = result.find("\r\n", pos)) != std::string::npos) {
    result.replace(pos, 2, "\n");
    pos += 1;
  }
  // Replace remaining \r with \n
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

// Helper function to compare grid outputs (exact characters, no float tolerance needed)
bool compareOutputs(const std::string& actual, const std::string& expected) {
  return trim(normalizeLineEndings(actual)) == trim(normalizeLineEndings(expected));
}

// Drives the existing rule class (rules/JohnConway) through the formal ai4games fixture semantics:
// C x L toroidal grid, T generations of B3/S23 on the double-buffered World.
std::string runLifeSimulation(const std::string& input) {
  std::istringstream inputStream(input);

  int columns, lines, steps;
  inputStream >> columns >> lines >> steps;

  World world;
  world.Resize(columns, lines);

  // Read the grid char by char, same parser semantics as the ai4games harness
  for (int l = 0; l < lines; ++l) {
    for (int c = 0; c < columns; ++c) {
      char e;
      inputStream >> e;
      if (e == '.') {
        world.SetCurrent({c, l}, false);
      } else if (e == '#') {
        world.SetCurrent({c, l}, true);
      } else {
        throw std::runtime_error("Invalid input character: " + std::string(1, e));
      }
    }
    // Skip any remaining characters on the line
    inputStream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }

  JohnConway rule;
  for (int s = 0; s < steps; ++s) {
    rule.Step(world);
    world.SwapBuffers();
  }

  std::ostringstream outputStream;
  for (int l = 0; l < lines; ++l) {
    for (int c = 0; c < columns; ++c) {
      outputStream << (world.Get({c, l}) ? '#' : '.');
    }
    outputStream << "\n";
  }

  return outputStream.str();
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
    expectedOutput = trim(normalizeLineEndings(expectedOutput));

    std::string actualOutput;
    try {
      actualOutput = runLifeSimulation(input);
    } catch (const std::exception& e) {
      std::cerr << "Exception during simulation (" << fixture.name << "): " << e.what() << std::endl;
      actualOutput.clear();
    }
    passed = compareOutputs(actualOutput, expectedOutput);
  }

  fixtureResults[fixture.name] = passed;
  return passed;
}

TEST_CASE("Life formal fixtures") {
  static const auto fixtures = findFixtures(fs::path(LIFE_TESTS_DIR));

  for (const auto& fixture : fixtures) {
    SUBCASE(fixture.name.c_str()) { CHECK(fixturePasses(fixture)); }
  }
}

int main(int argc, char** argv) {
  doctest::Context ctx(argc, argv);
  int res = ctx.run();

  if (fixtureResults.empty()) {
    std::cout << "Life formal tests: fixtures not found at " << LIFE_TESTS_DIR << std::endl;
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
  std::cout << "Life formal tests: " << passedCount << "/" << fixtureResults.size() << " passed (" << percentage << "%)" << std::endl;

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
