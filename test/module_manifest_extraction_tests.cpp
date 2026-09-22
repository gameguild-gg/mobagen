#include <doctest/doctest.h>

#include <atomic>
#include <algorithm>
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

TEST_CASE("Module extraction CLI: golden manifest carries decodable marshaling descriptors") {
  using namespace mobagen::modules;
  const std::filesystem::path golden = MOBAGEN_MODULE_EXTRACTION_GOLDEN_MANIFEST;
  REQUIRE(std::filesystem::exists(golden));

  const auto parsed = parse_module_manifest(read_all(golden));
  REQUIRE(parsed.ok());
  REQUIRE(parsed.manifest->exports.size() == 3);

  /* The descriptor emitted for the tagged guest must match the expected
     signature table exactly: (i32,i32)->i32, (ptr)->i32, ()->void, and the
     i32-arg count/cell-width conventions the dispatchers rely on. */
  const auto& ping = parsed.manifest->exports[0];
  REQUIRE(ping.marshaling.has_value());
  CHECK(ping.marshaling->return_type == ModuleMarshalingType::I32);
  CHECK(ping.marshaling->param_count == 2);
  CHECK(ping.marshaling->params[0] == ModuleMarshalingType::I32);
  CHECK(ping.marshaling->params[1] == ModuleMarshalingType::I32);
  CHECK(module_marshaling_cell_count(*ping.marshaling) == 2);

  const auto& span_bytes = parsed.manifest->exports[1];
  REQUIRE(span_bytes.marshaling.has_value());
  CHECK(span_bytes.marshaling->return_type == ModuleMarshalingType::I32);
  CHECK(span_bytes.marshaling->param_count == 1);
  CHECK(span_bytes.marshaling->params[0] == ModuleMarshalingType::Ptr);
  CHECK(module_marshaling_cell_count(*span_bytes.marshaling) == 1);

  const auto& health = parsed.manifest->exports[2];
  REQUIRE(health.marshaling.has_value());
  CHECK(health.marshaling->return_type == ModuleMarshalingType::Void);
  CHECK(health.marshaling->param_count == 0);
  CHECK(module_marshaling_cell_count(*health.marshaling) == 0);

  /* Round-trip: a descriptor disagreeing with the signature id is a parse
     error (canonical-serialization guarantee), and the golden text itself
     reparses byte-stable. */
  const auto roundtrip = serialize_module_manifest(*parsed.manifest);
  CHECK(roundtrip == read_all(golden));
  CHECK(parse_module_manifest(roundtrip).ok());
}

TEST_CASE("Module manifest: undecodable signature ids are rejected at parse time") {
  using namespace mobagen::modules;
  constexpr auto id = [](std::uint32_t signature) {
    return "schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nexports:\n  - name: f\n    signature: " + std::to_string(signature)
           + "\n";
  };

  /* type code 7 does not exist in the ABI vocabulary — must fail at LOAD */
  constexpr auto unknown_type_id = MOBAGEN_MODULE_SIG_2(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, UINT32_C(7));
  const auto unknown_type = parse_module_manifest(id(unknown_type_id));
  CHECK_FALSE(unknown_type.ok());
  CHECK(std::ranges::any_of(unknown_type.errors, [](const auto& e) { return e.code == ModuleManifestErrorCode::InvalidValue; }));

  /* param count 6 exceeds the ABI's 5-slot table */
  constexpr auto over_arity_id = UINT32_C(0x4D600000) | MOBAGEN_MODULE_SIG_PACK(MOBAGEN_MODULE_T_I32, 17) | MOBAGEN_MODULE_SIG_PACK(MOBAGEN_MODULE_T_I32, 14);
  const auto over_arity = parse_module_manifest(id(over_arity_id));
  CHECK_FALSE(over_arity.ok());

  /* reserved bits set */
  const auto reserved = parse_module_manifest(id(MOBAGEN_MODULE_SIG_2(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32) | 1U));
  CHECK_FALSE(reserved.ok());

  /* ptr/span cells outside guest memory are rejected by the walker, and the
     decoded descriptor's return/params fields reject disagreement */
  constexpr auto disagree = "schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nexports:\n  - name: f\n    signature: 1294092288\n    return: i64\n";
  const auto mismatch = parse_module_manifest(disagree);
  CHECK_FALSE(mismatch.ok());
  CHECK(std::ranges::any_of(mismatch.errors, [](const auto& e) {
    return e.code == ModuleManifestErrorCode::InvalidValue && e.field.find("return") != std::string::npos;
  }));
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
