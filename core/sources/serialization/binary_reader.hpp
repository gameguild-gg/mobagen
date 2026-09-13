#pragma once

#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>

namespace serialization {

  class BinaryReader {
  public:
    explicit BinaryReader(std::span<const std::byte> bytes, std::size_t offset = 0) noexcept
        : bytes_(bytes), offset_(offset) {}

    template <class T> [[nodiscard]] bool read(T& out) noexcept {
      static_assert(std::is_trivially_copyable_v<T>, "BinaryReader supports POD values only");
      if (offset_ > bytes_.size() || sizeof(T) > bytes_.size() - offset_) return false;

      std::memcpy(&out, bytes_.data() + offset_, sizeof(T));
      offset_ += sizeof(T);
      return true;
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

    [[nodiscard]] std::size_t remaining() const noexcept {
      return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0;
    }

  private:
    std::span<const std::byte> bytes_;
    std::size_t offset_ = 0;
  };

}  // namespace serialization
