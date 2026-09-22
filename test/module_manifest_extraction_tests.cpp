#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "extraction_cli.hpp"
#include "modules/module_manifest.hpp"
#include <mobagen/module/module_abi.h>

#ifndef MOBAGEN_MODULE_EXTRACTION_REFERENCE_GUEST
#  define MOBAGEN_MODULE_EXTRACTION_REFERENCE_GUEST ""
#endif
#ifndef MOBAGEN_MODULE_EXTRACTION_GOLDEN_MANIFEST
#  define MOBAGEN_MODULE_EXTRACTION_GOLDEN_MANIFEST ""
#endif
#ifndef MOBAGEN_MODULE_EXTRACTION_ENTRYLESS_GUEST
#  define MOBAGEN_MODULE_EXTRACTION_ENTRYLESS_GUEST ""
#endif

namespace {

  std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    REQUIRE_MESSAGE(stream.good(), ("cannot read " + path.string()).c_str());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return std::move(buffer).str();
  }

  class TemporaryExtractionOutput {
  public:
    TemporaryExtractionOutput() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-module-extraction-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directories(path_));
    }

    ~TemporaryExtractionOutput() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] std::filesystem::path manifest() const { return path_ / "module.manifest"; }

  private:
    std::filesystem::path path_;
  };

  int run_manifest(std::string_view wasm, std::string_view output_path, std::ostringstream& output, std::ostringstream& error) {
    const std::vector<std::string_view> arguments{"manifest", wasm, output_path};
    return mobagen::modules::cli::run(arguments, output, error);
  }

}  // namespace

TEST_CASE("Module extraction CLI: reference guest manifest matches the golden manifest byte-for-byte") {
  const std::filesystem::path guest = MOBAGEN_MODULE_EXTRACTION_REFERENCE_GUEST;
  const std::filesystem::path golden = MOBAGEN_MODULE_EXTRACTION_GOLDEN_MANIFEST;
  REQUIRE(std::filesystem::exists(guest));
  REQUIRE(std::filesystem::exists(golden));

  TemporaryExtractionOutput scratch;
  std::ostringstream output;
  std::ostringstream error;
  const auto status = run_manifest(guest.string(), scratch.manifest().string(), output, error);
  REQUIRE_MESSAGE(status == 0, ("manifest extraction failed: " + error.str()).c_str());

  const auto generated = read_all(scratch.manifest());
  const auto expected = read_all(golden);
  CHECK(generated == expected);
}

TEST_CASE("Module extraction CLI: generated manifest reparses with the annotated signature ids") {
  const std::filesystem::path guest = MOBAGEN_MODULE_EXTRACTION_REFERENCE_GUEST;
  REQUIRE(std::filesystem::exists(guest));

  TemporaryExtractionOutput scratch;
  std::ostringstream output;
  std::ostringstream error;
  REQUIRE(run_manifest(guest.string(), scratch.manifest().string(), output, error) == 0);

  const auto text = read_all(scratch.manifest());
  const auto parsed = mobagen::modules::parse_module_manifest(text);
  REQUIRE(parsed.ok());
  CHECK(parsed.manifest->schema == mobagen::modules::module_manifest_schema_version);
  CHECK(parsed.manifest->abi_version == MOBAGEN_MODULE_ABI_VERSION);
  CHECK(parsed.manifest->entry == "mobagen_module_entry_v1");
  REQUIRE(parsed.manifest->exports.size() == 3);
  CHECK(parsed.manifest->exports[0].name == "reference_guest_ping");
  CHECK(parsed.manifest->exports[0].signature_id == MOBAGEN_MODULE_SIG_2(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32));
  CHECK(parsed.manifest->exports[1].name == "reference_guest_span_bytes");
  CHECK(parsed.manifest->exports[1].signature_id == MOBAGEN_MODULE_SIG_1(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_PTR));
  CHECK(parsed.manifest->exports[2].name == "reference_guest_health");
  CHECK(parsed.manifest->exports[2].signature_id == MOBAGEN_MODULE_SIG_0(MOBAGEN_MODULE_T_VOID));

  REQUIRE(parsed.manifest->payloads.size() == 1);
  CHECK(parsed.manifest->payloads[0].filename == "plugin.wasm");
  CHECK(parsed.manifest->payloads[0].size == std::filesystem::file_size(guest));
  CHECK(parsed.manifest->payloads[0].hash.starts_with("sha256:"));

  const auto digest = mobagen::modules::module_manifest_signature(*parsed.manifest);
  CHECK(output.str().find("signature\t" + digest) != std::string::npos);
}

TEST_CASE("Module extraction CLI: entry-less wasm exits 3 naming the missing annotation table") {
  const std::filesystem::path guest = MOBAGEN_MODULE_EXTRACTION_ENTRYLESS_GUEST;
  REQUIRE(std::filesystem::exists(guest));

  TemporaryExtractionOutput scratch;
  std::ostringstream output;
  std::ostringstream error;
  const auto status = run_manifest(guest.string(), scratch.manifest().string(), output, error);
  CHECK(status == 3);
  CHECK(error.str().find("mobagen_module_exports_v1") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(scratch.manifest()));
}

TEST_CASE("Module extraction CLI: usage errors exit 2") {
  std::ostringstream output;
  std::ostringstream error;
  CHECK(mobagen::modules::cli::run({}, output, error) == 2);
  CHECK(error.str().find("usage:") != std::string::npos);
}
