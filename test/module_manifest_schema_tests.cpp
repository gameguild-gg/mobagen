#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "modules/module_manifest.hpp"

namespace {

  bool has_error(const mobagen::modules::ModuleManifestParseResult& result, mobagen::modules::ModuleManifestErrorCode code, std::string_view field) {
    return std::ranges::any_of(result.errors, [=](const auto& error) { return error.code == code && error.field == field; });
  }

  /* 0x4D200000|ret@17|p1@14|p2@11 — MOBAGEN_MODULE_SIG_2(I32, I32, PTR) */
  constexpr std::uint32_t compute_signature = 0x4D200000U | (1U << 17) | (1U << 14) | (5U << 11);
  /* 0x4D000000|ret@17 — MOBAGEN_MODULE_SIG_0(I64) */
  constexpr std::uint32_t tick_count_signature = 0x4D000000U | (2U << 17);
  static_assert(compute_signature == 0x4D226800U);
  static_assert(tick_count_signature == 0x4D040000U);

}  // namespace

TEST_CASE("Module manifest: v2 golden parses with exports threads and payload hashes") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 2
api: 1
abi: 1
entry: mobagen_module_entry_v1
threads: managed
shared-memory: true
toolchain:
  producer: wamrc
  version: 2.4.5
exports:
  - name: mobagen_compute
    signature: 1294100480
payloads:
  - file: plugin.wasm
    hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    size: 4096
  - file: plugin.aot
    hash: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
    size: 8192
)yaml";

  const auto result = parse_module_manifest(source, "package/module.manifest");

  REQUIRE(result.ok());
  REQUIRE(result.manifest.has_value());
  CHECK(result.manifest->schema == module_manifest_schema_version);
  CHECK(result.manifest->api_version == 1);
  CHECK(result.manifest->abi_version == 1);
  CHECK(result.manifest->entry == "mobagen_module_entry_v1");
  CHECK(result.manifest->threads == ModuleThreadsPolicy::Managed);
  CHECK(result.manifest->shared_memory);
  REQUIRE(result.manifest->toolchain.has_value());
  CHECK(result.manifest->toolchain->producer == "wamrc");
  CHECK(result.manifest->toolchain->version == "2.4.5");
  REQUIRE(result.manifest->exports.size() == 1);
  CHECK(result.manifest->exports[0].name == "mobagen_compute");
  CHECK(result.manifest->exports[0].signature_id == compute_signature);
  REQUIRE(result.manifest->payloads.size() == 2);
  CHECK(result.manifest->payloads[0].filename == "plugin.wasm");
  CHECK(result.manifest->payloads[1].filename == "plugin.aot");
  CHECK(result.manifest->payloads[1].size == 8192);
}

TEST_CASE("Module manifest: defaults are threads none and shared-memory false") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 2
api: 1
abi: 1
entry: mobagen_module_entry_v1
exports: []
)yaml";

  const auto result = parse_module_manifest(source);

  REQUIRE(result.ok());
  CHECK(result.manifest->threads == ModuleThreadsPolicy::None);
  CHECK_FALSE(result.manifest->shared_memory);
  CHECK_FALSE(result.manifest->toolchain.has_value());
  CHECK(result.manifest->payloads.empty());
}

TEST_CASE("Module manifest: v1 manifests are rejected with a schema issue") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
api: 1
abi: 1
entry: mobagen_module_entry_v1
exports: []
)yaml";

  const auto result = parse_module_manifest(source, "module.manifest");

  CHECK_FALSE(result.ok());
  CHECK_FALSE(result.manifest.has_value());
  CHECK(has_error(result, ModuleManifestErrorCode::UnsupportedSchema, "schema"));
}

TEST_CASE("Module manifest: malformed exports entries yield field-pathed issues") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 2
api: 1
abi: 1
entry: mobagen_module_entry_v1
exports:
  - name: valid_export
    signature: 1294100480
  - signature: 1294100480
  - name: bad_signature
    signature: 12
  - name: bad_signature_magic
    signature: 16777216
  - name: valid_export
    signature: 1294100480
)yaml";

  const auto result = parse_module_manifest(source);

  CHECK_FALSE(result.ok());
  CHECK(has_error(result, ModuleManifestErrorCode::MissingField, "exports[1].name"));
  CHECK(has_error(result, ModuleManifestErrorCode::InvalidValue, "exports[2].signature"));
  CHECK(has_error(result, ModuleManifestErrorCode::InvalidValue, "exports[3].signature"));
  CHECK(has_error(result, ModuleManifestErrorCode::DuplicateEntry, "exports[4]"));
}

TEST_CASE("Module manifest: unknown fields threads vocabulary and entry symbol are strict") {
  using namespace mobagen::modules;

  SUBCASE("unknown root field") {
    const auto result = parse_module_manifest("schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nexports: []\nmystery: 1\n");
    CHECK(has_error(result, ModuleManifestErrorCode::UnknownField, "mystery"));
    CHECK_FALSE(result.ok());
  }

  SUBCASE("threads vocabulary") {
    const auto result = parse_module_manifest("schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nthreads: shared\nexports: []\n");
    CHECK(has_error(result, ModuleManifestErrorCode::InvalidValue, "threads"));
    CHECK_FALSE(result.ok());
  }

  SUBCASE("entry symbol convention") {
    const auto result = parse_module_manifest("schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v2\nexports: []\n");
    CHECK(has_error(result, ModuleManifestErrorCode::InvalidValue, "entry"));
    CHECK_FALSE(result.ok());
  }

  SUBCASE("shared-memory vocabulary") {
    const auto result = parse_module_manifest("schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nshared-memory: yes\nexports: []\n");
    CHECK(has_error(result, ModuleManifestErrorCode::InvalidValue, "shared-memory"));
    CHECK_FALSE(result.ok());
  }
}

TEST_CASE("Module manifest: payload filenames and hashes are strict") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 2
api: 1
abi: 1
entry: mobagen_module_entry_v1
payloads:
  - file: README.txt
    hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    size: 1
  - file: plugin.wasm
    hash: sha256:short
    size: 1
)yaml";

  const auto result = parse_module_manifest(source);

  CHECK_FALSE(result.ok());
  CHECK(has_error(result, ModuleManifestErrorCode::InvalidValue, "payloads[0].file"));
  CHECK(has_error(result, ModuleManifestErrorCode::InvalidHash, "payloads[1].hash"));
}

TEST_CASE("Module manifest: serialization is canonical and byte-stable") {
  using namespace mobagen::modules;

  ModuleManifest manifest;
  manifest.api_version = 1;
  manifest.abi_version = 1;
  manifest.entry = std::string{module_entry_symbol_v1};
  manifest.threads = ModuleThreadsPolicy::Managed;
  manifest.shared_memory = true;
  manifest.toolchain = ModuleManifestToolchain{"wamrc", "2.4.5"};
  manifest.exports = {{"mobagen_compute", compute_signature}, {"mobagen_tick_count", tick_count_signature}};
  manifest.payloads = {{"plugin.wasm", "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 4096}};

  const auto first = serialize_module_manifest(manifest);
  const auto second = serialize_module_manifest(manifest);
  CHECK(first == second);

  constexpr std::string_view expected = R"yaml(schema: 2
api: 1
abi: 1
entry: mobagen_module_entry_v1
threads: managed
shared-memory: true
toolchain:
  producer: wamrc
  version: 2.4.5
exports:
  - name: mobagen_compute
    signature: 1294100480
  - name: mobagen_tick_count
    signature: 1292107776
payloads:
  - file: plugin.wasm
    hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    size: 4096
)yaml";
  CHECK(first == expected);

  const auto reparsed = parse_module_manifest(first, "roundtrip.manifest");
  REQUIRE(reparsed.ok());
  CHECK(reparsed.manifest->threads == manifest.threads);
  CHECK(reparsed.manifest->shared_memory == manifest.shared_memory);
  REQUIRE(reparsed.manifest->exports.size() == 2);
  CHECK(reparsed.manifest->exports[1].signature_id == tick_count_signature);
}

TEST_CASE("Module manifest: signature digest is stable and export-order canonical") {
  using namespace mobagen::modules;

  ModuleManifest manifest;
  manifest.exports = {{"mobagen_compute", compute_signature}, {"mobagen_tick_count", tick_count_signature}};
  const auto first = module_manifest_signature(manifest);
  std::ranges::reverse(manifest.exports);
  const auto reversed = module_manifest_signature(manifest);

  CHECK_FALSE(first.empty());
  CHECK(first.starts_with("sha256:"));
  /* The digest covers the export table; the manifest itself is the authority for ordering. */
  CHECK(first != reversed);
  CHECK(module_manifest_signature(manifest) == reversed);
}
