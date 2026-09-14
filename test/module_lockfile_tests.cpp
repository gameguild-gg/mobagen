#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "modules/lockfile.hpp"

namespace {

  mobagen::modules::ProviderDescriptor lockfile_provider(std::string id, mobagen::modules::SemanticVersion version,
                                                         std::vector<std::string> capabilities) {
    using namespace mobagen::modules;

    return {
        .id = std::move(id),
        .version = version,
        .provides = std::move(capabilities),
        .targets = {TargetPlatform::Windows},
        .linkages = {LinkageMode::Static},
    };
  }

  struct ResolvedFixture {
    mobagen::modules::CapabilityRegistry registry;
    mobagen::modules::ModuleResolution resolution;
  };

  ResolvedFixture make_resolved_fixture() {
    using namespace mobagen::modules;

    auto renderer = lockfile_provider("mobagen.render.webgpu", {1, 4, 2}, {"render.backend.v1"});
    renderer.required = {"window.surface.v1"};
    renderer.permissions = {"gpu", "filesystem-read"};
    renderer.configuration_schema = "mobagen.render.config.v1";
    auto window = lockfile_provider("mobagen.window.sdl3", {3, 1, 0}, {"window.surface.v1"});
    window.permissions = {"windowing"};

    CapabilityRegistryBuilder registry_builder;
    registry_builder.add(std::move(renderer));
    registry_builder.add(std::move(window));
    auto registry_result = registry_builder.build();
    REQUIRE(registry_result.ok());
    auto registry = std::move(*registry_result.registry);

    ProductDescriptor product{
        .schema = project_schema_version,
        .name = "lockfile-test",
        .modules = {{.alias = "render", .provider = "default", .configuration = ModuleConfiguration{"mobagen.render.config.v1", "sample-count: 4"}}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Static, .editor = false, .permissions = {"windowing", "gpu", "filesystem-read"}}},
    };
    ResolverOptions options{
        .target = TargetPlatform::Windows,
        .profile = "release",
        .aliases = {{.alias = "render", .capability = "render.backend.v1"}},
        .defaults = {{
            .target = TargetPlatform::Windows,
            .profile = "release",
            .capability = "render.backend.v1",
            .provider = "mobagen.render.webgpu",
        }},
    };
    auto resolution_result = resolve_modules(product, registry, options);
    REQUIRE(resolution_result.ok());
    return {std::move(registry), std::move(*resolution_result.resolution)};
  }

  bool has_lockfile_issue(const mobagen::modules::LockfileSerializeResult& result, mobagen::modules::LockfileIssueCode code, std::string_view field) {
    return std::ranges::any_of(result.issues, [=](const auto& issue) { return issue.code == code && issue.field == field; });
  }

  bool has_parse_issue(const mobagen::modules::LockfileParseResult& result,
                       mobagen::modules::LockfileParseIssueCode code, std::string_view field) {
    return std::ranges::any_of(result.issues, [=](const auto& issue) {
      return issue.code == code && issue.field == field;
    });
  }

  class TemporaryLockDirectory {
  public:
    TemporaryLockDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() / ("mobagen-lockfile-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryLockDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(stream.good());
  }

  std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  }

  bool has_lockfile_temporary_file(const std::filesystem::path& directory) {
    return std::ranges::any_of(std::filesystem::directory_iterator(directory),
                               [](const auto& entry) { return entry.path().filename().string().starts_with("mobagen.lock.tmp-"); });
  }

}  // namespace

TEST_CASE("Module lockfile: serialization is canonical and independent of plugin order") {
  using namespace mobagen::modules;

  const auto fixture = make_resolved_fixture();
  constexpr std::string_view first_hash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr std::string_view second_hash = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  LockfileMetadata metadata{
      .sdk = {1, 2, 3},
      .target = TargetPlatform::Windows,
      .profile = "release",
      .manifest_hash = std::string(first_hash),
      .plugins = {
          {.provider = "customer.transfer",
           .version = {2, 0, 1},
           .abi_version = 1,
           .package = "plugins/customer-transfer.plugin",
           .hash = std::string(second_hash)},
          {.provider = "customer.color",
           .version = {1, 5, 0},
           .abi_version = 1,
           .package = "plugins/customer color.plugin",
           .hash = std::string(first_hash)},
      },
  };

  const auto serialized = serialize_lockfile(fixture.registry, fixture.resolution, metadata);
  std::ranges::reverse(metadata.plugins);
  const auto reversed = serialize_lockfile(fixture.registry, fixture.resolution, metadata);

  constexpr std::string_view expected
      = "schema: 1\n"
        "sdk: 1.2.3\n"
        "target: windows\n"
        "profile: release\n"
        "manifest: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "permissions:\n"
        "  - filesystem-read\n"
        "  - gpu\n"
        "  - windowing\n"
        "configurations:\n"
        "  mobagen.render.webgpu:\n"
        "    schema: mobagen.render.config.v1\n"
        "    hash: sha256:54ce6a0a614a7f41fd32f108e3c853947dd0dc97793b2a3691f3beb2837ee072\n"
        "resolved:\n"
        "  render.backend.v1:\n"
        "    provider: mobagen.render.webgpu\n"
        "    version: 1.4.2\n"
        "    linkage: static\n"
        "  window.surface.v1:\n"
        "    provider: mobagen.window.sdl3\n"
        "    version: 3.1.0\n"
        "    linkage: static\n"
        "dependencies:\n"
        "  - capability: window.surface.v1\n"
        "    provider: mobagen.window.sdl3\n"
        "    required-by: mobagen.render.webgpu\n"
        "plugins:\n"
        "  customer.color:\n"
        "    version: 1.5.0\n"
        "    abi: 1\n"
        "    package: \"plugins/customer color.plugin\"\n"
        "    hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "  customer.transfer:\n"
        "    version: 2.0.1\n"
        "    abi: 1\n"
        "    package: \"plugins/customer-transfer.plugin\"\n"
        "    hash: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n";

  REQUIRE(serialized.ok());
  REQUIRE(reversed.ok());
  CHECK(*serialized.contents == expected);
  CHECK(*reversed.contents == expected);

  const auto parsed = parse_lockfile(*serialized.contents, "mobagen.lock");
  REQUIRE(parsed.ok());
  CHECK(parsed.document->metadata.sdk == SemanticVersion{1, 2, 3});
  CHECK(parsed.document->metadata.target == TargetPlatform::Windows);
  CHECK(parsed.document->metadata.profile == "release");
  CHECK(parsed.document->metadata.manifest_hash == first_hash);
  REQUIRE(parsed.document->metadata.plugins.size() == 2);
  CHECK(parsed.document->metadata.plugins.front().provider == "customer.color");
  CHECK(parsed.document->permissions == std::vector<std::string>{"filesystem-read", "gpu", "windowing"});
  CHECK(parsed.document->resolved.size() == 2);
  CHECK(parsed.document->dependencies.size() == 1);
}

TEST_CASE("Module lockfile: invalid metadata returns issues without partial YAML") {
  using namespace mobagen::modules;

  const auto fixture = make_resolved_fixture();
  LockfileMetadata metadata{
      .schema = 9,
      .sdk = {1, 0, 0},
      .target = TargetPlatform::Windows,
      .profile = "Invalid Profile",
      .manifest_hash = "not-a-manifest-hash",
      .plugins = {
          {.provider = "customer.color",
           .version = {1, 0, 0},
           .abi_version = 1,
           .package = "../escape.plugin",
           .hash = "sha256:not-a-digest"},
          {.provider = "customer.color",
           .version = {1, 1, 0},
           .abi_version = 1,
           .package = "plugins/./customer.plugin",
           .hash = "sha256:short"},
      },
  };

  const auto result = serialize_lockfile(fixture.registry, fixture.resolution, metadata);

  CHECK_FALSE(result.ok());
  CHECK_FALSE(result.contents.has_value());
  CHECK(has_lockfile_issue(result, LockfileIssueCode::UnsupportedSchema, "schema"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::InvalidValue, "profile"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::InvalidHash, "manifest"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::InvalidValue, "plugins.customer.color.package"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::InvalidHash, "plugins.customer.color.hash"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::DuplicateEntry, "plugins.customer.color"));
}

TEST_CASE("Module lockfile: strict parsing rejects duplicate, unknown, and tagged fields") {
  using namespace mobagen::modules;
  constexpr std::string_view source = R"yaml(schema: 1
sdk: 1.0.0
target: windows
profile: release
manifest: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
manifest: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
permissions: []
configurations: {}
resolved: {}
dependencies: []
plugins: !include {}
extra: forbidden
)yaml";

  const auto parsed = parse_lockfile(source, "mobagen.lock");

  CHECK_FALSE(parsed.ok());
  CHECK(has_parse_issue(parsed, LockfileParseIssueCode::DuplicateKey, "manifest"));
  CHECK(has_parse_issue(parsed, LockfileParseIssueCode::UnsupportedTag, "plugins"));
  CHECK(has_parse_issue(parsed, LockfileParseIssueCode::UnknownField, "extra"));
}

TEST_CASE("Module lockfile: strict parsing rejects control characters in package paths") {
  using namespace mobagen::modules;
  constexpr std::string_view source = R"yaml(schema: 1
sdk: 1.0.0
target: windows
profile: release
manifest: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
permissions: []
configurations: {}
resolved: {}
dependencies: []
plugins:
  customer.color:
    version: 1.0.0
    abi: 1
    package: "plugins/customer\u001f.plugin"
    hash: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
)yaml";

  const auto parsed = parse_lockfile(source, "mobagen.lock");

  CHECK_FALSE(parsed.ok());
  CHECK(has_parse_issue(parsed, LockfileParseIssueCode::InvalidValue,
                        "plugins.customer.color.package"));
}

TEST_CASE("Module lockfile: atomic write replaces the complete destination") {
  using namespace mobagen::modules;

  TemporaryLockDirectory directory;
  const auto destination = directory.path() / "mobagen.lock";
  write_text(destination, "old lockfile\n");

  const auto result = write_lockfile_atomic(destination, "schema: 1\nprofile: release\n");

  REQUIRE(result.ok());
  CHECK(read_text(destination) == "schema: 1\nprofile: release\n");
  CHECK_FALSE(has_lockfile_temporary_file(directory.path()));
}

TEST_CASE("Module lockfile: atomic write rejects a destination without a filename") {
  using namespace mobagen::modules;

  const auto result = write_lockfile_atomic({}, "schema: 1\n");

  CHECK_FALSE(result.ok());
  CHECK(result.issue.has_value());
  CHECK(result.issue->code == LockfileWriteIssueCode::InvalidPath);
}

TEST_CASE("Module lockfile: failed atomic commit preserves the destination and removes its temporary file") {
  using namespace mobagen::modules;

  TemporaryLockDirectory directory;
  const auto destination = directory.path() / "mobagen.lock";
  REQUIRE(std::filesystem::create_directory(destination));
  const auto marker = destination / "keep.txt";
  write_text(marker, "preserve me");

  const auto result = write_lockfile_atomic(destination, "schema: 1\n");

  CHECK_FALSE(result.ok());
  CHECK(result.issue.has_value());
  CHECK(result.issue->code == LockfileWriteIssueCode::CommitFailed);
  CHECK(std::filesystem::is_directory(destination));
  CHECK(read_text(marker) == "preserve me");
  CHECK_FALSE(has_lockfile_temporary_file(directory.path()));
}

TEST_CASE("Module lockfile: bounded read returns exact canonical bytes") {
  using namespace mobagen::modules;

  TemporaryLockDirectory directory;
  const auto source = directory.path() / "mobagen.lock";
  write_text(source, "schema: 1\nprofile: release\n");

  const auto result = read_lockfile_bounded(source);

  REQUIRE(result.ok());
  CHECK(*result.contents == "schema: 1\nprofile: release\n");
}

TEST_CASE("Module lockfile: bounded read rejects missing directories and oversized inputs") {
  using namespace mobagen::modules;

  TemporaryLockDirectory directory;
  const auto missing = read_lockfile_bounded(directory.path() / "missing.lock");
  CHECK_FALSE(missing.ok());
  REQUIRE(missing.issue.has_value());
  CHECK(missing.issue->code == LockfileReadIssueCode::NotFound);

  const auto invalid = read_lockfile_bounded(directory.path());
  CHECK_FALSE(invalid.ok());
  REQUIRE(invalid.issue.has_value());
  CHECK(invalid.issue->code == LockfileReadIssueCode::InvalidPath);

  const auto oversized_path = directory.path() / "oversized.lock";
  write_text(oversized_path, std::string(max_lockfile_bytes + 1, 'x'));
  const auto oversized = read_lockfile_bounded(oversized_path);
  CHECK_FALSE(oversized.ok());
  REQUIRE(oversized.issue.has_value());
  CHECK(oversized.issue->code == LockfileReadIssueCode::TooLarge);
}
