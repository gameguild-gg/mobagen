#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "resolver.hpp"

namespace mobagen::modules {

  inline constexpr std::uint32_t lockfile_schema_version = 1;

  struct PluginLockEntry {
    std::string provider;
    SemanticVersion version;
    std::uint32_t abi_version{};
    std::string hash;
  };

  struct LockfileMetadata {
    std::uint32_t schema{lockfile_schema_version};
    SemanticVersion sdk;
    TargetPlatform target{};
    std::string profile;
    std::vector<PluginLockEntry> plugins;
  };

  enum class LockfileIssueCode : std::uint8_t {
    UnsupportedSchema,
    InvalidValue,
    InvalidHash,
    DuplicateEntry,
    InvalidResolution,
  };

  struct LockfileIssue {
    LockfileIssueCode code{};
    std::string field;
    std::string message;
  };

  struct LockfileSerializeResult {
    std::optional<std::string> contents;
    std::vector<LockfileIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return contents.has_value(); }
  };

  [[nodiscard]] LockfileSerializeResult serialize_lockfile(const CapabilityRegistry& registry, const ModuleResolution& resolution,
                                                           const LockfileMetadata& metadata);

  enum class LockfileWriteIssueCode : std::uint8_t {
    InvalidPath,
    CreateFailed,
    WriteFailed,
    CommitFailed,
  };

  struct LockfileWriteIssue {
    LockfileWriteIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
  };

  struct LockfileWriteResult {
    std::optional<LockfileWriteIssue> issue;

    [[nodiscard]] bool ok() const noexcept { return !issue.has_value(); }
  };

  [[nodiscard]] LockfileWriteResult write_lockfile_atomic(const std::filesystem::path& destination, std::string_view contents);

}  // namespace mobagen::modules
