#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// Keep both sides of function-table benchmarks behind the same indirect-call
// boundary. GCC's interprocedural constant propagation can otherwise clone a
// local baseline helper and devirtualize only that side of the comparison.
#if defined(_MSC_VER)
#  define MOBAGEN_BENCHMARK_OPAQUE_CALL __declspec(noinline)
#elif defined(__GNUC__) && !defined(__clang__)
#  define MOBAGEN_BENCHMARK_OPAQUE_CALL __attribute__((noipa))
#elif defined(__clang__)
#  define MOBAGEN_BENCHMARK_OPAQUE_CALL __attribute__((noinline))
#else
#  define MOBAGEN_BENCHMARK_OPAQUE_CALL
#endif

namespace mobagen::benchmark {
  struct Options {
    std::size_t warmup = 5;
    std::size_t samples = 30;
    std::optional<double> max_overhead_percent;
  };

  struct Result {
    std::string name;
    std::vector<double> samples_ns;
    double median_ns = 0.0;
    double p95_ns = 0.0;
  };

  inline std::size_t parse_positive_count(std::string_view value, std::string_view option) {
    std::size_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0) {
      throw std::invalid_argument(std::string(option) + " requires a positive integer");
    }
    return parsed;
  }

  inline double parse_non_negative_number(std::string_view value, std::string_view option) {
    double parsed = 0.0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(parsed) || parsed < 0.0) {
      throw std::invalid_argument(std::string(option) + " requires a non-negative number");
    }
    return parsed;
  }

  inline Options parse_options(std::span<const std::string_view> arguments) {
    Options options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const std::string_view option = arguments[index];
      if (option != "--warmup" && option != "--samples" && option != "--max-overhead-percent") {
        throw std::invalid_argument("unknown benchmark option: " + std::string(option));
      }
      if (++index == arguments.size()) {
        throw std::invalid_argument(std::string(option) + " requires a value");
      }
      if (option == "--warmup") {
        options.warmup = parse_positive_count(arguments[index], option);
      } else if (option == "--samples") {
        options.samples = parse_positive_count(arguments[index], option);
      } else {
        options.max_overhead_percent = parse_non_negative_number(arguments[index], option);
      }
    }
    return options;
  }

  inline Options parse_options(int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    return parse_options(arguments);
  }

  inline double percentile(std::vector<double> values, double quantile) {
    if (values.empty()) throw std::invalid_argument("percentile requires at least one sample");
    if (!std::isfinite(quantile) || quantile <= 0.0 || quantile > 1.0) {
      throw std::invalid_argument("percentile quantile must be in (0, 1]");
    }
    if (!std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); })) {
      throw std::invalid_argument("percentile samples must be finite");
    }

    std::sort(values.begin(), values.end());
    const std::size_t rank = static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(values.size())));
    return values[rank - 1];
  }

  template <class Fn> Result measure(std::string name, const Options& options, Fn&& operation) {
    if (options.warmup == 0 || options.samples == 0) {
      throw std::invalid_argument("benchmark warmup and samples must be positive");
    }

    for (std::size_t index = 0; index < options.warmup; ++index) operation();

    Result result;
    result.name = std::move(name);
    result.samples_ns.reserve(options.samples);
    for (std::size_t index = 0; index < options.samples; ++index) {
      const auto begin = std::chrono::steady_clock::now();
      operation();
      const auto end = std::chrono::steady_clock::now();
      result.samples_ns.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
    }
    result.median_ns = percentile(result.samples_ns, 0.50);
    result.p95_ns = percentile(result.samples_ns, 0.95);
    return result;
  }

  struct PairedResult {
    Result baseline;
    Result candidate;
  };

  template <class Baseline, class Candidate> PairedResult measure_paired(std::string baseline_name, std::string candidate_name,
                                                                         const Options& options, Baseline&& baseline, Candidate&& candidate,
                                                                         std::size_t interleavings = 1) {
    if (options.warmup == 0 || options.samples == 0 || interleavings == 0) {
      throw std::invalid_argument("benchmark warmup, samples, and interleavings must be positive");
    }
    for (std::size_t index = 0; index < options.warmup; ++index) {
      for (std::size_t interleave = 0; interleave < interleavings; ++interleave) {
        if ((index + interleave) % 2 == 0) {
          baseline();
          candidate();
        } else {
          candidate();
          baseline();
        }
      }
    }

    PairedResult result{{std::move(baseline_name)}, {std::move(candidate_name)}};
    result.baseline.samples_ns.reserve(options.samples);
    result.candidate.samples_ns.reserve(options.samples);
    const auto sample = [](auto&& operation) {
      const auto begin = std::chrono::steady_clock::now();
      operation();
      const auto end = std::chrono::steady_clock::now();
      return std::chrono::duration<double, std::nano>(end - begin).count();
    };
    for (std::size_t index = 0; index < options.samples; ++index) {
      std::vector<double> baseline_blocks;
      std::vector<double> candidate_blocks;
      baseline_blocks.reserve(interleavings);
      candidate_blocks.reserve(interleavings);
      for (std::size_t interleave = 0; interleave < interleavings; ++interleave) {
        if ((index + interleave) % 2 == 0) {
          baseline_blocks.push_back(sample(baseline));
          candidate_blocks.push_back(sample(candidate));
        } else {
          candidate_blocks.push_back(sample(candidate));
          baseline_blocks.push_back(sample(baseline));
        }
      }
      const auto scale = static_cast<double>(interleavings);
      result.baseline.samples_ns.push_back(percentile(baseline_blocks, 0.50) * scale);
      result.candidate.samples_ns.push_back(percentile(candidate_blocks, 0.50) * scale);
    }
    result.baseline.median_ns = percentile(result.baseline.samples_ns, 0.50);
    result.baseline.p95_ns = percentile(result.baseline.samples_ns, 0.95);
    result.candidate.median_ns = percentile(result.candidate.samples_ns, 0.50);
    result.candidate.p95_ns = percentile(result.candidate.samples_ns, 0.95);
    return result;
  }

  inline double overhead_percent(const Result& baseline, const Result& candidate) {
    if (!std::isfinite(baseline.median_ns) || baseline.median_ns <= 0.0 || !std::isfinite(candidate.median_ns) || candidate.median_ns < 0.0) {
      throw std::invalid_argument("overhead comparison requires finite positive timings");
    }
    return ((candidate.median_ns / baseline.median_ns) - 1.0) * 100.0;
  }

  struct PairedOverhead {
    double median_percent{};
    double p05_percent{};
  };

  inline PairedOverhead paired_overhead(const PairedResult& result) {
    if (result.baseline.samples_ns.empty() || result.baseline.samples_ns.size() != result.candidate.samples_ns.size()) {
      throw std::invalid_argument("paired overhead requires equally sized non-empty sample sets");
    }
    std::vector<double> samples;
    samples.reserve(result.baseline.samples_ns.size());
    for (std::size_t index = 0; index < result.baseline.samples_ns.size(); ++index) {
      const auto baseline = result.baseline.samples_ns[index];
      const auto candidate = result.candidate.samples_ns[index];
      if (!std::isfinite(baseline) || baseline <= 0.0 || !std::isfinite(candidate) || candidate < 0.0) {
        throw std::invalid_argument("paired overhead requires finite positive timings");
      }
      samples.push_back(((candidate / baseline) - 1.0) * 100.0);
    }
    return {
        .median_percent = percentile(samples, 0.50),
        .p05_percent = percentile(samples, 0.05),
    };
  }

  inline void write_json_string(std::ostream& output, std::string_view value) {
    output << '"';
    for (const unsigned char character : value) {
      switch (character) {
        case '"':
          output << "\\\"";
          break;
        case '\\':
          output << "\\\\";
          break;
        case '\b':
          output << "\\b";
          break;
        case '\f':
          output << "\\f";
          break;
        case '\n':
          output << "\\n";
          break;
        case '\r':
          output << "\\r";
          break;
        case '\t':
          output << "\\t";
          break;
        default:
          if (character < 0x20) {
            output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(character) << std::dec << std::setfill(' ');
          } else {
            output << static_cast<char>(character);
          }
      }
    }
    output << '"';
  }

  inline void write_json(std::ostream& output, const Options& options, std::span<const Result> results) {
    if (options.warmup == 0 || options.samples == 0) {
      throw std::invalid_argument("benchmark warmup and samples must be positive");
    }
    for (const Result& result : results) {
      if (result.samples_ns.size() != options.samples) {
        throw std::invalid_argument("benchmark result sample count does not match options");
      }
      if (!std::all_of(result.samples_ns.begin(), result.samples_ns.end(), [](double value) { return std::isfinite(value) && value >= 0.0; })) {
        throw std::invalid_argument("benchmark samples must be finite and non-negative");
      }
      if (!std::isfinite(result.median_ns) || !std::isfinite(result.p95_ns)) {
        throw std::invalid_argument("benchmark statistics must be finite");
      }
    }

    output << "{\"schema\":\"mobagen.foundation-benchmark.v1\",\"warmup\":" << options.warmup << ",\"samples\":" << options.samples
           << ",\"max_overhead_percent\":";
    if (options.max_overhead_percent.has_value()) {
      output << std::setprecision(17) << *options.max_overhead_percent;
    } else {
      output << "null";
    }
    output << ",\"results\":[";
    for (std::size_t result_index = 0; result_index < results.size(); ++result_index) {
      if (result_index != 0) output << ',';
      const Result& result = results[result_index];
      output << "{\"name\":";
      write_json_string(output, result.name);
      output << std::setprecision(17) << ",\"median_ns\":" << result.median_ns << ",\"p95_ns\":" << result.p95_ns << ",\"samples_ns\":[";
      for (std::size_t sample_index = 0; sample_index < result.samples_ns.size(); ++sample_index) {
        if (sample_index != 0) output << ',';
        output << result.samples_ns[sample_index];
      }
      output << "]}";
    }
    output << "]}\n";
  }
}  // namespace mobagen::benchmark
