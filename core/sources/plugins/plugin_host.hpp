#pragma once

#include "plugin_abi.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mobagen::plugins {

  using PluginLogSink = void (*)(void* context, MobagenLogLevel level, std::string_view message) noexcept;

  struct NativeCapabilityBindingView {
    std::string_view provider_id;
    std::string_view capability_id;
    std::uint32_t abi_version{0};
    const void* function_table{nullptr};
    std::uint32_t function_table_size{0};
  };

  class PluginHost {
  public:
    explicit PluginHost(PluginLogSink log_sink = nullptr, void* log_context = nullptr) noexcept;
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    PluginHost(PluginHost&&) = delete;
    PluginHost& operator=(PluginHost&&) = delete;

    [[nodiscard]] const MobagenHostApiV1& api() const noexcept { return api_; }
    [[nodiscard]] bool owns_current_thread() const noexcept { return on_owner_thread(); }
    [[nodiscard]] bool begin_registration(std::string_view provider_id);
    [[nodiscard]] bool staged_capabilities_match(std::span<const std::string> expected) const noexcept;
    [[nodiscard]] bool commit_registration();
    void rollback_registration() noexcept;
    [[nodiscard]] bool remove_provider(std::string_view provider_id);

    [[nodiscard]] std::optional<NativeCapabilityBindingView> find_binding(std::string_view capability_id,
                                                                          std::uint32_t minimum_abi_version) const noexcept;

    template <class FunctionTable>
    [[nodiscard]] std::optional<const FunctionTable*> find(std::string_view capability_id, std::uint32_t minimum_abi_version) const noexcept {
      const auto binding = find_binding(capability_id, minimum_abi_version);
      if (!binding.has_value() || binding->function_table_size < sizeof(FunctionTable)
          || reinterpret_cast<std::uintptr_t>(binding->function_table) % alignof(FunctionTable) != 0) {
        return std::nullopt;
      }
      return static_cast<const FunctionTable*>(binding->function_table);
    }

    [[nodiscard]] std::size_t size() const noexcept { return services_.size(); }

  private:
    struct CapabilityBinding {
      std::string provider_id;
      std::string capability_id;
      std::uint32_t abi_version{0};
      const void* function_table{nullptr};
      std::uint32_t function_table_size{0};
    };

    static void* MOBAGEN_PLUGIN_CALL allocate(void* context, std::size_t size, std::size_t alignment) noexcept;
    static void MOBAGEN_PLUGIN_CALL deallocate(void* context, void* memory, std::size_t size, std::size_t alignment) noexcept;
    static void MOBAGEN_PLUGIN_CALL log(void* context, MobagenLogLevel level, MobagenStringView message) noexcept;
    static MobagenStatus MOBAGEN_PLUGIN_CALL publish_capability(void* context, MobagenStringView capability_id, std::uint32_t abi_version,
                                                                const void* function_table, std::uint32_t function_table_size) noexcept;
    static MobagenStatus MOBAGEN_PLUGIN_CALL find_capability(void* context, MobagenStringView capability_id, std::uint32_t minimum_abi_version,
                                                             const void** function_table, std::uint32_t* function_table_size) noexcept;

    [[nodiscard]] bool on_owner_thread() const noexcept;
    [[nodiscard]] MobagenStatus publish(MobagenStringView capability_id, std::uint32_t abi_version, const void* function_table,
                                        std::uint32_t function_table_size);

    MobagenHostApiV1 api_{};
    std::thread::id owner_thread_;
    PluginLogSink log_sink_{nullptr};
    void* log_context_{nullptr};
    std::optional<std::string> active_provider_;
    std::vector<CapabilityBinding> staged_;
    std::map<std::string, CapabilityBinding, std::less<>> services_;
  };

}  // namespace mobagen::plugins
