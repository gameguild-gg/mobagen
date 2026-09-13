#include <doctest/doctest.h>
#include "render/scene_serialize.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {
  constexpr std::size_t kCountOffset = 8;
  constexpr std::size_t kPositionXOffset = 12;
  constexpr std::size_t kRotationWOffset = 36;
  constexpr std::size_t kScaleXOffset = 40;
  constexpr std::size_t kParentOffset = 52;
  constexpr std::size_t kHasVolumeOffset = 56;
  constexpr std::size_t kVolumeWidthOffset = 65;
  constexpr std::size_t kVolumeHeightOffset = 69;
  constexpr std::size_t kVolumeDepthOffset = 73;
  constexpr std::size_t kVolumeSpacingXOffset = 77;
  constexpr std::size_t kVolumeSpacingYOffset = 81;
  constexpr std::size_t kVolumeSpacingZOffset = 85;
  constexpr std::size_t kVolumeFormatOffset = 89;
  constexpr std::size_t kWindowCenterOffset = 90;
  constexpr std::size_t kWindowWidthOffset = 94;
  constexpr std::size_t kTransferPresetOffset = 98;
  constexpr std::size_t kRenderModeOffset = 102;
  constexpr std::size_t kIsoThresholdOffset = 103;

  template <class T> void append(std::vector<std::uint8_t>& bytes, const T& value) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(T));
  }

  template <class T>
  void overwrite(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value) {
    REQUIRE(offset + sizeof(T) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
  }

  std::vector<std::uint8_t> make_valid_volume_scene() {
    ecs::World world;
    const ecs::Entity entity = world.create();
    scene::Transform transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    world.add<scene::Transform>(entity, transform);

    render::VolumeRenderable volume;
    volume.source.handle = resource::Handle{5u, 8u};
    volume.source.width = 16;
    volume.source.height = 32;
    volume.source.depth = 64;
    volume.source.spacing_mm = {0.5f, 0.5f, 1.0f};
    volume.source.format = render::VolumeScalarFormat::UInt16;
    volume.display.window_center = 40.0f;
    volume.display.window_width = 400.0f;
    volume.display.transfer_preset = 2;
    volume.display.mode = render::VolumeRenderMode::DVR;
    volume.display.iso_threshold = 0.5f;
    world.add<render::VolumeRenderable>(entity, volume);
    return render::save_scene(world);
  }

  void check_rejected_without_mutation(const std::uint8_t* data, std::size_t size) {
    ecs::World world;
    const ecs::Entity sentinel = world.create();
    world.add<scene::Transform>(sentinel).position = {9.0f, 8.0f, 7.0f};
    const std::size_t alive_before = world.alive();
    const std::size_t transforms_before = world.count<scene::Transform>();

    const std::vector<ecs::Entity> loaded = render::load_scene(world, data, size);

    CHECK(loaded.empty());
    CHECK(world.alive() == alive_before);
    CHECK(world.count<scene::Transform>() == transforms_before);
    CHECK(world.count<render::VolumeRenderable>() == 0);
    REQUIRE(world.valid(sentinel));
    CHECK(world.get<scene::Transform>(sentinel).position.x == 9.0f);
  }
}  // namespace

TEST_CASE("Scene loading: rejects null, truncated, and trailing input transactionally") {
  check_rejected_without_mutation(nullptr, 0);
  check_rejected_without_mutation(nullptr, 12);

  const std::vector<std::uint8_t> valid = make_valid_volume_scene();
  for (std::size_t size = 0; size < 12; ++size) {
    check_rejected_without_mutation(valid.data(), size);
  }
  check_rejected_without_mutation(valid.data(), valid.size() - 1);

  std::vector<std::uint8_t> trailing = valid;
  trailing.push_back(std::uint8_t{0xA5});
  check_rejected_without_mutation(trailing.data(), trailing.size());
}

TEST_CASE("Scene loading: rejects impossible and excessive node counts before allocation") {
  std::vector<std::uint8_t> excessive;
  append(excessive, render::kSceneMagic);
  append(excessive, render::kSceneVersion);
  append(excessive, render::kMaxSceneNodes + 1u);
  CHECK_NOTHROW(check_rejected_without_mutation(excessive.data(), excessive.size()));

  std::vector<std::uint8_t> overflowing;
  append(overflowing, render::kSceneMagic);
  append(overflowing, render::kSceneVersion);
  append(overflowing, std::numeric_limits<std::uint32_t>::max());
  CHECK_NOTHROW(check_rejected_without_mutation(overflowing.data(), overflowing.size()));
}

TEST_CASE("Scene loading: rejects invalid node metadata transactionally") {
  const std::vector<std::uint8_t> valid = make_valid_volume_scene();

  std::vector<std::uint8_t> invalid_parent = valid;
  overwrite(invalid_parent, kParentOffset, std::int32_t{1});
  check_rejected_without_mutation(invalid_parent.data(), invalid_parent.size());

  std::vector<std::uint8_t> negative_parent = valid;
  overwrite(negative_parent, kParentOffset, std::int32_t{-2});
  check_rejected_without_mutation(negative_parent.data(), negative_parent.size());

  std::vector<std::uint8_t> invalid_presence = valid;
  overwrite(invalid_presence, kHasVolumeOffset, std::uint8_t{2});
  check_rejected_without_mutation(invalid_presence.data(), invalid_presence.size());

  for (const std::size_t offset : {kPositionXOffset, kRotationWOffset, kScaleXOffset}) {
    std::vector<std::uint8_t> non_finite_transform = valid;
    overwrite(non_finite_transform, offset, std::numeric_limits<float>::quiet_NaN());
    check_rejected_without_mutation(non_finite_transform.data(), non_finite_transform.size());
  }

  std::vector<std::uint8_t> invalid_second_node = valid;
  overwrite(invalid_second_node, kCountOffset, std::uint32_t{2});
  invalid_second_node.insert(invalid_second_node.end(), valid.begin() + 12, valid.end());
  overwrite(invalid_second_node, valid.size(), std::numeric_limits<float>::quiet_NaN());
  check_rejected_without_mutation(invalid_second_node.data(), invalid_second_node.size());
}

TEST_CASE("Scene loading: rejects invalid volume metadata transactionally") {
  const std::vector<std::uint8_t> valid = make_valid_volume_scene();

  for (const std::size_t offset :
       {kVolumeWidthOffset, kVolumeHeightOffset, kVolumeDepthOffset}) {
    std::vector<std::uint8_t> invalid_dimension = valid;
    overwrite(invalid_dimension, offset, std::uint32_t{0});
    check_rejected_without_mutation(invalid_dimension.data(), invalid_dimension.size());

    std::vector<std::uint8_t> excessive_dimension = valid;
    overwrite(excessive_dimension, offset, render::kMaxVolumeDimension + 1u);
    check_rejected_without_mutation(excessive_dimension.data(), excessive_dimension.size());
  }

  for (const std::size_t offset :
       {kVolumeSpacingXOffset, kVolumeSpacingYOffset, kVolumeSpacingZOffset,
        kWindowWidthOffset}) {
    std::vector<std::uint8_t> invalid_positive_value = valid;
    overwrite(invalid_positive_value, offset, 0.0f);
    check_rejected_without_mutation(invalid_positive_value.data(), invalid_positive_value.size());
  }

  for (const std::size_t offset : {kWindowCenterOffset, kIsoThresholdOffset}) {
    std::vector<std::uint8_t> non_finite_value = valid;
    overwrite(non_finite_value, offset, std::numeric_limits<float>::infinity());
    check_rejected_without_mutation(non_finite_value.data(), non_finite_value.size());
  }

  std::vector<std::uint8_t> invalid_format = valid;
  overwrite(invalid_format, kVolumeFormatOffset, std::uint8_t{0xFF});
  check_rejected_without_mutation(invalid_format.data(), invalid_format.size());

  std::vector<std::uint8_t> invalid_mode = valid;
  overwrite(invalid_mode, kRenderModeOffset, std::uint8_t{0xFF});
  check_rejected_without_mutation(invalid_mode.data(), invalid_mode.size());

  std::vector<std::uint8_t> invalid_preset = valid;
  overwrite(invalid_preset, kTransferPresetOffset, std::uint32_t{0});
  check_rejected_without_mutation(invalid_preset.data(), invalid_preset.size());

  overwrite(invalid_preset, kTransferPresetOffset, std::uint32_t{5});
  check_rejected_without_mutation(invalid_preset.data(), invalid_preset.size());
}
