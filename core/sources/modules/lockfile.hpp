#pragma once

#include <cstddef>
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
  inline constexpr std::size_t max_lockfile_bytes = 1024 * 1024;

  struct PluginLockEntry {
    std::string provider;
    SemanticVersion version;
    std::uint32_t abi_version{};
    std::string package;
    std::string hash;
  };

  struct LockfileMetadata {
    std::uint32_t schema{lockfile_schema_version};
    SemanticVersion sdk;
    TargetPlatform target{};
    std::string profile;
    std::string manifest_hash;
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

  struct LockedConfiguration {
    std::string provider;
    std::string schema;
    std::string hash;
  };

  struct LockedProviderSelection {
    std::string capability;
    std::string provider;
    SemanticVersion version;
    LinkageMode linkage{};
  };

  struct LockedDependency {
    std::string capability;
    std::string provider;
    std::string required_by;
  };

  struct LockfileDocument {
    LockfileMetadata metadata;
    std::vector<std::string> permissions;
    std::vector<LockedConfiguration> configurations;
    std::vector<LockedProviderSelection> resolved;
    std::vector<LockedDependency> dependencies;
  };

  enum class LockfileParseIssueCode : std::uint8_t {
    Syntax,
    DuplicateKey,
    UnsupportedTag,
    UnknownField,
    MissingField,
    WrongType,
    UnsupportedSchema,
    InvalidValue,
    InvalidHash,
    LimitExceeded,
    DuplicateEntry,
  };

  struct LockfileParseIssue {
    LockfileParseIssueCode code{};
    std::string source_path;
    std::size_t line{};
    std::size_t column{};
    std::string field;
    std::string message;
  };

  struct LockfileParseResult {
    std::optional<LockfileDocument> document;
    std::vector<LockfileParseIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return document.has_value() && issues.empty(); }
  };

  [[nodiscard]] LockfileParseResult parse_lockfile(std::string_view source, std::string_view source_path = "mobagen.lock");

  enum class LockfileReadIssueCode : std::uint8_t {
    InvalidPath,
    NotFound,
    TooLarge,
    ReadFailed,
    Changed,
  };

  struct LockfileReadIssue {
    LockfileReadIssueCode code{};
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
  };

  struct LockfileReadResult {
    std::optional<std::string> contents;
    std::optional<LockfileReadIssue> issue;

    [[nodiscard]] bool ok() const noexcept { return contents.has_value() && !issue.has_value(); }
  };

  [[nodiscard]] LockfileReadResult read_lockfile_bounded(const std::filesystem::path& source);

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
