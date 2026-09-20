#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include "behaviours/AlignmentRule.h"
#include "behaviours/CohesionRule.h"
#include "behaviours/SeparationRule.h"

#include <algorithm>
#include <cmath>
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

// Set by `--regenerate` in main: rewrite the .out files from the current rule implementations
// instead of comparing against them (instructor tool for refreshing the formal fixtures).
bool regenerateFixtures = false;

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

// Helper function to compare floating point numbers with tolerance
bool isClose(double a, double b, double tolerance = 1e-6) { return std::fabs(a - b) < tolerance; }

// Helper function to compare output strings with floating point tolerance
bool compareOutputs(const std::string& actual, const std::string& expected, double tolerance = 1e-3) {
  std::istringstream actualStream(actual);
  std::istringstream expectedStream(expected);

  double actualVal, expectedVal;
  while (actualStream >> actualVal && expectedStream >> expectedVal) {
    if (!isClose(actualVal, expectedVal, tolerance)) {
      return false;
    }
  }

  // Try to read one more value from each stream to trigger EOF
  double dummy;
  actualStream >> dummy;
  expectedStream >> dummy;

  // Check if both streams reached the end
  return actualStream.eof() && expectedStream.eof();
}

// Drives the existing rule classes (behaviours/) through the formal ai4games fixture semantics:
// double-buffered state, per-rule neighborhood filtering, harness-side separation clamping.
std::string runFlockingSimulation(const std::string& input) {
  std::istringstream inputStream(input);

  double cohesionRadius, separationRadius, separationMaxForce, alignmentRadius;
  double cohesionK, separationK, alignmentK;
  int numberOfBoids;
  inputStream >> cohesionRadius >> separationRadius >> separationMaxForce >> alignmentRadius >> cohesionK >> separationK >> alignmentK
      >> numberOfBoids;

  std::vector<glm::dvec2> pos, vel;
  for (int i = 0; i < numberOfBoids; i++) {
    double x, y, vx, vy;
    inputStream >> x >> y >> vx >> vy;
    pos.emplace_back(x, y);
    vel.emplace_back(vx, vy);
  }

  // Rule weights carry the K constants; base weight multipliers are all 1. Each rule filters its own
  // neighborhood from the full boid list using its radius member (boundary-inclusive; alignment
  // counts self): cohesion/alignment radii come from the header radii, separation's radius comes
  // from r_s and K_s rides its weight. Separation is clamped to separationMaxForce after the weighted sum.
  CohesionRule cohesion(static_cast<float>(cohesionRadius), static_cast<float>(cohesionK));
  AlignmentRule alignment(static_cast<float>(alignmentRadius), static_cast<float>(alignmentK));
  SeparationRule separation(static_cast<float>(separationRadius), static_cast<float>(separationK));

  std::vector<glm::dvec2> newPos = pos, newVel = vel;
  std::vector<BoidView> views(static_cast<std::size_t>(numberOfBoids));

  std::ostringstream outputStream;
  outputStream << std::fixed << std::setprecision(3);

  double deltaTime;
  while (inputStream >> deltaTime) {
    for (int i = 0; i < numberOfBoids; i++) {
      views[i] = BoidView{glm::vec2(pos[i]), glm::vec2(vel[i])};
    }
    for (int i = 0; i < numberOfBoids; i++) {
      const glm::vec2 cohesionForce = cohesion.computeWeightedForce(views, i);    // Kc * toCom/rc (proportional)
      const glm::vec2 alignmentForce = alignment.computeWeightedForce(views, i);  // Ka * raw mean (self included)
      glm::dvec2 separationForce(separation.computeWeightedForce(views, i));      // sum (away/d) * (Ks/d)

      // Spec clamp position: after accumulation, harness-side only (the visual app does not clamp).
      const double separationMagnitude = glm::length(separationForce);
      if (separationMagnitude > separationMaxForce) separationForce = separationForce / separationMagnitude * separationMaxForce;

      const glm::dvec2 totalForce = glm::dvec2(cohesionForce) + glm::dvec2(alignmentForce) + separationForce;
      newVel[i] = vel[i] + totalForce * deltaTime;
      newPos[i] = pos[i] + newVel[i] * deltaTime;
    }
    pos.swap(newPos);
    vel.swap(newVel);
    for (int i = 0; i < numberOfBoids; i++) {
      outputStream << pos[i].x << " " << pos[i].y << " " << vel[i].x << " " << vel[i].y << "\n";
    }
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

  if (!inFile.is_open()) {
    fixtureResults[fixture.name] = false;
    return false;
  }

  std::string input((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
  input = normalizeLineEndings(input);
  std::string actualOutput = trim(runFlockingSimulation(input));

  if (regenerateFixtures) {
    std::ofstream outFile(fixture.output, std::ios::binary);
    outFile << actualOutput << "\n";
    fixtureResults[fixture.name] = true;
    return true;
  }

  bool passed = false;
  std::ifstream outFile(fixture.output);
  if (outFile.is_open()) {
    std::string expectedOutput((std::istreambuf_iterator<char>(outFile)), std::istreambuf_iterator<char>());
    expectedOutput = trim(normalizeLineEndings(expectedOutput));
    passed = compareOutputs(actualOutput, expectedOutput, 1e-3);
  }

  fixtureResults[fixture.name] = passed;
  return passed;
}

TEST_CASE("Flocking formal fixtures") {
  static const auto fixtures = findFixtures(fs::path(FLOCKING_TESTS_DIR));

  for (const auto& fixture : fixtures) {
    SUBCASE(fixture.name.c_str()) { CHECK(fixturePasses(fixture)); }
  }
}

int main(int argc, char** argv) {
  std::vector<char*> keptArgs;
  keptArgs.push_back(argv[0]);
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--regenerate") {
      regenerateFixtures = true;
    } else {
      keptArgs.push_back(argv[i]);
    }
  }

  doctest::Context ctx(static_cast<int>(keptArgs.size()), keptArgs.data());
  int res = ctx.run();

  if (fixtureResults.empty()) {
    std::cout << "Flocking formal tests: fixtures not found at " << FLOCKING_TESTS_DIR << std::endl;
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
  std::cout << "Flocking formal tests: " << passedCount << "/" << fixtureResults.size() << " passed (" << percentage << "%)" << std::endl;
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
