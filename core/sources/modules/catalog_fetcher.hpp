#pragma once

#include "catalog_parser.hpp"
#include "descriptor.hpp"
#include "http/client.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mobagen::modules {

  enum class CatalogFetchIssueCode : std::uint8_t { InvalidProduct, Transport, HttpStatus, LimitExceeded, InvalidCatalog };

  struct CatalogFetchIssue {
    CatalogFetchIssueCode code{};
    std::string source_name;
    std::string message;
    std::vector<DescriptorIssue> descriptor_issues;
    std::vector<CatalogError> catalog_errors;
    http::Error transport_error;
  };

  struct FetchedModuleCatalog {
    ModuleSourceDescriptor source;
    ModuleCatalogDescriptor catalog;
  };

  struct CatalogFetchResult {
    std::vector<FetchedModuleCatalog> catalogs;
    std::vector<CatalogFetchIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  [[nodiscard]] CatalogFetchResult fetch_module_catalogs(const ProductDescriptor& product, http::Client& client);

}  // namespace mobagen::modules
