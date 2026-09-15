#pragma once

#include "modules/descriptor.hpp"
#include "modules/manifest_parser.hpp"
#include "plugins/wasm_host_imports.hpp"

#include <mobagen/version.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::plugins {
  class PortableWasmBackend;
}

namespace mobagen::compositions {
  class NativeModuleManager;
  class PortableModuleManager;

  enum class ProjectModuleRuntimeKind : std::uint8_t { Native, Portable };

  struct LockedProjectOptions {
    modules::SemanticVersion sdk_version{
        MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR,
        MOBAGEN_SDK_VERSION_PATCH
    };
    modules::TargetPlatform target{};
    std::string profile;
  };

  struct LockedProjectServices {
    plugins::PortableWasmBackend* portable_backend{};
    std::span<const modules::ProviderDescriptor> builtin_providers{};
    plugins::WasmHostServices wasm_host_services{};
  };

  enum class LockedProjectIssueCode : std::uint8_t {
    ReadManifest,
    ParseManifest,
    ProfileUnavailable,
    UnsupportedLinkage,
    PortableBackendUnavailable,
    NativeProject,
    PortableProject,
  };

  struct LockedProjectIssue {
    LockedProjectIssueCode code{};
    std::string message;
    std::vector<modules::ManifestError> manifest_errors;
  };

  struct ProjectModuleManagerIssue {
    std::string provider_id;
    std::string message;
  };

  struct ProjectModuleManagerActionResult {
    std::vector<ProjectModuleManagerIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct LockedProjectResult;

  class ProjectModuleManager {
  public:
    ProjectModuleManager(const ProjectModuleManager&) = delete;
    ProjectModuleManager& operator=(const ProjectModuleManager&) = delete;
    ProjectModuleManager(ProjectModuleManager&&) noexcept;
    ProjectModuleManager& operator=(ProjectModuleManager&&) noexcept;
    ~ProjectModuleManager();

    [[nodiscard]] ProjectModuleRuntimeKind kind() const noexcept;
    [[nodiscard]] ProjectModuleManagerActionResult activate(
        std::string_view capability
    );
    [[nodiscard]] ProjectModuleManagerActionResult stop();
    [[nodiscard]] std::size_t active_count() const noexcept;
    [[nodiscard]] NativeModuleManager* native() noexcept;
    [[nodiscard]] const NativeModuleManager* native() const noexcept;
    [[nodiscard]] PortableModuleManager* portable() noexcept;
    [[nodiscard]] const PortableModuleManager* portable() const noexcept;

  private:
    struct Storage;

    explicit ProjectModuleManager(std::unique_ptr<Storage> storage) noexcept;

    std::unique_ptr<Storage> storage_;

    friend LockedProjectResult open_locked_project(
        const std::filesystem::path&, LockedProjectOptions, LockedProjectServices
    );
  };

  struct LockedProjectResult {
    std::unique_ptr<ProjectModuleManager> manager;
    std::optional<modules::ProductDescriptor> product;
    std::vector<LockedProjectIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
      return manager != nullptr && product.has_value() && issues.empty();
    }
  };

  /* Reads only bounded project metadata and chooses the module manager from the
     selected profile. Plugin code remains cold until activate(capability). */
  [[nodiscard]] LockedProjectResult open_locked_project(
      const std::filesystem::path& manifest_path, LockedProjectOptions options,
      LockedProjectServices services = {}
  );

}  // namespace mobagen::compositions
