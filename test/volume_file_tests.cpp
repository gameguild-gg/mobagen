#include <doctest/doctest.h>
#include "volume_file.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {
  class TemporaryVolumeFile {
  public:
    TemporaryVolumeFile() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-volume-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)) + ".mvol");
    }

    ~TemporaryVolumeFile() {
      std::error_code error;
      std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
  };

  volume::VolumeFileHeader valid_header() {
    volume::VolumeFileHeader header{};
    std::memcpy(header.magic, "MVL1", 4);
    header.width = 2;
    header.height = 2;
    header.depth = 1;
    header.storage_format = 0;
    header.bytes_per_voxel = 1;
    header.spacing_x = 0.5f;
    header.spacing_y = 0.5f;
    header.spacing_z = 1.0f;
    header.rescale_slope = 1.0f;
    header.rescale_intercept = -1024.0f;
    header.window_center = 40.0f;
    header.window_width = 400.0f;
    header.value_min = 0.0f;
    header.value_max = 4095.0f;
    return header;
  }

  void write_file(const std::filesystem::path& path, const volume::VolumeFileHeader& header, const std::vector<std::uint8_t>& payload,
                  const std::vector<std::uint8_t>& trailing = {}) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    stream.write(reinterpret_cast<const char*>(trailing.data()), static_cast<std::streamsize>(trailing.size()));
    REQUIRE(stream.good());
  }

  volume::VolumeBuffer load(const std::filesystem::path& path, bool& ok) { return volume::load_volume_file(path.string().c_str(), ok); }

  void check_invalid_header(const volume::VolumeFileHeader& header) {
    TemporaryVolumeFile file;
    write_file(file.path(), header, {1, 2, 3, 4});
    bool ok = true;
    CHECK(load(file.path(), ok).empty());
    CHECK_FALSE(ok);
  }
}  // namespace

TEST_CASE("Volume layout: validates formats and checked byte counts") {
  volume::VolumeMetadata metadata;
  metadata.width = 2;
  metadata.height = 3;
  metadata.depth = 4;
  volume::VolumeLayout layout{};

  CHECK(volume::try_volume_layout(metadata, volume::VolumeStorageFormat::R8, 1, layout));
  CHECK(layout.voxel_count == 24);
  CHECK(layout.byte_count == 24);

  CHECK(volume::try_volume_layout(metadata, volume::VolumeStorageFormat::U16PackedRG8, 2, layout));
  CHECK(layout.voxel_count == 24);
  CHECK(layout.byte_count == 48);
  CHECK_FALSE(volume::try_volume_layout(metadata, volume::VolumeStorageFormat::R8, 2, layout));
  CHECK_FALSE(volume::try_volume_layout(metadata, volume::VolumeStorageFormat::U16PackedRG8, 1, layout));
  CHECK_FALSE(volume::try_volume_layout(metadata, static_cast<volume::VolumeStorageFormat>(0xFF), 1, layout));
}

TEST_CASE("Volume layout: rejects excessive dimensions and multiplication overflow") {
  volume::VolumeMetadata metadata;
  metadata.width = volume::kMaxVolumeDimension + 1u;
  metadata.height = 1;
  metadata.depth = 1;
  volume::VolumeLayout layout{};
  CHECK_FALSE(volume::try_volume_layout(metadata, volume::VolumeStorageFormat::R8, 1, layout));

  const volume::VolumeBuffer invalid_buffer(metadata, volume::VolumeStorageFormat::R8, 1, std::pmr::get_default_resource());
  CHECK(invalid_buffer.empty());
  CHECK_FALSE(invalid_buffer.metadata().valid());

  metadata.width = std::numeric_limits<std::uint32_t>::max();
  metadata.height = std::numeric_limits<std::uint32_t>::max();
  metadata.depth = std::numeric_limits<std::uint32_t>::max();
  CHECK_FALSE(volume::try_volume_layout(metadata, volume::VolumeStorageFormat::U16PackedRG8, 2, layout));

  metadata.width = 1024;
  metadata.height = 1024;
  metadata.depth = 1024;
  CHECK_FALSE(volume::try_volume_layout(metadata, volume::VolumeStorageFormat::R8, 1, layout));
}

TEST_CASE("Volume file: loads exact R8 and packed UInt16 payloads") {
  TemporaryVolumeFile r8_file;
  const volume::VolumeFileHeader r8_header = valid_header();
  write_file(r8_file.path(), r8_header, {1, 2, 3, 4});
  bool r8_ok = false;
  const volume::VolumeBuffer r8 = load(r8_file.path(), r8_ok);
  REQUIRE(r8_ok);
  CHECK(r8.storage_format() == volume::VolumeStorageFormat::R8);
  CHECK(r8.bytes_per_voxel() == 1);
  CHECK(r8.size_bytes() == 4);
  CHECK(r8.data()[3] == 4);

  TemporaryVolumeFile u16_file;
  volume::VolumeFileHeader u16_header = valid_header();
  u16_header.storage_format = 1;
  u16_header.bytes_per_voxel = 2;
  write_file(u16_file.path(), u16_header, {1, 0, 2, 0, 3, 0, 4, 0});
  bool u16_ok = false;
  const volume::VolumeBuffer u16 = load(u16_file.path(), u16_ok);
  REQUIRE(u16_ok);
  CHECK(u16.storage_format() == volume::VolumeStorageFormat::U16PackedRG8);
  CHECK(u16.bytes_per_voxel() == 2);
  CHECK(u16.size_bytes() == 8);
}

TEST_CASE("Volume file: rejects truncated and trailing payloads") {
  const volume::VolumeFileHeader header = valid_header();

  TemporaryVolumeFile truncated_header;
  {
    std::ofstream stream(truncated_header.path(), std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header) - 1);
    REQUIRE(stream.good());
  }
  bool truncated_header_ok = true;
  CHECK(load(truncated_header.path(), truncated_header_ok).empty());
  CHECK_FALSE(truncated_header_ok);

  TemporaryVolumeFile truncated;
  write_file(truncated.path(), header, {1, 2, 3});
  bool truncated_ok = true;
  CHECK(load(truncated.path(), truncated_ok).empty());
  CHECK_FALSE(truncated_ok);

  TemporaryVolumeFile trailing;
  write_file(trailing.path(), header, {1, 2, 3, 4}, {5});
  bool trailing_ok = true;
  CHECK(load(trailing.path(), trailing_ok).empty());
  CHECK_FALSE(trailing_ok);
}

TEST_CASE("Volume file: rejects unknown and mismatched storage formats") {
  volume::VolumeFileHeader bad_magic = valid_header();
  bad_magic.magic[0] = 'X';
  check_invalid_header(bad_magic);

  for (const auto [format, bytes_per_voxel] : {std::pair{2u, 1u}, std::pair{0u, 2u}, std::pair{1u, 1u}}) {
    TemporaryVolumeFile file;
    volume::VolumeFileHeader header = valid_header();
    header.storage_format = format;
    header.bytes_per_voxel = bytes_per_voxel;
    write_file(file.path(), header, {1, 2, 3, 4});
    bool ok = true;
    CHECK(load(file.path(), ok).empty());
    CHECK_FALSE(ok);
  }
}

TEST_CASE("Volume file: rejects invalid physical metadata before allocation") {
  for (const float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    volume::VolumeFileHeader header = valid_header();
    header.spacing_x = invalid;
    check_invalid_header(header);
  }

  volume::VolumeFileHeader invalid_spacing_y = valid_header();
  invalid_spacing_y.spacing_y = std::numeric_limits<float>::quiet_NaN();
  check_invalid_header(invalid_spacing_y);

  volume::VolumeFileHeader invalid_spacing_z = valid_header();
  invalid_spacing_z.spacing_z = -1.0f;
  check_invalid_header(invalid_spacing_z);

  volume::VolumeFileHeader invalid_slope = valid_header();
  invalid_slope.rescale_slope = 0.0f;
  check_invalid_header(invalid_slope);

  volume::VolumeFileHeader invalid_intercept = valid_header();
  invalid_intercept.rescale_intercept = std::numeric_limits<float>::infinity();
  check_invalid_header(invalid_intercept);

  volume::VolumeFileHeader invalid_window = valid_header();
  invalid_window.window_width = 0.0f;
  check_invalid_header(invalid_window);

  volume::VolumeFileHeader invalid_range = valid_header();
  invalid_range.value_min = 10.0f;
  invalid_range.value_max = 9.0f;
  check_invalid_header(invalid_range);

  TemporaryVolumeFile excessive;
  volume::VolumeFileHeader excessive_header = valid_header();
  excessive_header.width = volume::kMaxVolumeDimension + 1u;
  write_file(excessive.path(), excessive_header, {});
  bool excessive_ok = true;
  CHECK_NOTHROW(load(excessive.path(), excessive_ok));
  CHECK_FALSE(excessive_ok);
}
