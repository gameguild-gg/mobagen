#include "catalog_fetcher.hpp"

#include <chrono>
#include <exception>
#include <string_view>
#include <utility>

namespace mobagen::modules {
  namespace {

    constexpr std::chrono::milliseconds catalog_connect_timeout{5000};
    constexpr std::chrono::milliseconds catalog_transfer_timeout{30000};

    CatalogFetchResult failure(CatalogFetchIssueCode code, std::string source_name, std::string message,
                               std::vector<DescriptorIssue> descriptor_issues = {}, std::vector<CatalogError> catalog_errors = {},
                               http::Error transport_error = {}) {
      CatalogFetchResult result;
      result.issues.push_back(
          {code, std::move(source_name), std::move(message), std::move(descriptor_issues), std::move(catalog_errors), std::move(transport_error)});
      return result;
    }

  }  // namespace

  CatalogFetchResult fetch_module_catalogs(const ProductDescriptor& product, http::Client& client) {
    auto descriptor_issues = validate(product);
    if (!descriptor_issues.empty()) {
      return failure(CatalogFetchIssueCode::InvalidProduct, {}, "product descriptor is invalid", std::move(descriptor_issues));
    }

    std::vector<FetchedModuleCatalog> staged;
    staged.reserve(product.sources.size());
    for (const auto& source : product.sources) {
      http::GetResult fetched;
      try {
        fetched = client.get({
            .url = source.url,
            .max_response_bytes = max_module_catalog_bytes,
            .connect_timeout = catalog_connect_timeout,
            .transfer_timeout = catalog_transfer_timeout,
            .max_redirects = 0,
        });
      } catch (const std::exception& exception) {
        return failure(CatalogFetchIssueCode::Transport, source.name, "HTTPS client threw while fetching module catalog", {}, {},
                       {http::ErrorCode::Transfer, exception.what()});
      } catch (...) {
        return failure(CatalogFetchIssueCode::Transport, source.name, "HTTPS client threw while fetching module catalog", {}, {},
                       {http::ErrorCode::Transfer, "unknown HTTPS client failure"});
      }

      if (!fetched.ok()) {
        auto transport_error = fetched.error.value_or(http::Error{http::ErrorCode::Transfer, "HTTPS client returned no response"});
        return failure(CatalogFetchIssueCode::Transport, source.name, "module catalog HTTPS request failed", {}, {}, std::move(transport_error));
      }
      if (fetched.response->status != 200) {
        return failure(CatalogFetchIssueCode::HttpStatus, source.name,
                       "module catalog server returned HTTP status " + std::to_string(fetched.response->status));
      }
      if (fetched.response->body.size() > max_module_catalog_bytes) {
        return failure(CatalogFetchIssueCode::LimitExceeded, source.name, "module catalog response exceeds the 1 MiB limit");
      }

      const auto* bytes = fetched.response->body.empty() ? "" : reinterpret_cast<const char*>(fetched.response->body.data());
      auto parsed = parse_module_catalog(std::string_view{bytes, fetched.response->body.size()}, source.name + "/catalog.yaml");
      if (!parsed.ok()) {
        return failure(CatalogFetchIssueCode::InvalidCatalog, source.name, "module catalog response is invalid", {}, std::move(parsed.errors));
      }
      staged.push_back({source, std::move(*parsed.catalog)});
    }

    return {.catalogs = std::move(staged)};
  }

}  // namespace mobagen::modules
