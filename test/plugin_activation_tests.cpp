#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>

#include "plugins/plugin_activation.hpp"
#include "plugins/runtime_tick_v1.h"

namespace module_allocation_probe {
  extern std::atomic_bool enabled;
  extern std::atomic_size_t count;
}  // namespace module_allocation_probe

namespace {

  class TemporaryPluginRoot {
  public:
    TemporaryPluginRoot() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-plugin-activation-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryPluginRoot() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] std::filesystem::path package(const std::filesystem::path& binary, std::string_view name) const {
      const auto package = path_ / (std::string{name} + ".plugin");
      REQUIRE(std::filesystem::create_directory(package));
      REQUIRE(std::filesystem::copy_file(binary, package / mobagen::plugins::native_plugin_binary_filename()));
      return package;
    }

  private:
    std::filesystem::path path_;
  };

  bool has_issue(const mobagen::plugins::NativePluginActivationResult& result, mobagen::plugins::NativePluginActivationIssueCode code,
                 mobagen::plugins::NativePluginLifecyclePhase phase) {
    return std::ranges::any_of(result.issues, [code, phase](const auto& issue) { return issue.code == code && issue.phase == phase; });
  }

}  // namespace

TEST_CASE("Plugin activation: a real dot-plugin publishes starts and tears down transactionally") {
  using namespace mobagen::plugins;
  TemporaryPluginRoot root;
  const auto package = root.package(MOBAGEN_REFERENCE_PLUGIN_PATH, "reference");
  PluginHost host;

  auto result = activate_native_plugin_package(package, host);
  REQUIRE(result.ok());
  CHECK(result.issues.empty());
  CHECK(result.activation->state() == NativePluginActivationState::Active);
  CHECK(result.activation->provider_id() == "mobagen.reference");

  const auto tick_api = host.find<MobagenRuntimeTickV1>(MOBAGEN_RUNTIME_TICK_V1_ID, 1);
  REQUIRE(tick_api.has_value());
  bool ticks_succeeded = true;
  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_relaxed);
  for (std::size_t invocation = 0; invocation < 100'000; ++invocation) {
    ticks_succeeded = (*tick_api)->tick((*tick_api)->plugin_state) == MOBAGEN_STATUS_OK && ticks_succeeded;
  }
  module_allocation_probe::enabled.store(false, std::memory_order_relaxed);
  CHECK(ticks_succeeded);
  CHECK(module_allocation_probe::count.load(std::memory_order_relaxed) == 0);
  CHECK((*tick_api)->tick_count((*tick_api)->plugin_state) == 100'000);

  CHECK(result.activation->quiesce().ok());
  CHECK((*tick_api)->tick((*tick_api)->plugin_state) == MOBAGEN_STATUS_CONFLICT);
  CHECK(result.activation->stop().ok());
  CHECK(host.size() == 0);
  CHECK_FALSE(host.find<MobagenRuntimeTickV1>(MOBAGEN_RUNTIME_TICK_V1_ID, 1).has_value());
}

TEST_CASE("Plugin activation: configure and start failures remove every staged service") {
  using namespace mobagen::plugins;
  TemporaryPluginRoot root;
  PluginHost host;

  const auto configure_package = root.package(MOBAGEN_CONFIGURE_FAILURE_PLUGIN_PATH, "configure-failure");
  auto configure = activate_native_plugin_package(configure_package, host);
  CHECK_FALSE(configure.ok());
  CHECK(has_issue(configure, NativePluginActivationIssueCode::CallbackFailed, NativePluginLifecyclePhase::Configure));
  CHECK(host.size() == 0);
  std::error_code error;
  CHECK(std::filesystem::remove(configure_package / native_plugin_binary_filename(), error));
  CHECK_FALSE(error);

  const auto start_package = root.package(MOBAGEN_START_FAILURE_PLUGIN_PATH, "start-failure");
  auto start = activate_native_plugin_package(start_package, host);
  CHECK_FALSE(start.ok());
  CHECK(has_issue(start, NativePluginActivationIssueCode::CallbackFailed, NativePluginLifecyclePhase::Start));
  CHECK(host.size() == 0);
  error.clear();
  CHECK(std::filesystem::remove(start_package / native_plugin_binary_filename(), error));
  CHECK_FALSE(error);
}

TEST_CASE("Plugin activation: undeclared capability publication rolls back before start") {
  using namespace mobagen::plugins;
  TemporaryPluginRoot root;
  const auto package = root.package(MOBAGEN_CAPABILITY_MISMATCH_PLUGIN_PATH, "capability-mismatch");
  PluginHost host;

  const auto result = activate_native_plugin_package(package, host);
  CHECK_FALSE(result.ok());
  CHECK(has_issue(result, NativePluginActivationIssueCode::CapabilityMismatch, NativePluginLifecyclePhase::Configure));
  CHECK(host.size() == 0);
  std::error_code error;
  CHECK(std::filesystem::remove(package / native_plugin_binary_filename(), error));
  CHECK_FALSE(error);
}

TEST_CASE("Plugin activation: destruction quiesces stops and removes published services") {
  using namespace mobagen::plugins;
  TemporaryPluginRoot root;
  const auto package = root.package(MOBAGEN_REFERENCE_PLUGIN_PATH, "reference");
  PluginHost host;

  {
    auto result = activate_native_plugin_package(package, host);
    REQUIRE(result.ok());
    CHECK(host.size() == 1);
  }
  CHECK(host.size() == 0);
  std::error_code error;
  CHECK(std::filesystem::remove(package / native_plugin_binary_filename(), error));
  CHECK_FALSE(error);
}

TEST_CASE("Plugin activation: lifecycle actions reject non-owner threads without state changes") {
  using namespace mobagen::plugins;
  TemporaryPluginRoot root;
  const auto package = root.package(MOBAGEN_REFERENCE_PLUGIN_PATH, "reference");
  PluginHost host;
  auto result = activate_native_plugin_package(package, host);
  REQUIRE(result.ok());

  NativePluginActionResult worker_result;
  std::thread worker([&] { worker_result = result.activation->quiesce(); });
  worker.join();
  CHECK_FALSE(worker_result.ok());
  CHECK(worker_result.issues.front().code == NativePluginActivationIssueCode::WrongThread);
  CHECK(result.activation->state() == NativePluginActivationState::Active);
  CHECK(result.activation->quiesce().ok());
  CHECK(result.activation->stop().ok());
}
