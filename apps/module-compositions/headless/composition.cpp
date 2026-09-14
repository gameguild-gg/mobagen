#include "composition.hpp"

#include <iterator>
#include <span>
#include <utility>

#ifdef __APPLE__
#  include <TargetConditionals.h>
#endif

namespace mobagen::compositions {

  namespace {

    constexpr std::string_view headless_provider_id = "mobagen.runtime.headless";
    constexpr std::string_view tick_capability_id = "runtime.tick.v1";

    void add_issues(HeadlessCompositionResult& result, HeadlessCompositionIssueCode code, std::span<const modules::RegistryIssue> issues) {
      for (const auto& issue : issues) result.issues.push_back({code, issue.message});
    }

    void add_issues(HeadlessCompositionResult& result, HeadlessCompositionIssueCode code, std::span<const modules::ResolutionIssue> issues) {
      for (const auto& issue : issues) result.issues.push_back({code, issue.message});
    }

    void add_issues(HeadlessCompositionResult& result, HeadlessCompositionIssueCode code, std::span<const modules::ModuleLifecycleIssue> issues) {
      for (const auto& issue : issues) result.issues.push_back({code, issue.message});
    }

    void add_issues(HeadlessCompositionResult& result, HeadlessCompositionIssueCode code, std::span<const modules::ExecutionGraphIssue> issues) {
      for (const auto& issue : issues) result.issues.push_back({code, issue.message});
    }

  }  // namespace

  modules::TargetPlatform native_target_platform() noexcept {
#ifdef __EMSCRIPTEN__
    return modules::TargetPlatform::Web;
#elif defined(__ANDROID__)
    return modules::TargetPlatform::Android;
#elif defined(_WIN32)
    return modules::TargetPlatform::Windows;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return modules::TargetPlatform::IOS;
#elif defined(__APPLE__)
    return modules::TargetPlatform::MacOS;
#else
    return modules::TargetPlatform::Linux;
#endif
  }

  HeadlessComposition::HeadlessComposition(modules::CapabilityRegistry registry, modules::ModuleResolution resolution)
      : registry_(std::move(registry)), resolution_(std::move(resolution)) {}

  HeadlessComposition::~HeadlessComposition() {
    if (activation_.activation == nullptr) return;
    if (activation_.activation->state() == modules::ModuleLifecycleState::Active) (void)activation_.activation->quiesce();
    if (activation_.activation->state() == modules::ModuleLifecycleState::Quiesced) (void)activation_.activation->stop();
  }

  bool HeadlessComposition::configure(void* state, modules::ModuleContext& context) {
    auto& runtime = *static_cast<RuntimeState*>(state);
    return context.bind(runtime.capability, runtime.provider, &runtime.service);
  }

  bool HeadlessComposition::tick() noexcept {
    if (activation_.activation == nullptr || activation_.activation->state() != modules::ModuleLifecycleState::Active || !graph_.ok()
        || !tick_handle_.has_value()) {
      return false;
    }
    auto* service = graph_.graph->service(*tick_handle_);
    if (service == nullptr) return false;
    ++service->ticks;
    return true;
  }

  std::uint64_t HeadlessComposition::ticks() const noexcept { return runtime_.service.ticks; }

  modules::ModuleLifecycleResult HeadlessComposition::stop() {
    modules::ModuleLifecycleResult result;
    if (activation_.activation == nullptr) return result;
    if (activation_.activation->state() == modules::ModuleLifecycleState::Active) {
      auto quiesced = activation_.activation->quiesce();
      result.issues.insert(result.issues.end(), std::make_move_iterator(quiesced.issues.begin()), std::make_move_iterator(quiesced.issues.end()));
    }
    if (activation_.activation->state() == modules::ModuleLifecycleState::Quiesced) {
      auto stopped = activation_.activation->stop();
      result.issues.insert(result.issues.end(), std::make_move_iterator(stopped.issues.begin()), std::make_move_iterator(stopped.issues.end()));
    }
    return result;
  }

  const modules::CapabilityRegistry& HeadlessComposition::registry() const noexcept { return registry_; }

  const modules::ModuleResolution& HeadlessComposition::resolution() const noexcept { return resolution_; }

  HeadlessCompositionResult create_headless_composition(const modules::ProductDescriptor& product, modules::TargetPlatform target,
                                                        std::string_view profile) {
    HeadlessCompositionResult result;
    modules::CapabilityRegistryBuilder registry_builder;
    registry_builder.add({
        .id = std::string(headless_provider_id),
        .version = {1, 0, 0},
        .provides = {std::string(tick_capability_id)},
        .targets = {modules::TargetPlatform::Windows, modules::TargetPlatform::Linux, modules::TargetPlatform::MacOS, modules::TargetPlatform::Web,
                    modules::TargetPlatform::Android, modules::TargetPlatform::IOS},
        .linkages = {modules::LinkageMode::Static},
    });
    auto registry_result = registry_builder.build();
    if (!registry_result.ok()) {
      add_issues(result, HeadlessCompositionIssueCode::Registry, registry_result.issues);
      return result;
    }
    auto registry = std::move(*registry_result.registry);

    modules::ResolverOptions options{
        .target = target,
        .profile = std::string(profile),
        .aliases = {{.alias = "runtime", .capability = std::string(tick_capability_id)}},
        .defaults = {{.target = target,
                      .profile = std::string(profile),
                      .capability = std::string(tick_capability_id),
                      .provider = std::string(headless_provider_id)}},
    };
    auto resolution_result = modules::resolve_modules(product, registry, options);
    if (!resolution_result.ok()) {
      add_issues(result, HeadlessCompositionIssueCode::Resolution, resolution_result.issues);
      return result;
    }

    auto composition = std::unique_ptr<HeadlessComposition>(new HeadlessComposition(std::move(registry), std::move(*resolution_result.resolution)));
    const auto provider = composition->registry_.find_provider(headless_provider_id);
    const auto capability = composition->registry_.find_capability(tick_capability_id);
    if (!provider.has_value() || !capability.has_value()) {
      result.issues.push_back({HeadlessCompositionIssueCode::Registry, "headless runtime indices are unavailable"});
      return result;
    }
    composition->runtime_.provider = *provider;
    composition->runtime_.capability = *capability;
    const modules::ModuleLifecycleBinding binding{
        .provider = *provider,
        .api = {.state = &composition->runtime_, .configure = HeadlessComposition::configure},
    };
    composition->activation_ = modules::activate_modules(composition->registry_, composition->resolution_, std::span(&binding, 1));
    if (!composition->activation_.ok()) {
      add_issues(result, HeadlessCompositionIssueCode::Activation, composition->activation_.issues);
      return result;
    }
    composition->graph_ = modules::freeze_execution_graph(composition->registry_, composition->resolution_, *composition->activation_.activation);
    if (!composition->graph_.ok()) {
      add_issues(result, HeadlessCompositionIssueCode::Freeze, composition->graph_.issues);
      return result;
    }
    composition->tick_handle_ = composition->graph_.graph->handle<HeadlessComposition::TickService>(*capability);
    if (!composition->tick_handle_.has_value()) {
      result.issues.push_back({HeadlessCompositionIssueCode::Freeze, "headless tick handle is unavailable"});
      return result;
    }

    result.composition = std::move(composition);
    return result;
  }

}  // namespace mobagen::compositions
