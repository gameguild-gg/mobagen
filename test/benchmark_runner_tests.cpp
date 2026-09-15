#include <doctest/doctest.h>

#include "benchmark_runner.hpp"

#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

using mobagen::benchmark::measure;
using mobagen::benchmark::measure_paired;
using mobagen::benchmark::Options;
using mobagen::benchmark::overhead_percent;
using mobagen::benchmark::paired_overhead;
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
  constexpr std::array args{
      std::string_view{"--warmup"},
      std::string_view{"7"},
      std::string_view{"--samples"},
      std::string_view{"11"},
      std::string_view{"--max-overhead-percent"},
      std::string_view{"1.25"},
  };

  const Options options = parse_options(args);

  CHECK(options.warmup == 7);
  CHECK(options.samples == 11);
  REQUIRE(options.max_overhead_percent.has_value());
  CHECK(*options.max_overhead_percent == doctest::Approx(1.25));
}

TEST_CASE("Benchmark options reject unknown, missing, and zero values") {
  constexpr std::array unknown{std::string_view{"--other"}, std::string_view{"2"}};
  constexpr std::array missing{std::string_view{"--samples"}};
  constexpr std::array zero{std::string_view{"--samples"}, std::string_view{"0"}};
  constexpr std::array negative_overhead{std::string_view{"--max-overhead-percent"}, std::string_view{"-0.1"}};

  CHECK_THROWS_AS(parse_options(unknown), std::invalid_argument);
  CHECK_THROWS_AS(parse_options(missing), std::invalid_argument);
  CHECK_THROWS_AS(parse_options(zero), std::invalid_argument);
  CHECK_THROWS_AS(parse_options(negative_overhead), std::invalid_argument);
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

TEST_CASE("Benchmark paired measurement alternates and reports comparable medians") {
  const Options options{.warmup = 2, .samples = 4};
  std::size_t baseline_calls = 0;
  std::size_t candidate_calls = 0;

  const auto result = measure_paired(
      "baseline", "candidate", options, [&] { ++baseline_calls; }, [&] { ++candidate_calls; });

  CHECK(baseline_calls == 6);
  CHECK(candidate_calls == 6);
  CHECK(result.baseline.samples_ns.size() == 4);
  CHECK(result.candidate.samples_ns.size() == 4);
  CHECK(result.baseline.median_ns >= 0.0);
  CHECK(result.candidate.median_ns >= 0.0);
  CHECK(overhead_percent(Result{"baseline", {100.0}, 100.0, 100.0}, Result{"candidate", {101.0}, 101.0, 101.0}) == doctest::Approx(1.0));

  CHECK_THROWS_AS(measure_paired(
                      "baseline", "candidate", options, [] {}, [] {}, 0),
                  std::invalid_argument);
}

TEST_CASE("Benchmark paired overhead exposes median and conservative p05") {
  const mobagen::benchmark::PairedResult result{
      .baseline = Result{"baseline", {100.0, 100.0, 100.0}, 100.0, 100.0},
      .candidate = Result{"candidate", {99.0, 102.0, 104.0}, 102.0, 104.0},
  };

  const auto overhead = paired_overhead(result);

  CHECK(overhead.median_percent == doctest::Approx(2.0));
  CHECK(overhead.p05_percent == doctest::Approx(-1.0));

  const mobagen::benchmark::PairedResult persistent_regression{
      .baseline = Result{"baseline", {100.0, 100.0, 100.0}, 100.0, 100.0},
      .candidate = Result{"candidate", {102.0, 102.0, 102.0}, 102.0, 102.0},
  };
  CHECK(paired_overhead(persistent_regression).p05_percent == doctest::Approx(2.0));
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
  CHECK(json.find("\"max_overhead_percent\":null") != std::string::npos);
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
