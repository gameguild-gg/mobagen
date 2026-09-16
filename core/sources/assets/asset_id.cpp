#include "asset_id.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace mobagen::assets {
  namespace {

    constexpr std::array<std::uint32_t, 64> round_constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU,
        0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU,
        0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U,
        0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
        0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    constexpr char hex_digits[] = "0123456789abcdef";
    constexpr std::string_view asset_id_prefix = "sha256:";
    constexpr std::size_t digest_text_size = 64;

    [[nodiscard]] constexpr std::uint32_t choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept { return (x & y) ^ (~x & z); }

    [[nodiscard]] constexpr std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept { return (x & y) ^ (x & z) ^ (y & z); }

    [[nodiscard]] constexpr std::uint32_t big_sigma_zero(std::uint32_t value) noexcept {
      return std::rotr(value, 2) ^ std::rotr(value, 13) ^ std::rotr(value, 22);
    }

    [[nodiscard]] constexpr std::uint32_t big_sigma_one(std::uint32_t value) noexcept {
      return std::rotr(value, 6) ^ std::rotr(value, 11) ^ std::rotr(value, 25);
    }

    [[nodiscard]] constexpr std::uint32_t small_sigma_zero(std::uint32_t value) noexcept {
      return std::rotr(value, 7) ^ std::rotr(value, 18) ^ (value >> 3U);
    }

    [[nodiscard]] constexpr std::uint32_t small_sigma_one(std::uint32_t value) noexcept {
      return std::rotr(value, 17) ^ std::rotr(value, 19) ^ (value >> 10U);
    }

    [[nodiscard]] constexpr std::uint8_t decode_hex(char value) noexcept {
      return value >= '0' && value <= '9' ? static_cast<std::uint8_t>(value - '0') : static_cast<std::uint8_t>(value - 'a' + 10);
    }

  }  // namespace

  Sha256Hasher::Sha256Hasher() noexcept
      : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

  bool Sha256Hasher::update(std::span<const std::byte> data) noexcept {
    constexpr std::uint64_t max_bytes = std::numeric_limits<std::uint64_t>::max() / 8U;
    if (finished_ || data.size() > max_bytes - total_bytes_) {
      return false;
    }

    total_bytes_ += static_cast<std::uint64_t>(data.size());
    std::size_t offset = 0;

    if (buffered_ != 0) {
      const auto copied = std::min(buffer_.size() - buffered_, data.size());
      if (copied != 0) {
        std::memcpy(buffer_.data() + buffered_, data.data(), copied);
      }
      buffered_ += copied;
      offset += copied;
      if (buffered_ == buffer_.size()) {
        compress(buffer_.data());
        buffered_ = 0;
      }
    }

    while (data.size() - offset >= buffer_.size()) {
      compress(data.data() + offset);
      offset += buffer_.size();
    }

    const auto remaining = data.size() - offset;
    if (remaining != 0) {
      std::memcpy(buffer_.data(), data.data() + offset, remaining);
      buffered_ = remaining;
    }
    return true;
  }

  std::optional<AssetId> Sha256Hasher::finish() noexcept {
    if (finished_) {
      return std::nullopt;
    }
    finished_ = true;

    const auto bit_count = total_bytes_ * 8U;
    buffer_[buffered_++] = std::byte{0x80};
    if (buffered_ > 56) {
      std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.end(), std::byte{0});
      compress(buffer_.data());
      buffered_ = 0;
    }

    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.begin() + 56, std::byte{0});
    for (std::size_t index = 0; index < sizeof(bit_count); ++index) {
      buffer_[63 - index] = std::byte{static_cast<std::uint8_t>(bit_count >> (index * 8U))};
    }
    compress(buffer_.data());

    AssetId result;
    for (std::size_t word = 0; word < state_.size(); ++word) {
      for (std::size_t byte = 0; byte < 4; ++byte) {
        result.bytes[word * 4 + byte] = static_cast<std::uint8_t>(state_[word] >> ((3 - byte) * 8U));
      }
    }
    return result;
  }

  void Sha256Hasher::compress(const std::byte* block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      const auto offset = index * 4;
      words[index] = (std::to_integer<std::uint32_t>(block[offset]) << 24U) | (std::to_integer<std::uint32_t>(block[offset + 1]) << 16U)
                     | (std::to_integer<std::uint32_t>(block[offset + 2]) << 8U) | std::to_integer<std::uint32_t>(block[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      words[index] = small_sigma_one(words[index - 2]) + words[index - 7] + small_sigma_zero(words[index - 15]) + words[index - 16];
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
      const auto temporary_one = h + big_sigma_one(e) + choose(e, f, g) + round_constants[index] + words[index];
      const auto temporary_two = big_sigma_zero(a) + majority(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + temporary_one;
      d = c;
      c = b;
      b = a;
      a = temporary_one + temporary_two;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::optional<AssetId> sha256(std::span<const std::byte> data) noexcept {
    Sha256Hasher hasher;
    if (!hasher.update(data)) {
      return std::nullopt;
    }
    return hasher.finish();
  }

  std::optional<AssetId> parse_asset_id(std::string_view value) noexcept {
    if (value.size() != asset_id_prefix.size() + digest_text_size || !value.starts_with(asset_id_prefix)) {
      return std::nullopt;
    }

    AssetId result;
    const auto digest = value.substr(asset_id_prefix.size());
    for (std::size_t index = 0; index < digest.size(); ++index) {
      const auto digit = digest[index];
      if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) {
        return std::nullopt;
      }
    }
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
      result.bytes[index] = static_cast<std::uint8_t>((decode_hex(digest[index * 2]) << 4U) | decode_hex(digest[index * 2 + 1]));
    }
    return result;
  }

  std::string to_string(const AssetId& id) {
    std::string result{asset_id_prefix};
    result.reserve(asset_id_prefix.size() + digest_text_size);
    for (const auto byte : id.bytes) {
      result.push_back(hex_digits[byte >> 4U]);
      result.push_back(hex_digits[byte & 0x0fU]);
    }
    return result;
  }

  std::size_t AssetIdHash::operator()(const AssetId& id) const noexcept {
    constexpr std::size_t offset_basis = sizeof(std::size_t) == 8 ? 14695981039346656037ULL : 2166136261U;
    constexpr std::size_t prime = sizeof(std::size_t) == 8 ? 1099511628211ULL : 16777619U;
    auto result = offset_basis;
    for (const auto byte : id.bytes) {
      result ^= byte;
      result *= prime;
    }
    return result;
  }

}  // namespace mobagen::assets
