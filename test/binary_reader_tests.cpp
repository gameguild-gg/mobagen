#include <doctest/doctest.h>
#include "net/transport.hpp"
#include "serialization/binary_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

TEST_CASE("BinaryReader: exact trivially-copyable reads advance the offset") {
  constexpr std::uint32_t expected = 0x12345678u;
  std::array<std::byte, sizeof(expected)> bytes{};
  std::memcpy(bytes.data(), &expected, sizeof(expected));

  serialization::BinaryReader reader(std::span<const std::byte>{bytes});
  std::uint32_t actual = 0;

  CHECK(reader.read(actual));
  CHECK(actual == expected);
  CHECK(reader.offset() == bytes.size());
  CHECK(reader.remaining() == 0);
}

TEST_CASE("BinaryReader: empty and truncated inputs fail without mutation") {
  serialization::BinaryReader empty(std::span<const std::byte>{});
  std::uint32_t empty_value = 41u;
  CHECK_FALSE(empty.read(empty_value));
  CHECK(empty_value == 41u);
  CHECK(empty.offset() == 0);

  const std::array<std::byte, 3> bytes{};
  serialization::BinaryReader truncated(std::span<const std::byte>{bytes});
  std::uint32_t truncated_value = 42u;
  CHECK_FALSE(truncated.read(truncated_value));
  CHECK(truncated_value == 42u);
  CHECK(truncated.offset() == 0);
  CHECK(truncated.remaining() == bytes.size());
}

TEST_CASE("BinaryReader: an initial offset beyond the input remains invalid") {
  const std::array<std::byte, 1> bytes{};
  serialization::BinaryReader reader(std::span<const std::byte>{bytes}, 2);
  std::uint8_t value = 7;

  CHECK_FALSE(reader.read(value));
  CHECK(value == 7);
  CHECK(reader.offset() == 2);
  CHECK(reader.remaining() == 0);
}

TEST_CASE("Transport: get rejects truncated values without consuming bytes") {
  const net::Blob blob(sizeof(std::uint32_t) - 1);
  std::size_t offset = 0;
  std::uint32_t value = 99u;

  CHECK_FALSE(net::get(blob, offset, value));
  CHECK(offset == 0);
  CHECK(value == 99u);
}

TEST_CASE("Transport: put and get round-trip a POD value") {
  constexpr std::uint64_t expected = 0x0123456789ABCDEFull;
  net::Blob blob;
  net::put(blob, expected);
  std::size_t offset = 0;
  std::uint64_t actual = 0;

  CHECK(net::get(blob, offset, actual));
  CHECK(actual == expected);
  CHECK(offset == sizeof(expected));
}
