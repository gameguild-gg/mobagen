#include "composition.hpp"

#include <array>
#include <iterator>
#include <span>
#include <utility>

#include <SDL3/SDL_log.h>

#ifdef __APPLE__
#  include <TargetConditionals.h>
#endif

namespace mobagen::compositions {

  namespace {

    constexpr std::string_view platform_provider_id = "mobagen.platform.sdl3";
    constexpr std::string_view target_provider_id = "mobagen.target.sdl3";
    constexpr std::string_view render_provider_id = "mobagen.render.webgpu";
    constexpr std::string_view runtime_provider_id = "mobagen.runtime.graphics";

    constexpr std::string_view platform_capability_id = "platform.events.v1";
    constexpr std::string_view target_capability_id = "render.target.v1";
    constexpr std::string_view render_capability_id = "render.backend.v1";
    constexpr std::string_view runtime_capability_id = "runtime.tick.v1";

    struct PlatformService {
      app::App* host{};
    };

    struct TargetService {
      app::App* host{};
      SDL_Window* window{};
      int width{};
      int height{};
      bool offscreen{};
    };

    struct RenderService {
      app::App* host{};
      WGPUDevice device{};
      WGPUQueue queue{};
      WGPUSurface surface{};
      bool resources_alive_during_shutdown{};
    };

    struct RuntimeService {
      std::uint64_t ticks{};
      std::uint64_t draws{};
      float elapsed_seconds{};
    };

    template <typename Service> struct ProviderState {
      std::string_view id;
      modules::ProviderIndex provider;
      modules::CapabilityIndex capability;
      modules::CapabilityIndex dependency;
      void* expected_dependency_service{};
      Service service;
      std::vector<std::string>* trace{};
    };

    template <typename Service> void record(ProviderState<Service>& state, std::string_view phase) {
      state.trace->emplace_back(std::string(phase) + ":" + std::string(state.id));
    }

    template <typename Service> bool configure_provider(void* raw_state, modules::ModuleContext& context) {
      auto& state = *static_cast<ProviderState<Service>*>(raw_state);
      record(state, "configure");
      return context.bind(state.capability, state.provider, &state.service);
    }

    template <typename Service> bool dependency_is_bound(const ProviderState<Service>& state, const modules::ModuleContext& context) {
      const auto* binding = context.binding(state.dependency);
      return binding != nullptr && binding->service == state.expected_dependency_service;
    }

    bool start_platform(void* raw_state, modules::ModuleContext&) {
      auto& state = *static_cast<ProviderState<PlatformService>*>(raw_state);
      record(state, "start");
      return state.service.host != nullptr && state.service.host->settings.render_mode != app::AppSettings::RenderMode::HeadlessNone;
    }

    bool start_target(void* raw_state, modules::ModuleContext& context) {
      auto& state = *static_cast<ProviderState<TargetService>*>(raw_state);
      record(state, "start");
      if (!dependency_is_bound(state, context) || state.service.host == nullptr || state.service.width <= 0 || state.service.height <= 0)
        return false;
      if (state.service.host->settings.render_mode == app::AppSettings::RenderMode::Windowed) {
        return state.service.window != nullptr && state.service.host->gpu.surface() != nullptr && !state.service.offscreen;
      }
      return state.service.host->settings.render_mode == app::AppSettings::RenderMode::HeadlessNull && state.service.window == nullptr
             && state.service.offscreen;
    }

    bool start_render(void* raw_state, modules::ModuleContext& context) {
      auto& state = *static_cast<ProviderState<RenderService>*>(raw_state);
      record(state, "start");
      return dependency_is_bound(state, context) && state.service.host != nullptr && state.service.host->gpu.operational()
             && state.service.device != nullptr && state.service.queue != nullptr && state.service.device == state.service.host->gpu.device()
             && state.service.queue == state.service.host->gpu.queue() && state.service.surface == state.service.host->gpu.surface();
    }

    bool start_runtime(void* raw_state, modules::ModuleContext& context) {
      auto& state = *static_cast<ProviderState<RuntimeService>*>(raw_state);
      record(state, "start");
      return dependency_is_bound(state, context);
    }

    template <typename Service> void quiesce_provider(void* raw_state, modules::ModuleContext&) {
      record(*static_cast<ProviderState<Service>*>(raw_state), "quiesce");
    }

    template <typename Service> void stop_provider(void* raw_state, modules::ModuleContext&) {
      record(*static_cast<ProviderState<Service>*>(raw_state), "stop");
    }

    void stop_render(void* raw_state, modules::ModuleContext&) {
      auto& state = *static_cast<ProviderState<RenderService>*>(raw_state);
      state.service.resources_alive_during_shutdown = state.service.host != nullptr && state.service.host->gpu.operational()
                                                      && state.service.host->gpu.device() == state.service.device
                                                      && state.service.host->gpu.queue() == state.service.queue;
      record(state, "stop");
    }

    template <typename Service> void rollback_provider(void* raw_state, modules::ModuleContext&) {
      record(*static_cast<ProviderState<Service>*>(raw_state), "rollback");
    }

    template <typename Service> modules::ModuleLifecycleBinding lifecycle_binding(ProviderState<Service>& state,
                                                                                  modules::ModuleLifecycleApi::Start start,
                                                                                  modules::ModuleLifecycleApi::Action stop = stop_provider<Service>) {
      return {
          .provider = state.provider,
          .api = {.state = &state,
                  .configure = configure_provider<Service>,
                  .start = start,
                  .quiesce = quiesce_provider<Service>,
                  .stop = stop,
                  .rollback = rollback_provider<Service>},
      };
    }

    std::vector<modules::TargetPlatform> all_targets() {
      return {modules::TargetPlatform::Windows, modules::TargetPlatform::Linux,   modules::TargetPlatform::MacOS,
              modules::TargetPlatform::Web,     modules::TargetPlatform::Android, modules::TargetPlatform::IOS};
    }

    modules::TargetPlatform current_target() noexcept {
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

    template <typename Issue> void add_issues(GraphicsCompositionResult& result, GraphicsCompositionIssueCode code, std::span<Issue> issues) {
      for (const auto& issue : issues) result.issues.push_back({code, issue.message});
    }

    template <typename Issue>
    void append_issues(std::vector<GraphicsCompositionIssue>& destination, GraphicsCompositionIssueCode code, std::span<Issue> issues) {
      for (const auto& issue : issues) destination.push_back({code, issue.message});
    }

  }  // namespace

  struct GraphicsComposition::Impl {
    Impl(modules::CapabilityRegistry registry_value, modules::ModuleResolution resolution_value)
        : registry(std::move(registry_value)), resolution(std::move(resolution_value)) {}

    modules::CapabilityRegistry registry;
    modules::ModuleResolution resolution;
    std::vector<std::string> trace;
    ProviderState<PlatformService> platform;
    ProviderState<TargetService> target;
    ProviderState<RenderService> render;
    ProviderState<RuntimeService> runtime;
    modules::ModuleActivationResult activation;
    modules::ExecutionGraphResult graph;
    std::optional<modules::CapabilityHandle<RuntimeService>> runtime_handle;
  };

  modules::ProductDescriptor default_graphics_product() {
    return {
        .schema = modules::project_schema_version,
        .name = "modular-graphics",
        .modules = {{.alias = "platform", .provider = "default"},
                    {.alias = "target", .provider = "default"},
                    {.alias = "renderer", .provider = "default"},
                    {.alias = "runtime", .provider = "default"}},
        .plugins = {},
        .profiles = {{.name = "release", .linkage = modules::LinkageMode::Static, .editor = false}},
    };
  }

  GraphicsComposition::GraphicsComposition(std::unique_ptr<Impl> implementation) : impl_(std::move(implementation)) {}

  GraphicsComposition::~GraphicsComposition() {
    if (active()) (void)stop();
  }

  bool GraphicsComposition::tick(float dt) noexcept {
    if (!active() || !impl_->graph.ok() || !impl_->runtime_handle.has_value()) return false;
    auto* runtime = impl_->graph.graph->service(*impl_->runtime_handle);
    if (runtime == nullptr) return false;
    runtime->elapsed_seconds += dt;
    ++runtime->ticks;
    return true;
  }

  bool GraphicsComposition::draw(WGPURenderPassEncoder pass) noexcept {
    if (pass == nullptr || !active() || !impl_->graph.ok() || !impl_->runtime_handle.has_value()) return false;
    auto* runtime = impl_->graph.graph->service(*impl_->runtime_handle);
    if (runtime == nullptr) return false;
    ++runtime->draws;
    return true;
  }

  std::uint64_t GraphicsComposition::ticks() const noexcept { return impl_->runtime.service.ticks; }

  std::uint64_t GraphicsComposition::draws() const noexcept { return impl_->runtime.service.draws; }

  bool GraphicsComposition::active() const noexcept {
    return impl_->activation.activation != nullptr && impl_->activation.activation->state() == modules::ModuleLifecycleState::Active;
  }

  bool GraphicsComposition::stopped() const noexcept {
    return impl_->activation.activation != nullptr && impl_->activation.activation->state() == modules::ModuleLifecycleState::Stopped;
  }

  bool GraphicsComposition::resources_alive_during_shutdown() const noexcept { return impl_->render.service.resources_alive_during_shutdown; }

  modules::ModuleLifecycleResult GraphicsComposition::stop() {
    modules::ModuleLifecycleResult result;
    if (impl_->activation.activation == nullptr) return result;
    if (impl_->activation.activation->state() == modules::ModuleLifecycleState::Active) {
      auto quiesced = impl_->activation.activation->quiesce();
      result.issues.insert(result.issues.end(), std::make_move_iterator(quiesced.issues.begin()), std::make_move_iterator(quiesced.issues.end()));
    }
    if (impl_->activation.activation->state() == modules::ModuleLifecycleState::Quiesced) {
      auto stopped_result = impl_->activation.activation->stop();
      result.issues.insert(result.issues.end(), std::make_move_iterator(stopped_result.issues.begin()),
                           std::make_move_iterator(stopped_result.issues.end()));
    }
    return result;
  }

  const modules::CapabilityRegistry& GraphicsComposition::registry() const noexcept { return impl_->registry; }

  const modules::ModuleResolution& GraphicsComposition::resolution() const noexcept { return impl_->resolution; }

  const std::vector<std::string>& GraphicsComposition::lifecycle_trace() const noexcept { return impl_->trace; }

  GraphicsCompositionResult create_graphics_composition(app::App& host, const modules::ProductDescriptor& product, modules::TargetPlatform target,
                                                        std::string_view profile) {
    GraphicsCompositionResult result;
    if (host.settings.render_mode == app::AppSettings::RenderMode::HeadlessNone || !host.gpu.operational() || host.gpu.device() == nullptr
        || host.gpu.queue() == nullptr) {
      result.issues.push_back({GraphicsCompositionIssueCode::HostResources, "graphics composition requires an operational WebGPU host"});
      return result;
    }

    modules::CapabilityRegistryBuilder registry_builder;
    registry_builder.add({.id = std::string(platform_provider_id),
                          .version = {1, 0, 0},
                          .provides = {std::string(platform_capability_id)},
                          .targets = all_targets(),
                          .linkages = {modules::LinkageMode::Static}});
    registry_builder.add({.id = std::string(target_provider_id),
                          .version = {1, 0, 0},
                          .provides = {std::string(target_capability_id)},
                          .required = {std::string(platform_capability_id)},
                          .targets = all_targets(),
                          .linkages = {modules::LinkageMode::Static}});
    registry_builder.add({.id = std::string(render_provider_id),
                          .version = {1, 0, 0},
                          .provides = {std::string(render_capability_id)},
                          .required = {std::string(target_capability_id)},
                          .targets = all_targets(),
                          .linkages = {modules::LinkageMode::Static}});
    registry_builder.add({.id = std::string(runtime_provider_id),
                          .version = {1, 0, 0},
                          .provides = {std::string(runtime_capability_id)},
                          .required = {std::string(render_capability_id)},
                          .targets = all_targets(),
                          .linkages = {modules::LinkageMode::Static}});
    auto registry_result = registry_builder.build();
    if (!registry_result.ok()) {
      add_issues(result, GraphicsCompositionIssueCode::Registry, std::span(registry_result.issues));
      return result;
    }
    auto registry = std::move(*registry_result.registry);

    modules::ResolverOptions options{
        .target = target,
        .profile = std::string(profile),
        .aliases = {{.alias = "platform", .capability = std::string(platform_capability_id)},
                    {.alias = "target", .capability = std::string(target_capability_id)},
                    {.alias = "renderer", .capability = std::string(render_capability_id)},
                    {.alias = "runtime", .capability = std::string(runtime_capability_id)}},
        .defaults = {{.target = target,
                      .profile = std::string(profile),
                      .capability = std::string(platform_capability_id),
                      .provider = std::string(platform_provider_id)},
                     {.target = target,
                      .profile = std::string(profile),
                      .capability = std::string(target_capability_id),
                      .provider = std::string(target_provider_id)},
                     {.target = target,
                      .profile = std::string(profile),
                      .capability = std::string(render_capability_id),
                      .provider = std::string(render_provider_id)},
                     {.target = target,
                      .profile = std::string(profile),
                      .capability = std::string(runtime_capability_id),
                      .provider = std::string(runtime_provider_id)}},
    };
    auto resolution_result = modules::resolve_modules(product, registry, options);
    if (!resolution_result.ok()) {
      add_issues(result, GraphicsCompositionIssueCode::Resolution, std::span(resolution_result.issues));
      return result;
    }

    auto implementation = std::make_unique<GraphicsComposition::Impl>(std::move(registry), std::move(*resolution_result.resolution));
    const auto platform_provider = implementation->registry.find_provider(platform_provider_id);
    const auto target_provider = implementation->registry.find_provider(target_provider_id);
    const auto render_provider = implementation->registry.find_provider(render_provider_id);
    const auto runtime_provider = implementation->registry.find_provider(runtime_provider_id);
    const auto platform_capability = implementation->registry.find_capability(platform_capability_id);
    const auto target_capability = implementation->registry.find_capability(target_capability_id);
    const auto render_capability = implementation->registry.find_capability(render_capability_id);
    const auto runtime_capability = implementation->registry.find_capability(runtime_capability_id);
    if (!platform_provider || !target_provider || !render_provider || !runtime_provider || !platform_capability || !target_capability
        || !render_capability || !runtime_capability) {
      result.issues.push_back({GraphicsCompositionIssueCode::Registry, "graphics provider indices are unavailable"});
      return result;
    }

    implementation->platform = {.id = platform_provider_id,
                                .provider = *platform_provider,
                                .capability = *platform_capability,
                                .dependency = *platform_capability,
                                .service = {.host = &host},
                                .trace = &implementation->trace};
    implementation->target = {.id = target_provider_id,
                              .provider = *target_provider,
                              .capability = *target_capability,
                              .dependency = *platform_capability,
                              .expected_dependency_service = &implementation->platform.service,
                              .service = {.host = &host,
                                          .window = host.window,
                                          .width = host.width(),
                                          .height = host.height(),
                                          .offscreen = host.settings.render_mode == app::AppSettings::RenderMode::HeadlessNull},
                              .trace = &implementation->trace};
    implementation->render = {.id = render_provider_id,
                              .provider = *render_provider,
                              .capability = *render_capability,
                              .dependency = *target_capability,
                              .expected_dependency_service = &implementation->target.service,
                              .service = {.host = &host, .device = host.gpu.device(), .queue = host.gpu.queue(), .surface = host.gpu.surface()},
                              .trace = &implementation->trace};
    implementation->runtime = {.id = runtime_provider_id,
                               .provider = *runtime_provider,
                               .capability = *runtime_capability,
                               .dependency = *render_capability,
                               .expected_dependency_service = &implementation->render.service,
                               .trace = &implementation->trace};

    const std::array bindings{
        lifecycle_binding(implementation->platform, start_platform),
        lifecycle_binding(implementation->target, start_target),
        lifecycle_binding(implementation->render, start_render, stop_render),
        lifecycle_binding(implementation->runtime, start_runtime),
    };
    implementation->activation = modules::activate_modules(implementation->registry, implementation->resolution, std::span(bindings));
    if (!implementation->activation.ok()) {
      add_issues(result, GraphicsCompositionIssueCode::Activation, std::span(implementation->activation.issues));
      return result;
    }
    implementation->graph
        = modules::freeze_execution_graph(implementation->registry, implementation->resolution, *implementation->activation.activation);
    if (!implementation->graph.ok()) {
      add_issues(result, GraphicsCompositionIssueCode::Freeze, std::span(implementation->graph.issues));
      return result;
    }
    implementation->runtime_handle = implementation->graph.graph->handle<RuntimeService>(*runtime_capability);
    if (!implementation->runtime_handle) {
      result.issues.push_back({GraphicsCompositionIssueCode::Freeze, "graphics runtime handle is unavailable"});
      return result;
    }

    result.composition = std::unique_ptr<GraphicsComposition>(new GraphicsComposition(std::move(implementation)));
    return result;
  }

  GraphicsAppCallbacks::GraphicsAppCallbacks(std::uint64_t exit_after_frames)
      : product_(default_graphics_product()), exit_after_frames_(exit_after_frames) {}

  SDL_AppResult GraphicsAppCallbacks::on_init(app::App& host, int argc, char** argv) {
    (void)app::AppSettings::parse(argc, argv, host.settings);
    for (int index = 1; index < argc; ++index) {
      if (argv[index] != nullptr && std::string_view(argv[index]) == "--mobagen-exit-after-frame") exit_after_frames_ = 1;
    }
    host.settings.title = "Mobagen Modular Graphics";
    return SDL_APP_CONTINUE;
  }

  SDL_AppResult GraphicsAppCallbacks::on_ready(app::App& host) {
    auto result = create_graphics_composition(host, product_, current_target(), "release");
    issues_ = std::move(result.issues);
    composition_ = std::move(result.composition);
    if (composition_ != nullptr) return SDL_APP_CONTINUE;
    for (const auto& issue : issues_) SDL_Log("Graphics composition: %s", issue.message.c_str());
    return SDL_APP_FAILURE;
  }

  SDL_AppResult GraphicsAppCallbacks::on_iterate(app::App&, float dt) {
    if (runtime_failed_ || composition_ == nullptr || !composition_->active()) return SDL_APP_FAILURE;
    if (exit_after_frames_ != 0 && composition_->draws() >= exit_after_frames_) return SDL_APP_SUCCESS;
    return composition_->tick(dt) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
  }

  void GraphicsAppCallbacks::on_draw(app::App&, WGPURenderPassEncoder pass) {
    if (composition_ == nullptr || !composition_->draw(pass)) runtime_failed_ = true;
  }

  void GraphicsAppCallbacks::on_shutdown(app::App&) {
    if (composition_ == nullptr) return;
    const auto stopped_result = composition_->stop();
    append_issues(issues_, GraphicsCompositionIssueCode::Shutdown, std::span(stopped_result.issues));
  }

  bool GraphicsAppCallbacks::active() const noexcept { return composition_ != nullptr && composition_->active(); }

  bool GraphicsAppCallbacks::stopped() const noexcept { return composition_ != nullptr && composition_->stopped(); }

  bool GraphicsAppCallbacks::resources_alive_during_shutdown() const noexcept {
    return composition_ != nullptr && composition_->resources_alive_during_shutdown();
  }

  std::uint64_t GraphicsAppCallbacks::ticks() const noexcept { return composition_ == nullptr ? 0 : composition_->ticks(); }

  std::uint64_t GraphicsAppCallbacks::draws() const noexcept { return composition_ == nullptr ? 0 : composition_->draws(); }

  const modules::CapabilityRegistry* GraphicsAppCallbacks::registry() const noexcept {
    return composition_ == nullptr ? nullptr : &composition_->registry();
  }

  const modules::ModuleResolution* GraphicsAppCallbacks::resolution() const noexcept {
    return composition_ == nullptr ? nullptr : &composition_->resolution();
  }

  const std::vector<std::string>& GraphicsAppCallbacks::lifecycle_trace() const noexcept {
    static const std::vector<std::string> empty;
    return composition_ == nullptr ? empty : composition_->lifecycle_trace();
  }

  std::span<const GraphicsCompositionIssue> GraphicsAppCallbacks::issues() const noexcept { return issues_; }

}  // namespace mobagen::compositions
