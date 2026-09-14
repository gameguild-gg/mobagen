#pragma once

#include "lockfile.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace mobagen::modules {

  enum class LockfileVerificationIssueCode : std::uint8_t {
    InvalidRoot,
    MetadataMismatch,
    InvalidResolution,
    InvalidPackagePath,
    MissingPackage,
    InvalidPackage,
    UnsupportedAbi,
    PackageTooLarge,
    ReadFailed,
    HashMismatch,
    Changed,
  };

  struct LockfileVerificationIssue {
    LockfileVerificationIssueCode code{};
    std::string provider_id;
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
  };

  struct LockfileVerificationContext {
    SemanticVersion sdk;
    TargetPlatform target{};
    std::string profile;
    std::string manifest_hash;
  };

  struct VerifiedLockedPlugin {
    std::string provider_id;
    SemanticVersion version;
    LinkageMode linkage{};
    std::uint32_t abi_version{};
    std::uint64_t size{};
    std::filesystem::path package_path;
    std::filesystem::path binary_path;
  };

  struct LockfileVerificationResult {
    std::vector<VerifiedLockedPlugin> plugins;
    std::vector<LockfileVerificationIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  /* Verifies a parsed lock and its package bytes without loading native or WASM code. */
  [[nodiscard]] LockfileVerificationResult verify_locked_project(
      const LockfileDocument& document, const std::filesystem::path& project_root,
      const LockfileVerificationContext& context
  );

}  // namespace mobagen::modules
