#pragma once

#include "module_manager.hpp"

#include "modules/lockfile.hpp"
#include "modules/lockfile_verifier.hpp"
#include "modules/manifest_parser.hpp"

#include <mobagen/version.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mobagen::compositions {

  struct LockedPortableProjectOptions {
    modules::SemanticVersion sdk_version{MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR, MOBAGEN_SDK_VERSION_PATCH};
    modules::TargetPlatform target{};
    std::string profile;
  };

  enum class LockedPortableProjectIssueCode : std::uint8_t {
    ReadManifest,
    ParseManifest,
    HashManifest,
    ReadLock,
    ParseLock,
    ProjectMismatch,
    VerifyLock,
    ActivationPlan,
    Configuration,
    ModuleManager,
  };

  struct LockedPortableProjectIssue {
    LockedPortableProjectIssueCode code{};
    std::string message;
    std::vector<modules::ManifestError> manifest_errors;
    std::optional<modules::LockfileReadIssue> lockfile_read_issue;
    std::vector<modules::LockfileParseIssue> lockfile_parse_issues;
    std::vector<modules::LockfileVerificationIssue> verification_issues;
    std::vector<modules::LockedActivationPlanIssue> activation_plan_issues;
    std::vector<PortableModuleManagerIssue> manager_issues;
  };

  struct LockedPortableProjectResult {
    std::unique_ptr<PortableModuleManager> manager;
    std::optional<modules::ProductDescriptor> product;
    std::vector<LockedPortableProjectIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return manager != nullptr && product.has_value() && issues.empty(); }
  };

  /* Opens a bootstrapped project from bounded local metadata. No plugin binary
     bytes are read and no WASM instance is created until manager.activate(). */
  [[nodiscard]] LockedPortableProjectResult open_locked_portable_project(const std::filesystem::path& manifest_path,
                                                                         LockedPortableProjectOptions options, plugins::PortableWasmBackend& backend,
                                                                         std::span<const modules::ProviderDescriptor> builtin_providers = {},
                                                                         plugins::WasmHostServices host_services = {});

}  // namespace mobagen::compositions
