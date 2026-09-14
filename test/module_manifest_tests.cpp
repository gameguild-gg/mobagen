#include <doctest/doctest.h>

#include <algorithm>
#include <string_view>
#include <vector>

#include "modules/manifest_parser.hpp"

namespace {

  bool has_error(const mobagen::modules::ManifestParseResult& result, mobagen::modules::ManifestErrorCode code, std::string_view field) {
    return std::ranges::any_of(result.errors, [=](const auto& error) { return error.code == code && error.field == field; });
  }

}  // namespace

TEST_CASE("Module manifest: canonical mobagen yaml parses into product intent") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
name: dicom-viewer
modules:
  render:
    use: default
  volume-importer:
    use: mobagen.import.dicom
plugins:
  - ./plugins/custom-transfer.plugin
profiles:
  editor:
    linkage: dynamic
    permissions:
      - filesystem-read
      - gpu
  web:
    linkage: static
    editor: false
)yaml";

  const auto result = parse_product_manifest(source, "project/mobagen.yaml");

  REQUIRE(result.ok());
  REQUIRE(result.descriptor.has_value());
  CHECK(result.descriptor->schema == 1);
  CHECK(result.descriptor->name == "dicom-viewer");
  REQUIRE(result.descriptor->modules.size() == 2);
  CHECK(result.descriptor->modules[0].alias == "render");
  CHECK(result.descriptor->modules[0].provider == "default");
  CHECK(result.descriptor->modules[1].provider == "mobagen.import.dicom");
  CHECK(result.descriptor->plugins == std::vector<std::string>{"./plugins/custom-transfer.plugin"});
  REQUIRE(result.descriptor->profiles.size() == 2);
  CHECK(result.descriptor->profiles[0].linkage == LinkageMode::Dynamic);
  CHECK(result.descriptor->profiles[0].editor);
  CHECK(result.descriptor->profiles[0].permissions == std::vector<std::string>{"filesystem-read", "gpu"});
  CHECK(result.descriptor->profiles[1].linkage == LinkageMode::Static);
  CHECK_FALSE(result.descriptor->profiles[1].editor);
  CHECK(result.descriptor->profiles[1].permissions.empty());
}

TEST_CASE("Module manifest: duplicate keys are rejected with source coordinates") {
  using namespace mobagen::modules;

  const auto result = parse_product_manifest("schema: 1\nname: duplicate\nname: duplicate-again\nmodules: {}\n", "mobagen.yaml");

  REQUIRE_FALSE(result.ok());
  CHECK_FALSE(result.descriptor.has_value());
  REQUIRE(has_error(result, ManifestErrorCode::DuplicateKey, "name"));
  const auto& error
      = *std::ranges::find_if(result.errors, [](const ManifestError& candidate) { return candidate.code == ManifestErrorCode::DuplicateKey; });
  CHECK(error.source_path == "mobagen.yaml");
  CHECK(error.line == 3);
  CHECK(error.column == 1);
}

TEST_CASE("Module manifest: nested duplicate keys are rejected") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
name: duplicate-module
modules:
  render:
    use: default
  render:
    use: mobagen.render.webgpu
)yaml";

  const auto result = parse_product_manifest(source);

  CHECK(has_error(result, ManifestErrorCode::DuplicateKey, "modules.render"));
  CHECK_FALSE(result.descriptor.has_value());
}

TEST_CASE("Module manifest: unknown fields and custom tags are rejected") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
name: !execute forbidden
unknown-root: true
modules:
  render:
    use: default
    priority: 10
profiles:
  release:
    linkage: static
    surprise: true
)yaml";

  const auto result = parse_product_manifest(source, "mobagen.yaml");

  CHECK(has_error(result, ManifestErrorCode::UnsupportedTag, "name"));
  CHECK(has_error(result, ManifestErrorCode::UnknownField, "unknown-root"));
  CHECK(has_error(result, ManifestErrorCode::UnknownField, "modules.render.priority"));
  CHECK(has_error(result, ManifestErrorCode::UnknownField, "profiles.release.surprise"));
}

TEST_CASE("Module manifest: scalar and collection types are strict") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: "1"
name: [not, scalar]
modules: []
plugins: {}
profiles:
  release:
    linkage: static
    editor: nope
    permissions: {}
)yaml";

  const auto result = parse_product_manifest(source, "mobagen.yaml");

  CHECK(has_error(result, ManifestErrorCode::WrongType, "schema"));
  CHECK(has_error(result, ManifestErrorCode::WrongType, "name"));
  CHECK(has_error(result, ManifestErrorCode::WrongType, "modules"));
  CHECK(has_error(result, ManifestErrorCode::WrongType, "plugins"));
  CHECK(has_error(result, ManifestErrorCode::InvalidValue, "profiles.release.editor"));
  CHECK(has_error(result, ManifestErrorCode::WrongType, "profiles.release.permissions"));
}

TEST_CASE("Module manifest: profile permissions must be unique lowercase slugs") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
name: invalid-permissions
modules: {}
profiles:
  release:
    linkage: static
    permissions:
      - gpu
      - GPU
      - gpu
)yaml";

  const auto result = parse_product_manifest(source, "mobagen.yaml");

  CHECK_FALSE(result.ok());
  CHECK(has_error(result, ManifestErrorCode::InvalidValue, "profiles.release.permissions[1]"));
  CHECK(has_error(result, ManifestErrorCode::InvalidValue, "profiles.release.permissions[2]"));
}

TEST_CASE("Module manifest: missing required fields and invalid values fail transactionally") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 2
name: invalid-product
modules:
  render:
    use: custom
plugins:
  - ./plugins/custom.zip
profiles:
  release:
    linkage: shared
)yaml";

  const auto result = parse_product_manifest(source, "mobagen.yaml");

  CHECK_FALSE(result.ok());
  CHECK(has_error(result, ManifestErrorCode::UnsupportedSchema, "schema"));
  CHECK(has_error(result, ManifestErrorCode::InvalidValue, "modules.render.use"));
  CHECK(has_error(result, ManifestErrorCode::InvalidValue, "plugins[0]"));
  CHECK(has_error(result, ManifestErrorCode::InvalidValue, "profiles.release.linkage"));
}

TEST_CASE("Module manifest: malformed YAML returns a syntax diagnostic") {
  using namespace mobagen::modules;

  const auto result = parse_product_manifest("schema: [1\n", "broken/mobagen.yaml");

  REQUIRE_FALSE(result.ok());
  REQUIRE(has_error(result, ManifestErrorCode::Syntax, {}));
  CHECK(result.errors.front().source_path == "broken/mobagen.yaml");
  CHECK(result.errors.front().line > 0);
  CHECK(result.errors.front().column > 0);
}

TEST_CASE("Module manifest: oversized input is rejected before YAML allocation") {
  using namespace mobagen::modules;

  const std::string source(max_product_manifest_bytes + 1, 'x');
  const auto result = parse_product_manifest(source);

  CHECK(has_error(result, ManifestErrorCode::LimitExceeded, {}));
  CHECK_FALSE(result.descriptor.has_value());
}
