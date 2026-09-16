#pragma once

#include "asset_id.hpp"
#include "resource/resource_registry.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>

namespace mobagen::assets {

  struct AssetInsertResult {
    resource::Handle handle{resource::kNullHandle};
    bool inserted{false};
  };

  template <class T> class AssetRegistry {
  public:
    [[nodiscard]] static constexpr resource::Handle null_handle() noexcept { return resource::kNullHandle; }

    template <class... Args> AssetInsertResult emplace(const AssetId& id, Args&&... args) {
      if (const auto existing = by_id_.find(id); existing != by_id_.end()) {
        if (storage_.valid(existing->second)) {
          return {existing->second, false};
        }
        by_id_.erase(existing);
      }

      const auto handle = storage_.create(Entry{id, T(std::forward<Args>(args)...)});
      try {
        const auto [entry, inserted] = by_id_.emplace(id, handle);
        if (!inserted) {
          storage_.release(handle);
          return {entry->second, false};
        }
      } catch (...) {
        storage_.release(handle);
        throw;
      }
      return {handle, true};
    }

    [[nodiscard]] std::optional<resource::Handle> find(const AssetId& id) const {
      const auto entry = by_id_.find(id);
      if (entry == by_id_.end() || !storage_.valid(entry->second)) {
        return std::nullopt;
      }
      return entry->second;
    }

    [[nodiscard]] bool valid(resource::Handle handle) const { return storage_.valid(handle); }

    [[nodiscard]] T* get(resource::Handle handle) {
      const auto entry = storage_.get(handle);
      return entry == nullptr ? nullptr : &entry->payload;
    }

    [[nodiscard]] const T* get(resource::Handle handle) const {
      const auto entry = storage_.get(handle);
      return entry == nullptr ? nullptr : &entry->payload;
    }

    bool release(resource::Handle handle) {
      const auto entry = storage_.get(handle);
      if (entry == nullptr) {
        return false;
      }

      const auto identity = entry->identity;
      if (const auto mapped = by_id_.find(identity); mapped != by_id_.end() && mapped->second == handle) {
        by_id_.erase(mapped);
      }
      storage_.release(handle);
      return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.capacity(); }

  private:
    struct Entry {
      AssetId identity{};
      T payload{};
    };

    resource::ResourceRegistry<Entry> storage_;
    std::unordered_map<AssetId, resource::Handle, AssetIdHash> by_id_;
  };

}  // namespace mobagen::assets
