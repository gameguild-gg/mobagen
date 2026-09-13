#pragma once

#include <cstdint>
#include <type_traits>

namespace resource {

  // Stable value type shared across registries, ECS metadata, render commands,
  // persistence, and eventually the C plugin ABI.
  struct Handle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0xFFFFFFFFu;

    friend constexpr bool operator==(Handle, Handle) = default;
  };

  inline constexpr Handle kNullHandle{};

  static_assert(std::is_standard_layout_v<Handle>);
  static_assert(std::is_trivially_copyable_v<Handle>);
  static_assert(sizeof(Handle) == sizeof(std::uint32_t) * 2);

}  // namespace resource
