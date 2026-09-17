#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mobagen::assets {

  struct AssetId {
    std::array<std::uint8_t, 32> bytes{};

    friend constexpr bool operator==(const AssetId&, const AssetId&) = default;
  };

  struct AssetIdHash {
    [[nodiscard]] std::size_t operator()(const AssetId& id) const noexcept;
  };

  class Sha256Hasher {
  public:
    Sha256Hasher() noexcept;

    [[nodiscard]] bool update(std::span<const std::byte> data) noexcept;
    [[nodiscard]] std::optional<AssetId> finish() noexcept;

  private:
    void compress(const std::byte* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::uint64_t total_bytes_{0};
    std::size_t buffered_{0};
    bool finished_{false};
  };

  [[nodiscard]] std::optional<AssetId> sha256(std::span<const std::byte> data) noexcept;
  [[nodiscard]] std::optional<AssetId> parse_asset_id(std::string_view value) noexcept;
  [[nodiscard]] std::string to_string(const AssetId& id);

}  // namespace mobagen::assets
