#include "plugin_activation.hpp"

#include <exception>
#include <new>
#include <utility>

namespace mobagen::plugins {
  namespace {

    void add_issue(std::vector<NativePluginActivationIssue>& issues, NativePluginActivationIssueCode code, NativePluginLifecyclePhase phase,
                   std::string message, MobagenStatus status = MOBAGEN_STATUS_OK, std::vector<NativePluginLoadIssue> load_issues = {}) {
      issues.push_back({code, phase, status, std::move(message), std::move(load_issues)});
    }

    MobagenStatus invoke_configure(const NativePluginContract& contract, const MobagenHostApiV1& host, std::span<const std::byte> configuration,
                                   std::vector<NativePluginActivationIssue>& issues) {
      try {
        return contract.lifecycle.configure(contract.plugin_state, &host,
                                            {reinterpret_cast<const std::uint8_t*>(configuration.data()), configuration.size()});
      } catch (const std::exception& exception) {
        add_issue(issues, NativePluginActivationIssueCode::CallbackException, NativePluginLifecyclePhase::Configure,
                  std::string{"plugin configure callback threw: "} + exception.what(), MOBAGEN_STATUS_FAILED);
      } catch (...) {
        add_issue(issues, NativePluginActivationIssueCode::CallbackException, NativePluginLifecyclePhase::Configure,
                  "plugin configure callback threw", MOBAGEN_STATUS_FAILED);
      }
      return MOBAGEN_STATUS_FAILED;
    }

    MobagenStatus invoke_status(MobagenPluginStartFn callback, void* state, NativePluginLifecyclePhase phase,
                                std::vector<NativePluginActivationIssue>& issues) {
      try {
        return callback(state);
      } catch (const std::exception& exception) {
        add_issue(issues, NativePluginActivationIssueCode::CallbackException, phase,
                  std::string{"plugin lifecycle callback threw: "} + exception.what(), MOBAGEN_STATUS_FAILED);
      } catch (...) {
        add_issue(issues, NativePluginActivationIssueCode::CallbackException, phase, "plugin lifecycle callback threw", MOBAGEN_STATUS_FAILED);
      }
      return MOBAGEN_STATUS_FAILED;
    }

    void invoke_stop(MobagenPluginStopFn callback, void* state, std::vector<NativePluginActivationIssue>& issues) {
      try {
        callback(state);
      } catch (const std::exception& exception) {
        add_issue(issues, NativePluginActivationIssueCode::CallbackException, NativePluginLifecyclePhase::Stop,
                  std::string{"plugin stop callback threw: "} + exception.what(), MOBAGEN_STATUS_FAILED);
      } catch (...) {
        add_issue(issues, NativePluginActivationIssueCode::CallbackException, NativePluginLifecyclePhase::Stop, "plugin stop callback threw",
                  MOBAGEN_STATUS_FAILED);
      }
    }

  }  // namespace

  NativePluginActivation::NativePluginActivation(PluginHost& host, std::string provider_id, NativePlugin plugin) noexcept
      : host_(host), provider_id_(std::move(provider_id)), plugin_(std::move(plugin)) {}

  NativePluginActivation::~NativePluginActivation() { shutdown_noexcept(); }

  void NativePluginActivation::shutdown_noexcept() noexcept {
    if (state_ == NativePluginActivationState::Stopped) {
      return;
    }
    if (!host_.owns_current_thread()) {
      plugin_.abandon();
      state_ = NativePluginActivationState::Stopped;
      return;
    }
    const auto& contract = plugin_.contract();
    if (state_ == NativePluginActivationState::Active) {
      try {
        (void)contract.lifecycle.quiesce(contract.plugin_state);
      } catch (...) {
      }
    }
    if (state_ == NativePluginActivationState::Active || state_ == NativePluginActivationState::Quiesced) {
      try {
        contract.lifecycle.stop(contract.plugin_state);
      } catch (...) {
      }
    }
    if (!host_.remove_provider(provider_id_)) {
      plugin_.abandon();
    }
    state_ = NativePluginActivationState::Stopped;
  }

  NativePluginActionResult NativePluginActivation::start() {
    NativePluginActionResult result;
    if (!host_.owns_current_thread()) {
      add_issue(result.issues, NativePluginActivationIssueCode::WrongThread, NativePluginLifecyclePhase::Start,
                "plugin lifecycle must run on the host owner thread");
      return result;
    }
    if (state_ != NativePluginActivationState::Configured) {
      add_issue(result.issues, NativePluginActivationIssueCode::InvalidTransition, NativePluginLifecyclePhase::Start,
                "only a configured plugin can be started");
      return result;
    }
    const auto& contract = plugin_.contract();
    const auto status = invoke_status(contract.lifecycle.start, contract.plugin_state, NativePluginLifecyclePhase::Start, result.issues);
    if (status != MOBAGEN_STATUS_OK) {
      if (result.issues.empty()) {
        add_issue(result.issues, NativePluginActivationIssueCode::CallbackFailed, NativePluginLifecyclePhase::Start,
                  "plugin start callback reported failure", status);
      }
      cleanup_failed_start(result);
      return result;
    }
    state_ = NativePluginActivationState::Active;
    return result;
  }

  NativePluginActionResult NativePluginActivation::quiesce() {
    NativePluginActionResult result;
    if (!host_.owns_current_thread()) {
      add_issue(result.issues, NativePluginActivationIssueCode::WrongThread, NativePluginLifecyclePhase::Quiesce,
                "plugin lifecycle must run on the host owner thread");
      return result;
    }
    if (state_ != NativePluginActivationState::Active) {
      add_issue(result.issues, NativePluginActivationIssueCode::InvalidTransition, NativePluginLifecyclePhase::Quiesce,
                "only an active plugin can be quiesced");
      return result;
    }
    const auto& contract = plugin_.contract();
    const auto status = invoke_status(contract.lifecycle.quiesce, contract.plugin_state, NativePluginLifecyclePhase::Quiesce, result.issues);
    if (status != MOBAGEN_STATUS_OK && result.issues.empty()) {
      add_issue(result.issues, NativePluginActivationIssueCode::CallbackFailed, NativePluginLifecyclePhase::Quiesce,
                "plugin quiesce callback reported failure", status);
    }
    state_ = NativePluginActivationState::Quiesced;
    return result;
  }

  NativePluginActionResult NativePluginActivation::stop() {
    NativePluginActionResult result;
    if (!host_.owns_current_thread()) {
      add_issue(result.issues, NativePluginActivationIssueCode::WrongThread, NativePluginLifecyclePhase::Stop,
                "plugin lifecycle must run on the host owner thread");
      return result;
    }
    if (state_ != NativePluginActivationState::Quiesced) {
      add_issue(result.issues, NativePluginActivationIssueCode::InvalidTransition, NativePluginLifecyclePhase::Stop,
                "only a quiesced plugin can be stopped");
      return result;
    }
    const auto& contract = plugin_.contract();
    invoke_stop(contract.lifecycle.stop, contract.plugin_state, result.issues);
    if (!host_.remove_provider(contract.provider.id)) {
      add_issue(result.issues, NativePluginActivationIssueCode::RegistrationFailed, NativePluginLifecyclePhase::Stop,
                "plugin provider services were not registered");
      plugin_.abandon();
    }
    state_ = NativePluginActivationState::Stopped;
    return result;
  }

  void NativePluginActivation::cleanup_failed_start(NativePluginActionResult& result) {
    const auto& contract = plugin_.contract();
    const auto quiesce_status = invoke_status(contract.lifecycle.quiesce, contract.plugin_state, NativePluginLifecyclePhase::Quiesce, result.issues);
    if (quiesce_status != MOBAGEN_STATUS_OK) {
      add_issue(result.issues, NativePluginActivationIssueCode::CallbackFailed, NativePluginLifecyclePhase::Quiesce,
                "plugin quiesce callback failed while rolling back start", quiesce_status);
    }
    invoke_stop(contract.lifecycle.stop, contract.plugin_state, result.issues);
    if (!host_.remove_provider(contract.provider.id)) {
      plugin_.abandon();
    }
    state_ = NativePluginActivationState::Stopped;
  }

  NativePluginActivationResult activate_loaded_native_plugin(NativePlugin plugin, PluginHost& host, std::span<const std::byte> configuration) {
    NativePluginActivationResult result;
    if (!plugin.loaded()) {
      add_issue(result.issues, NativePluginActivationIssueCode::LoadFailed, NativePluginLifecyclePhase::Load, "native plugin is not loaded",
                MOBAGEN_STATUS_INVALID_ARGUMENT);
      return result;
    }

    const auto& contract = plugin.contract();
    std::string provider_id = contract.provider.id;
    try {
      if (!host.begin_registration(contract.provider.id)) {
        add_issue(result.issues, NativePluginActivationIssueCode::RegistrationFailed, NativePluginLifecyclePhase::Register,
                  "plugin provider could not begin capability registration", MOBAGEN_STATUS_CONFLICT);
        return result;
      }
    } catch (const std::bad_alloc&) {
      add_issue(result.issues, NativePluginActivationIssueCode::OutOfMemory, NativePluginLifecyclePhase::Register,
                "plugin provider registration ran out of memory", MOBAGEN_STATUS_OUT_OF_MEMORY);
      return result;
    }

    const auto configure_status = invoke_configure(contract, host.api(), configuration, result.issues);
    if (configure_status != MOBAGEN_STATUS_OK) {
      host.rollback_registration();
      if (result.issues.empty()) {
        add_issue(result.issues, NativePluginActivationIssueCode::CallbackFailed, NativePluginLifecyclePhase::Configure,
                  "plugin configure callback reported failure", configure_status);
      }
      return result;
    }
    if (!host.staged_capabilities_match(contract.provider.provides)) {
      host.rollback_registration();
      add_issue(result.issues, NativePluginActivationIssueCode::CapabilityMismatch, NativePluginLifecyclePhase::Configure,
                "plugin published capabilities do not exactly match its descriptor", MOBAGEN_STATUS_CONFLICT);
      return result;
    }
    if (!host.commit_registration()) {
      add_issue(result.issues, NativePluginActivationIssueCode::RegistrationFailed, NativePluginLifecyclePhase::Register,
                "plugin capabilities could not be committed", MOBAGEN_STATUS_CONFLICT);
      return result;
    }

    try {
      result.activation = std::unique_ptr<NativePluginActivation>(new NativePluginActivation(host, std::move(provider_id), std::move(plugin)));
    } catch (const std::bad_alloc&) {
      (void)host.remove_provider(provider_id);
      add_issue(result.issues, NativePluginActivationIssueCode::OutOfMemory, NativePluginLifecyclePhase::Register,
                "plugin activation ran out of memory", MOBAGEN_STATUS_OUT_OF_MEMORY);
      return result;
    }
    auto started = result.activation->start();
    if (!started.ok()) {
      result.issues = std::move(started.issues);
      result.activation.reset();
    }
    return result;
  }

  NativePluginActivationResult activate_native_plugin_package(const std::filesystem::path& package, PluginHost& host,
                                                              std::span<const std::byte> configuration) {
    NativePluginActivationResult result;
    auto loaded = load_native_plugin_package(package, host.api());
    if (!loaded.plugin.has_value()) {
      add_issue(result.issues, NativePluginActivationIssueCode::LoadFailed, NativePluginLifecyclePhase::Load, "plugin package could not be loaded",
                MOBAGEN_STATUS_FAILED, std::move(loaded.issues));
      return result;
    }
    return activate_loaded_native_plugin(std::move(*loaded.plugin), host, configuration);
  }

}  // namespace mobagen::plugins
