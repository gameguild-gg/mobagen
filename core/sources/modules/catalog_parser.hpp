#pragma once

#include "catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::modules {

  inline constexpr std::size_t max_module_catalog_bytes = 1024 * 1024;

  enum class CatalogErrorCode : std::uint8_t {
    Syntax,
    DuplicateKey,
    UnsupportedTag,
    UnknownField,
    MissingField,
    WrongType,
    UnsupportedSchema,
    InvalidValue,
    LimitExceeded,
    DuplicateEntry,
  };

  struct CatalogError {
    CatalogErrorCode code{};
    std::string source_path;
    std::size_t line{};
    std::size_t column{};
    std::string field;
    std::string message;
  };

  struct CatalogParseResult {
    std::optional<ModuleCatalogDescriptor> catalog;
    std::vector<CatalogError> errors;

    [[nodiscard]] bool ok() const noexcept { return catalog.has_value(); }
  };

  [[nodiscard]] CatalogParseResult parse_module_catalog(std::string_view source, std::string_view source_path = "catalog.yaml");

}  // namespace mobagen::modules
