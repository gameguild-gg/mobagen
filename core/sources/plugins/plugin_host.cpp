#include "plugin_host.hpp"

#include "modules/descriptor.hpp"
#include "plugin_contract.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace mobagen::plugins {
  namespace {

    [[nodiscard]] bool valid_alignment(std::size_t alignment) noexcept { return alignment >= alignof(void*) && (alignment & (alignment - 1)) == 0; }

    [[nodiscard]] std::size_t normalized_alignment(std::size_t alignment) noexcept { return alignment == 0 ? alignof(std::max_align_t) : alignment; }

    [[nodiscard]] std::optional<std::string_view> checked_view(MobagenStringView value) noexcept {
      if ((value.size != 0 && value.data == nullptr) || value.size > max_plugin_string_bytes) {
        return std::nullopt;
      }
      const auto view = std::string_view{value.data == nullptr ? "" : value.data, value.size};
      if (view.find('\0') != std::string_view::npos) {
        return std::nullopt;
      }
      return view;
    }

  }  // namespace

  PluginHost::PluginHost(PluginLogSink log_sink, void* log_context) noexcept
      : owner_thread_(std::this_thread::get_id()), log_sink_(log_sink), log_context_(log_context) {
    api_ = {
        .struct_size = MOBAGEN_PLUGIN_HOST_API_V1_SIZE,
        .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
        .host_context = this,
        .allocate = allocate,
        .deallocate = deallocate,
        .log = log,
        .publish_capability = publish_capability,
        .find_capability = find_capability,
    };
  }

  bool PluginHost::begin_registration(std::string_view provider_id) {
    if (!on_owner_thread() || active_provider_.has_value() || !modules::is_provider_id(provider_id)) {
      return false;
    }
    if (std::ranges::any_of(services_, [provider_id](const auto& entry) { return entry.second.provider_id == provider_id; })) {
      return false;
    }
    active_provider_ = std::string{provider_id};
    staged_.clear();
    return true;
  }

  bool PluginHost::staged_capabilities_match(std::span<const std::string> expected) const noexcept {
    if (!on_owner_thread() || !active_provider_.has_value() || staged_.size() != expected.size()) {
      return false;
    }
    return std::ranges::all_of(expected, [this](const std::string& capability) {
      return std::ranges::any_of(staged_, [&capability](const CapabilityBinding& binding) { return binding.capability_id == capability; });
    });
  }

  bool PluginHost::commit_registration() {
    if (!on_owner_thread() || !active_provider_.has_value()) {
      return false;
    }
    try {
      auto committed = services_;
      for (const auto& binding : staged_) {
        if (!committed.emplace(binding.capability_id, binding).second) {
          rollback_registration();
          return false;
        }
      }
      services_.swap(committed);
    } catch (...) {
      rollback_registration();
      return false;
    }
    active_provider_.reset();
    staged_.clear();
    return true;
  }

  void PluginHost::rollback_registration() noexcept {
    if (!on_owner_thread()) {
      return;
    }
    active_provider_.reset();
    staged_.clear();
  }

  bool PluginHost::remove_provider(std::string_view provider_id) {
    if (!on_owner_thread() || active_provider_.has_value()) {
      return false;
    }
    const auto old_size = services_.size();
    std::erase_if(services_, [provider_id](const auto& entry) { return entry.second.provider_id == provider_id; });
    return services_.size() != old_size;
  }

  std::optional<NativeCapabilityBindingView> PluginHost::find_binding(std::string_view capability_id,
                                                                      std::uint32_t minimum_abi_version) const noexcept {
    const auto found = services_.find(capability_id);
    if (found == services_.end() || found->second.abi_version < minimum_abi_version) {
      return std::nullopt;
    }
    const auto& binding = found->second;
    return NativeCapabilityBindingView{binding.provider_id, binding.capability_id, binding.abi_version, binding.function_table,
                                       binding.function_table_size};
  }

  bool PluginHost::on_owner_thread() const noexcept { return std::this_thread::get_id() == owner_thread_; }

  void* MOBAGEN_PLUGIN_CALL PluginHost::allocate(void*, std::size_t size, std::size_t alignment) noexcept {
    alignment = normalized_alignment(alignment);
    if (!valid_alignment(alignment)) {
      return nullptr;
    }
    return ::operator new(size == 0 ? 1 : size, static_cast<std::align_val_t>(alignment), std::nothrow);
  }

  void MOBAGEN_PLUGIN_CALL PluginHost::deallocate(void*, void* memory, std::size_t, std::size_t alignment) noexcept {
    if (memory == nullptr) {
      return;
    }
    alignment = normalized_alignment(alignment);
    if (!valid_alignment(alignment)) {
      return;
    }
    ::operator delete(memory, static_cast<std::align_val_t>(alignment));
  }

  void MOBAGEN_PLUGIN_CALL PluginHost::log(void* context, MobagenLogLevel level, MobagenStringView message) noexcept {
    const auto* host = static_cast<const PluginHost*>(context);
    const auto text = checked_view(message);
    if (host == nullptr || host->log_sink_ == nullptr || !text.has_value()) {
      return;
    }
    host->log_sink_(host->log_context_, level, *text);
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL PluginHost::publish_capability(void* context, MobagenStringView capability_id, std::uint32_t abi_version,
                                                                   const void* function_table, std::uint32_t function_table_size) noexcept {
    auto* host = static_cast<PluginHost*>(context);
    if (host == nullptr) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    try {
      return host->publish(capability_id, abi_version, function_table, function_table_size);
    } catch (const std::bad_alloc&) {
      return MOBAGEN_STATUS_OUT_OF_MEMORY;
    } catch (...) {
      return MOBAGEN_STATUS_FAILED;
    }
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL PluginHost::find_capability(void* context, MobagenStringView capability_id, std::uint32_t minimum_abi_version,
                                                                const void** function_table, std::uint32_t* function_table_size) noexcept {
    if (function_table == nullptr || function_table_size == nullptr) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    *function_table = nullptr;
    *function_table_size = 0;
    const auto* host = static_cast<const PluginHost*>(context);
    const auto id = checked_view(capability_id);
    if (host == nullptr || !id.has_value() || !modules::is_capability_id(*id)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    const auto found = host->services_.find(*id);
    if (found == host->services_.end()) {
      return MOBAGEN_STATUS_NOT_FOUND;
    }
    if (found->second.abi_version < minimum_abi_version) {
      return MOBAGEN_STATUS_UNSUPPORTED;
    }
    *function_table = found->second.function_table;
    *function_table_size = found->second.function_table_size;
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus PluginHost::publish(MobagenStringView capability_id, std::uint32_t abi_version, const void* function_table,
                                    std::uint32_t function_table_size) {
    if (!on_owner_thread() || !active_provider_.has_value()) {
      return MOBAGEN_STATUS_CONFLICT;
    }
    const auto id = checked_view(capability_id);
    if (!id.has_value() || !modules::is_capability_id(*id) || abi_version == 0 || function_table == nullptr
        || function_table_size < sizeof(MobagenCapabilityHeaderV1)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }

    MobagenCapabilityHeaderV1 header{};
    std::memcpy(&header, function_table, sizeof(header));
    if (header.abi_version != abi_version || header.struct_size < sizeof(header) || header.struct_size > function_table_size) {
      return MOBAGEN_STATUS_UNSUPPORTED;
    }
    if (services_.contains(*id) || std::ranges::any_of(staged_, [id](const CapabilityBinding& binding) { return binding.capability_id == *id; })) {
      return MOBAGEN_STATUS_CONFLICT;
    }
    if (staged_.size() == max_plugin_capabilities) {
      return MOBAGEN_STATUS_OUT_OF_MEMORY;
    }
    staged_.push_back({*active_provider_, std::string{*id}, abi_version, function_table, header.struct_size});
    return MOBAGEN_STATUS_OK;
  }

}  // namespace mobagen::plugins
