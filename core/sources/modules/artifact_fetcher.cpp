#include "artifact_fetcher.hpp"

#include <exception>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::modules {
  namespace {

    struct HttpArtifactSource {
      http::Client* client{};
      http::GetRequest request;
      std::optional<http::StreamGetResult> result;
    };

    bool forward_chunk(void* context, std::span<const std::byte> bytes) noexcept {
      auto& sink = *static_cast<assets::AssetCacheSink*>(context);
      return sink.write(sink.context, bytes);
    }

    bool produce_artifact(void* context, assets::AssetCacheSink sink) noexcept {
      auto& source = *static_cast<HttpArtifactSource*>(context);
      try {
        source.result = source.client->get_stream(
            source.request, {.context = &sink, .write = forward_chunk}
        );
      } catch (const std::exception& exception) {
        source.result = http::StreamGetResult{
            .error = http::Error{http::ErrorCode::Transfer, exception.what()},
        };
      } catch (...) {
        source.result = http::StreamGetResult{
            .error = http::Error{http::ErrorCode::Transfer, "unknown HTTPS client failure"},
        };
      }
      return source.result->ok() && source.result->response->status == 200;
    }

    ArtifactFetchResult failure(ArtifactFetchIssue issue) {
      ArtifactFetchResult result;
      result.issues.push_back(std::move(issue));
      return result;
    }

  }  // namespace

  ArtifactFetchResult fetch_module_artifacts(const ModuleCatalogIndex& catalog,
                                             const ModuleResolution& resolution, http::Client& client,
                                             const assets::AssetCache& cache, ArtifactFetchOptions options) {
    if (catalog.registry().generation() != resolution.registry_generation()) {
      return failure({
          .code = ArtifactFetchIssueCode::InvalidPlan,
          .message = "module resolution belongs to a different capability registry",
      });
    }
    if (options.connect_timeout.count() <= 0 || options.transfer_timeout.count() <= 0) {
      return failure({
          .code = ArtifactFetchIssueCode::InvalidPlan,
          .message = "artifact fetch timeouts must be positive",
      });
    }

    std::vector<CachedModuleArtifact> staged;
    staged.reserve(resolution.lifecycle_order().size());
    for (const auto provider_index : resolution.lifecycle_order()) {
      const auto* provider = catalog.registry().provider(provider_index);
      const auto* artifact = catalog.artifact_for(provider_index);
      if (provider == nullptr || artifact == nullptr) {
        return failure({
            .code = ArtifactFetchIssueCode::InvalidPlan,
            .message = "selected provider or artifact is outside the module catalog",
        });
      }
      const auto expected_id = assets::parse_asset_id(artifact->hash);
      if (!expected_id.has_value() || artifact->size == 0
          || artifact->size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return failure({
            .code = ArtifactFetchIssueCode::InvalidArtifact,
            .provider_id = provider->id,
            .message = "selected artifact hash or size is invalid",
        });
      }

      HttpArtifactSource source{
          .client = &client,
          .request = {
              .url = artifact->url,
              .max_response_bytes = static_cast<std::size_t>(artifact->size),
              .connect_timeout = options.connect_timeout,
              .transfer_timeout = options.transfer_timeout,
              .max_redirects = 0,
          },
      };
      const auto stored = cache.store_stream(
          *expected_id, static_cast<std::size_t>(artifact->size),
          {.context = &source, .produce = produce_artifact}
      );
      if (!stored.ok()) {
        if (source.result.has_value() && source.result->response.has_value()
            && source.result->response->status != 200) {
          return failure({
              .code = ArtifactFetchIssueCode::HttpStatus,
              .provider_id = provider->id,
              .message = "plugin artifact server returned a non-success HTTP status",
              .http_status = source.result->response->status,
              .cache_status = stored.status,
          });
        }
        if (source.result.has_value() && source.result->error.has_value()) {
          if (source.result->error->code == http::ErrorCode::SinkRejected
              && stored.status == assets::AssetCacheStatus::io_error) {
            return failure({
                .code = ArtifactFetchIssueCode::CacheFailure,
                .provider_id = provider->id,
                .message = "plugin artifact cache rejected streamed bytes",
                .transport_error = std::move(source.result->error),
                .cache_status = stored.status,
            });
          }
          const auto code = source.result->error->code == http::ErrorCode::LimitExceeded
                                || source.result->error->code == http::ErrorCode::SinkRejected
                            ? ArtifactFetchIssueCode::SizeMismatch
                            : ArtifactFetchIssueCode::Transport;
          return failure({
              .code = code,
              .provider_id = provider->id,
              .message = code == ArtifactFetchIssueCode::SizeMismatch
                             ? "plugin artifact exceeded its declared size"
                             : "plugin artifact HTTPS request failed",
              .transport_error = std::move(source.result->error),
              .cache_status = stored.status,
          });
        }
        if (source.result.has_value() && source.result->response.has_value()
            && source.result->response->body_bytes != artifact->size) {
          return failure({
              .code = ArtifactFetchIssueCode::SizeMismatch,
              .provider_id = provider->id,
              .message = "plugin artifact size differs from its catalog metadata",
              .cache_status = stored.status,
          });
        }
        if (stored.status == assets::AssetCacheStatus::source_changed) {
          return failure({
              .code = ArtifactFetchIssueCode::IntegrityMismatch,
              .provider_id = provider->id,
              .message = "plugin artifact SHA-256 differs from its catalog metadata",
              .cache_status = stored.status,
          });
        }
        return failure({
            .code = ArtifactFetchIssueCode::CacheFailure,
            .provider_id = provider->id,
            .message = "plugin artifact could not be committed to the content cache",
            .cache_status = stored.status,
        });
      }

      staged.push_back({
          .provider_id = provider->id,
          .version = provider->version,
          .id = *expected_id,
          .size = artifact->size,
          .linkage = artifact->linkage,
          .abi_version = artifact->abi_version,
          .cache_path = stored.path,
          .downloaded = stored.status == assets::AssetCacheStatus::stored,
      });
    }
    return {.artifacts = std::move(staged)};
  }

}  // namespace mobagen::modules
