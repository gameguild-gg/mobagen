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
#include <string>
#include <vector>

namespace mobagen::compositions {

  struct LockedNativeProjectOptions {
    modules::SemanticVersion sdk_version{MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR, MOBAGEN_SDK_VERSION_PATCH};
    modules::TargetPlatform target{};
    std::string profile;
  };

  enum class LockedNativeProjectIssueCode : std::uint8_t {
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

  struct LockedNativeProjectIssue {
    LockedNativeProjectIssueCode code{};
    std::string message;
    std::vector<modules::ManifestError> manifest_errors;
    std::optional<modules::LockfileReadIssue> lockfile_read_issue;
    std::vector<modules::LockfileParseIssue> lockfile_parse_issues;
    std::vector<modules::LockfileVerificationIssue> verification_issues;
    std::vector<modules::LockedActivationPlanIssue> activation_plan_issues;
    std::vector<NativeModuleManagerIssue> manager_issues;
  };

  struct LockedNativeProjectResult {
    std::unique_ptr<NativeModuleManager> manager;
    std::optional<modules::ProductDescriptor> product;
    std::vector<LockedNativeProjectIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return manager != nullptr && product.has_value() && issues.empty(); }
  };

  /* Opens a bootstrapped project from local verified metadata without loading plugin code. */
  [[nodiscard]] LockedNativeProjectResult open_locked_native_project(const std::filesystem::path& manifest_path, LockedNativeProjectOptions options,
                                                                     plugins::PluginLogSink log_sink = nullptr, void* log_context = nullptr);

}  // namespace mobagen::compositions
