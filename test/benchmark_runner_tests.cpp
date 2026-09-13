#include <doctest/doctest.h>

#include "benchmark_runner.hpp"

#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

using mobagen::benchmark::measure;
using mobagen::benchmark::Options;
using mobagen::benchmark::parse_options;
using mobagen::benchmark::percentile;
using mobagen::benchmark::Result;
using mobagen::benchmark::write_json;

TEST_CASE("Benchmark percentile uses the nearest-rank value") {
  const std::vector<double> samples{40.0, 10.0, 30.0, 20.0};

  CHECK(percentile(samples, 0.50) == 20.0);
  CHECK(percentile(samples, 0.95) == 40.0);
  CHECK_THROWS_AS(percentile({}, 0.50), std::invalid_argument);
  CHECK_THROWS_AS(percentile(samples, 0.0), std::invalid_argument);
  CHECK_THROWS_AS(percentile(samples, 1.01), std::invalid_argument);
}

TEST_CASE("Benchmark options accept positive warmup and sample counts") {
  constexpr std::array args{std::string_view{"--warmup"}, std::string_view{"7"}, std::string_view{"--samples"}, std::string_view{"11"}};

  const Options options = parse_options(args);

  CHECK(options.warmup == 7);
  CHECK(options.samples == 11);
}

TEST_CASE("Benchmark options reject unknown, missing, and zero values") {
  constexpr std::array unknown{std::string_view{"--other"}, std::string_view{"2"}};
  constexpr std::array missing{std::string_view{"--samples"}};
  constexpr std::array zero{std::string_view{"--samples"}, std::string_view{"0"}};

  CHECK_THROWS_AS(parse_options(unknown), std::invalid_argument);
  CHECK_THROWS_AS(parse_options(missing), std::invalid_argument);
  CHECK_THROWS_AS(parse_options(zero), std::invalid_argument);
}

TEST_CASE("Benchmark measurement separates warmup from retained samples") {
  std::size_t invocations = 0;
  const Options options{.warmup = 2, .samples = 3};

  const Result result = measure("operation", options, [&] { ++invocations; });

  CHECK(invocations == 5);
  CHECK(result.name == "operation");
  CHECK(result.samples_ns.size() == 3);
  CHECK(result.median_ns >= 0.0);
  CHECK(result.p95_ns >= result.median_ns);
}

TEST_CASE("Benchmark JSON includes schema, options, statistics, and raw samples") {
  const Options options{.warmup = 2, .samples = 3};
  const std::array results{
      Result{"operation", {10.0, 20.0, 30.0}, 20.0, 30.0},
  };
  std::ostringstream output;

  write_json(output, options, results);

  const std::string json = output.str();
  CHECK(json.find("\"schema\":\"mobagen.foundation-benchmark.v1\"") != std::string::npos);
  CHECK(json.find("\"warmup\":2") != std::string::npos);
  CHECK(json.find("\"samples\":3") != std::string::npos);
  CHECK(json.find("\"name\":\"operation\"") != std::string::npos);
  CHECK(json.find("\"median_ns\":20") != std::string::npos);
  CHECK(json.find("\"p95_ns\":30") != std::string::npos);
  CHECK(json.find("\"samples_ns\":[10,20,30]") != std::string::npos);
}

TEST_CASE("Benchmark JSON rejects non-finite raw samples") {
  const Options options{.warmup = 1, .samples = 1};
  const std::array results{
      Result{"operation", {std::numeric_limits<double>::quiet_NaN()}, 1.0, 1.0},
  };
  std::ostringstream output;

  CHECK_THROWS_AS(write_json(output, options, results), std::invalid_argument);
  CHECK(output.str().empty());
}
