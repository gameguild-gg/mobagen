#include "project_module_manager.hpp"

#include "portable/locked_project.hpp"
#include "project_support.hpp"
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_BROWSER)
#  include "plugins/browser/browser_wasm_backend.hpp"
#endif

#include <algorithm>
#include <map>
#include <utility>

namespace mobagen::compositions {

  struct ProjectModuleManager::Storage {
    std::unique_ptr<PortableModuleManager> portable;
    std::map<std::string, ProjectModuleCapabilityEndpoint, std::less<>> endpoints;
  };

  namespace {

    template <typename Issue> ProjectModuleManagerIssue simplify_issue(Issue&& issue) {
      return {
          .provider_id = std::move(issue.provider_id),
          .message = std::move(issue.message),
      };
    }

    template <typename Issues> std::vector<ProjectModuleManagerIssue> simplify_issues(Issues&& issues) {
      std::vector<ProjectModuleManagerIssue> result;
      result.reserve(issues.size());
      for (auto& issue : issues) {
        result.push_back(simplify_issue(std::move(issue)));
      }
      return result;
    }

    template <typename Issues> ProjectModuleManagerActionResult simplify_action(Issues&& issues) {
      return {.issues = simplify_issues(std::forward<Issues>(issues))};
    }

    template <typename ProjectIssue>
    void append_open_issues(LockedProjectResult& result, LockedProjectIssueCode code, std::vector<ProjectIssue>&& issues) {
      result.issues.reserve(result.issues.size() + issues.size());
      for (auto& issue : issues) {
        result.issues.push_back({.code = code, .message = std::move(issue.message)});
      }
    }

  }  // namespace

  ProjectModuleManager::ProjectModuleManager(std::unique_ptr<Storage> storage) noexcept : storage_(std::move(storage)) {}

  ProjectModuleManager::ProjectModuleManager(ProjectModuleManager&&) noexcept = default;
  ProjectModuleManager& ProjectModuleManager::operator=(ProjectModuleManager&&) noexcept = default;
  ProjectModuleManager::~ProjectModuleManager() = default;

  ProjectModuleManagerActionResult ProjectModuleManager::activate(std::string_view capability) {
    auto activated = storage_->portable->activate(capability);
    return simplify_action(std::move(activated.issues));
  }

  ProjectModuleCapabilityResult ProjectModuleManager::acquire(std::string_view capability) {
    ProjectModuleCapabilityResult result;
    if (const auto* endpoint = find_active(capability)) {
      result.endpoint = *endpoint;
      return result;
    }
    auto acquired = storage_->portable->acquire(capability);
    result.issues = simplify_issues(std::move(acquired.issues));
    if (acquired.plugin != nullptr) {
      auto endpoint = ProjectModuleCapabilityEndpoint{
          .portable = acquired.plugin,
      };
      const auto [stored, inserted] = storage_->endpoints.try_emplace(std::string{capability}, endpoint);
      static_cast<void>(inserted);
      result.endpoint = stored->second;
    }
    return result;
  }

  const ProjectModuleCapabilityEndpoint* ProjectModuleManager::find_active(std::string_view capability) const noexcept {
    const auto found = storage_->endpoints.find(capability);
    if (found == storage_->endpoints.end()) return nullptr;
    return &found->second;
  }

  ProjectModuleManagerActionResult ProjectModuleManager::stop() {
    storage_->endpoints.clear();
    auto stopped = storage_->portable->stop();
    return simplify_action(std::move(stopped.issues));
  }

  std::size_t ProjectModuleManager::active_count() const noexcept { return storage_->portable->active_count(); }

  PortableModuleManager* ProjectModuleManager::portable() noexcept { return storage_->portable.get(); }

  const PortableModuleManager* ProjectModuleManager::portable() const noexcept { return storage_->portable.get(); }

  LockedProjectResult open_locked_project(const std::filesystem::path& manifest_path, LockedProjectOptions options, LockedProjectServices services) {
    LockedProjectResult result;
    auto source = detail::read_project_manifest_bounded(manifest_path);
    if (!source.ok()) {
      result.issues.push_back({
          .code = LockedProjectIssueCode::ReadManifest,
          .message = std::move(source.error),
      });
      return result;
    }
    auto parsed = modules::parse_product_manifest(*source.contents, source.absolute_path.generic_string());
    if (!parsed.ok()) {
      result.issues.push_back({
          .code = LockedProjectIssueCode::ParseManifest,
          .message = "mobagen.yaml is invalid",
          .manifest_errors = std::move(parsed.errors),
      });
      return result;
    }
    const auto profile = std::ranges::find(parsed.descriptor->profiles, options.profile, &modules::ProfileDescriptor::name);
    if (profile == parsed.descriptor->profiles.end()) {
      result.issues.push_back({
          .code = LockedProjectIssueCode::ProfileUnavailable,
          .message = "selected profile is absent from mobagen.yaml",
      });
      return result;
    }

    if (profile->linkage == modules::LinkageMode::Wasm) {
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_BROWSER)
      /* Web default backend (todo 8): the browser engine compiles/instantiates
         plugin modules; callers can still inject their own backend. */
      plugins::BrowserWasmBackend bundled_browser_backend;
      plugins::PortableWasmBackend* backend = services.portable_backend != nullptr ? services.portable_backend : &bundled_browser_backend;
#else
      plugins::PortableWasmBackend* backend = services.portable_backend;
#endif
      if (backend == nullptr) {
        result.issues.push_back({
            .code = LockedProjectIssueCode::PortableBackendUnavailable,
            .message = "selected wasm profile requires a portable backend",
        });
        return result;
      }
      auto opened = open_locked_portable_project(source.absolute_path,
                                                 {
                                                     .sdk_version = options.sdk_version,
                                                     .target = options.target,
                                                     .profile = std::move(options.profile),
                                                 },
                                                 *backend, services.builtin_providers, services.wasm_host_services);
      if (!opened.ok()) {
        append_open_issues(result, LockedProjectIssueCode::PortableProject, std::move(opened.issues));
        return result;
      }
      auto storage = std::make_unique<ProjectModuleManager::Storage>();
      storage->portable = std::move(opened.manager);
      result.manager = std::unique_ptr<ProjectModuleManager>(new ProjectModuleManager(std::move(storage)));
      result.product = std::move(opened.product);
      return result;
    }

    result.issues.push_back({
        .code = LockedProjectIssueCode::UnsupportedLinkage,
        .message = "selected profile linkage has no runtime module manager",
    });
    return result;
  }

}  // namespace mobagen::compositions
