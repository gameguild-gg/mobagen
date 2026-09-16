#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "descriptor.hpp"

namespace mobagen::modules {

  inline constexpr std::size_t max_product_manifest_bytes = 1024 * 1024;
  inline constexpr std::size_t max_manifest_collection_entries = 1024;

  enum class ManifestErrorCode : std::uint8_t {
    Syntax,
    DuplicateKey,
    UnsupportedTag,
    UnknownField,
    MissingField,
    WrongType,
    UnsupportedSchema,
    InvalidValue,
    LimitExceeded,
  };

  struct ManifestError {
    ManifestErrorCode code{};
    std::string source_path;
    std::size_t line{};
    std::size_t column{};
    std::string field;
    std::string message;
  };

  struct ManifestParseResult {
    std::optional<ProductDescriptor> descriptor;
    std::vector<ManifestError> errors;

    [[nodiscard]] bool ok() const noexcept { return descriptor.has_value(); }
  };

  [[nodiscard]] ManifestParseResult parse_product_manifest(std::string_view source, std::string_view source_path = "mobagen.yaml");

}  // namespace mobagen::modules
