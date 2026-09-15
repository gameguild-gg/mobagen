#include "assets/asset_store_service.hpp"

#include <mobagen/plugin/asset_store_v1.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace {

  constexpr std::size_t max_cache_root_bytes = 4096;

  struct DefaultAssetStorePluginState {
    MobagenHostApiV1 host{};
    std::unique_ptr<mobagen::assets::AssetCache> cache;
    std::unique_ptr<mobagen::assets::NativeAssetStoreService> service;
  };

  MobagenStatus MOBAGEN_PLUGIN_CALL configure(
      void* opaque, const MobagenHostApiV1* host, MobagenByteView configuration
  ) noexcept {
    auto* state = static_cast<DefaultAssetStorePluginState*>(opaque);
    if (state == nullptr || host == nullptr || host->publish_capability == nullptr
        || configuration.size > max_cache_root_bytes
        || (configuration.size != 0 && configuration.data == nullptr)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }

    try {
      const auto* configuration_data = configuration.data == nullptr
                                           ? ""
                                           : reinterpret_cast<const char*>(
                                                 configuration.data
                                             );
      const auto configured_root =
          std::string_view{configuration_data, configuration.size};
      if (configured_root.find('\0') != std::string_view::npos) {
        return MOBAGEN_STATUS_INVALID_ARGUMENT;
      }
      const auto root = configured_root.empty()
                            ? std::filesystem::path{".mobagen/cache"}
                            : std::filesystem::path{configured_root};
      state->service.reset();
      state->cache = std::make_unique<mobagen::assets::AssetCache>(root);
      state->service =
          std::make_unique<mobagen::assets::NativeAssetStoreService>(*state->cache);
      const auto& api = state->service->api();
      const auto published = host->publish_capability(
          host->host_context,
          {MOBAGEN_ASSET_STORE_V1_ID, sizeof(MOBAGEN_ASSET_STORE_V1_ID) - 1},
          MOBAGEN_ASSET_STORE_V1_ABI_VERSION, &api, MOBAGEN_ASSET_STORE_V1_SIZE
      );
      if (published != MOBAGEN_STATUS_OK) {
        state->service.reset();
        state->cache.reset();
      }
      return published;
    } catch (const std::bad_alloc&) {
      state->service.reset();
      state->cache.reset();
      return MOBAGEN_STATUS_OUT_OF_MEMORY;
    } catch (...) {
      state->service.reset();
      state->cache.reset();
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL start(void* opaque) noexcept {
    auto* state = static_cast<DefaultAssetStorePluginState*>(opaque);
    if (state == nullptr || state->service == nullptr) {
      return MOBAGEN_STATUS_FAILED;
    }
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL quiesce(void* opaque) noexcept {
    auto* state = static_cast<DefaultAssetStorePluginState*>(opaque);
    if (state == nullptr) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    return MOBAGEN_STATUS_OK;
  }

  void MOBAGEN_PLUGIN_CALL stop(void* opaque) noexcept {
    auto* state = static_cast<DefaultAssetStorePluginState*>(opaque);
    if (state == nullptr) {
      return;
    }
    state->service.reset();
    state->cache.reset();
  }

  void MOBAGEN_PLUGIN_CALL destroy(void* opaque) noexcept {
    auto* state = static_cast<DefaultAssetStorePluginState*>(opaque);
    if (state == nullptr) {
      return;
    }
    const auto host = state->host;
    state->~DefaultAssetStorePluginState();
    host.deallocate(
        host.host_context, state, sizeof(DefaultAssetStorePluginState),
        alignof(DefaultAssetStorePluginState)
    );
  }

}  // namespace

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(
    const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor
) {
  static const MobagenStringView provides[] = {
      {MOBAGEN_ASSET_STORE_V1_ID, sizeof(MOBAGEN_ASSET_STORE_V1_ID) - 1}
  };
  static const MobagenStringView permissions[] = {
      {"filesystem-read", sizeof("filesystem-read") - 1}
  };
  if (host == nullptr || descriptor == nullptr
      || host->abi_version != MOBAGEN_PLUGIN_ABI_VERSION
      || host->struct_size < MOBAGEN_PLUGIN_HOST_API_V1_SIZE
      || host->allocate == nullptr || host->deallocate == nullptr
      || descriptor->struct_size < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_STATUS_UNSUPPORTED;
  }

  auto* memory = host->allocate(
      host->host_context, sizeof(DefaultAssetStorePluginState),
      alignof(DefaultAssetStorePluginState)
  );
  if (memory == nullptr) {
    return MOBAGEN_STATUS_OUT_OF_MEMORY;
  }
  auto* state = new (memory) DefaultAssetStorePluginState{.host = *host};
  *descriptor = {
      .struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE,
      .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
      .id = {"mobagen.assets.default", sizeof("mobagen.assets.default") - 1},
      .version_major = 1,
      .version_minor = 0,
      .version_patch = 0,
      .reload_policy = MOBAGEN_RELOAD_RESTART,
      .plugin_state = state,
      .provides = provides,
      .provides_count = 1,
      .required = nullptr,
      .required_count = 0,
      .optional = nullptr,
      .optional_count = 0,
      .conflicts = nullptr,
      .conflicts_count = 0,
      .lifecycle =
          {
              .struct_size = MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE,
              .configure = configure,
              .start = start,
              .quiesce = quiesce,
              .stop = stop,
              .destroy = destroy,
          },
      .configuration_schema = {
          "mobagen.assets.default.config.v1",
          sizeof("mobagen.assets.default.config.v1") - 1
      },
      .permissions = permissions,
      .permissions_count = 1,
  };
  return MOBAGEN_STATUS_OK;
}
