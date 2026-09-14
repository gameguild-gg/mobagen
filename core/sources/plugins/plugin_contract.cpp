#include "plugin_contract.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

namespace mobagen::plugins {
  namespace {

    void add_issue(PluginContractResult& result, PluginContractIssueCode code, std::string field, std::string message) {
      result.issues.push_back({code, std::move(field), std::move(message)});
    }

    [[nodiscard]] bool valid_view(MobagenStringView view, std::string_view field, PluginContractResult& result) {
      if (view.size > max_plugin_string_bytes) {
        add_issue(result, PluginContractIssueCode::limit_exceeded, std::string{field}, "plugin string exceeds the 512-byte limit");
        return false;
      }
      if (view.size != 0 && view.data == nullptr) {
        add_issue(result, PluginContractIssueCode::invalid_string, std::string{field}, "non-empty plugin string has a null pointer");
        return false;
      }
      if (view.size != 0 && std::string_view{view.data, view.size}.find('\0') != std::string_view::npos) {
        add_issue(result, PluginContractIssueCode::invalid_string, std::string{field}, "plugin string contains an embedded null byte");
        return false;
      }
      return true;
    }

    [[nodiscard]] bool valid_array(const MobagenStringView* values, std::uint32_t count, std::string_view field, PluginContractResult& result) {
      if (count > max_plugin_capabilities) {
        add_issue(result, PluginContractIssueCode::limit_exceeded, std::string{field}, "plugin capability count exceeds 1024");
        return false;
      }
      if (count != 0 && values == nullptr) {
        add_issue(result, PluginContractIssueCode::missing_array, std::string{field}, "non-empty plugin array has a null pointer");
        return false;
      }
      bool valid = true;
      for (std::uint32_t index = 0; index < count; ++index) {
        valid = valid_view(values[index], std::string{field} + '[' + std::to_string(index) + ']', result) && valid;
      }
      return valid;
    }

    [[nodiscard]] std::vector<std::string> copy_array(const MobagenStringView* values, std::uint32_t count) {
      std::vector<std::string> result;
      result.reserve(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        result.emplace_back(values[index].data == nullptr ? "" : std::string_view{values[index].data, values[index].size});
      }
      return result;
    }

    [[nodiscard]] modules::TargetPlatform current_target() {
#if defined(__EMSCRIPTEN__)
      return modules::TargetPlatform::Web;
#elif defined(_WIN32)
      return modules::TargetPlatform::Windows;
#elif defined(__ANDROID__)
      return modules::TargetPlatform::Android;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
      return modules::TargetPlatform::IOS;
#elif defined(__APPLE__)
      return modules::TargetPlatform::MacOS;
#else
      return modules::TargetPlatform::Linux;
#endif
    }

    [[nodiscard]] std::optional<modules::ReloadPolicy> reload_policy(MobagenReloadPolicy value) {
      switch (value) {
        case MOBAGEN_RELOAD_NEVER:
          return modules::ReloadPolicy::Never;
        case MOBAGEN_RELOAD_RESTART:
          return modules::ReloadPolicy::Restart;
        case MOBAGEN_RELOAD_SAFE_POINT:
          return modules::ReloadPolicy::SafePoint;
        default:
          return std::nullopt;
      }
    }

  }  // namespace

  PluginContractResult validate_native_plugin(const MobagenPluginDescriptorV1& descriptor) {
    PluginContractResult result;
    if (descriptor.abi_version != MOBAGEN_PLUGIN_ABI_VERSION) {
      add_issue(result, PluginContractIssueCode::unsupported_abi, "abi_version", "plugin ABI version is not supported");
    }
    if (descriptor.struct_size < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE) {
      add_issue(result, PluginContractIssueCode::truncated_descriptor, "struct_size", "plugin descriptor is smaller than ABI v1");
    }
    if (descriptor.lifecycle.struct_size < MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE) {
      add_issue(result, PluginContractIssueCode::truncated_lifecycle, "lifecycle.struct_size", "plugin lifecycle is smaller than ABI v1");
    }
    const auto id_valid = valid_view(descriptor.id, "id", result);
    const auto provides_valid = valid_array(descriptor.provides, descriptor.provides_count, "provides", result);
    const auto required_valid = valid_array(descriptor.required, descriptor.required_count, "required", result);
    const auto optional_valid = valid_array(descriptor.optional, descriptor.optional_count, "optional", result);
    const auto conflicts_valid = valid_array(descriptor.conflicts, descriptor.conflicts_count, "conflicts", result);

    if (descriptor.lifecycle.configure == nullptr || descriptor.lifecycle.start == nullptr || descriptor.lifecycle.quiesce == nullptr
        || descriptor.lifecycle.stop == nullptr || descriptor.lifecycle.destroy == nullptr) {
      add_issue(result, PluginContractIssueCode::invalid_lifecycle, "lifecycle", "all ABI v1 lifecycle callbacks are required");
    }
    const auto reload = reload_policy(descriptor.reload_policy);
    if (!reload.has_value()) {
      add_issue(result, PluginContractIssueCode::invalid_reload_policy, "reload_policy", "plugin reload policy is not supported");
    }

    if (!result.issues.empty() || !id_valid || !provides_valid || !required_valid || !optional_valid || !conflicts_valid || !reload.has_value()) {
      return result;
    }

    modules::ProviderDescriptor provider{
        .id = std::string{descriptor.id.data == nullptr ? "" : std::string_view{descriptor.id.data, descriptor.id.size}},
        .version = {descriptor.version_major, descriptor.version_minor, descriptor.version_patch},
        .provides = copy_array(descriptor.provides, descriptor.provides_count),
        .required = copy_array(descriptor.required, descriptor.required_count),
        .optional = copy_array(descriptor.optional, descriptor.optional_count),
        .conflicts = copy_array(descriptor.conflicts, descriptor.conflicts_count),
        .targets = {current_target()},
        .linkages = {modules::LinkageMode::Dynamic},
        .reload = *reload,
    };
    for (const auto& issue : modules::validate(provider)) {
      add_issue(result, PluginContractIssueCode::invalid_provider_descriptor, issue.field, issue.message);
    }
    if (!result.issues.empty()) {
      return result;
    }

    result.contract = NativePluginContract{std::move(provider), descriptor.plugin_state, descriptor.lifecycle};
    return result;
  }

}  // namespace mobagen::plugins
