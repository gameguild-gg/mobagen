#include "project_module_manager.hpp"

#include "portable/locked_project.hpp"
#include "project_support.hpp"
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
#include "native/locked_project.hpp"
#endif

#include <algorithm>
#include <utility>

namespace mobagen::compositions {

  struct ProjectModuleManager::Storage {
    ProjectModuleRuntimeKind kind{};
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
    std::unique_ptr<NativeModuleManager> native;
#endif
    std::unique_ptr<PortableModuleManager> portable;
  };

  namespace {

    template <typename Issue>
    ProjectModuleManagerIssue simplify_issue(Issue&& issue) {
      return {
          .provider_id = std::move(issue.provider_id),
          .message = std::move(issue.message),
      };
    }

    template <typename Issues>
    ProjectModuleManagerActionResult simplify_action(Issues&& issues) {
      ProjectModuleManagerActionResult result;
      result.issues.reserve(issues.size());
      for (auto& issue : issues) {
        result.issues.push_back(simplify_issue(std::move(issue)));
      }
      return result;
    }

    template <typename ProjectIssue>
    void append_open_issues(
        LockedProjectResult& result, LockedProjectIssueCode code,
        std::vector<ProjectIssue>&& issues
    ) {
      result.issues.reserve(result.issues.size() + issues.size());
      for (auto& issue : issues) {
        result.issues.push_back({.code = code, .message = std::move(issue.message)});
      }
    }

  }  // namespace

  ProjectModuleManager::ProjectModuleManager(
      std::unique_ptr<Storage> storage
  ) noexcept
      : storage_(std::move(storage)) {}

  ProjectModuleManager::ProjectModuleManager(ProjectModuleManager&&) noexcept = default;
  ProjectModuleManager& ProjectModuleManager::operator=(
      ProjectModuleManager&&
  ) noexcept = default;
  ProjectModuleManager::~ProjectModuleManager() = default;

  ProjectModuleRuntimeKind ProjectModuleManager::kind() const noexcept {
    return storage_->kind;
  }

  ProjectModuleManagerActionResult ProjectModuleManager::activate(
      std::string_view capability
  ) {
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
    if (storage_->native != nullptr) {
      auto activated = storage_->native->activate(capability);
      return simplify_action(std::move(activated.issues));
    }
#endif
    auto activated = storage_->portable->activate(capability);
    return simplify_action(std::move(activated.issues));
  }

  ProjectModuleManagerActionResult ProjectModuleManager::stop() {
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
    if (storage_->native != nullptr) {
      auto stopped = storage_->native->stop();
      return simplify_action(std::move(stopped.issues));
    }
#endif
    auto stopped = storage_->portable->stop();
    return simplify_action(std::move(stopped.issues));
  }

  std::size_t ProjectModuleManager::active_count() const noexcept {
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
    if (storage_->native != nullptr) return storage_->native->active_count();
#endif
    return storage_->portable->active_count();
  }

  NativeModuleManager* ProjectModuleManager::native() noexcept {
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
    return storage_->native.get();
#else
    return nullptr;
#endif
  }

  const NativeModuleManager* ProjectModuleManager::native() const noexcept {
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
    return storage_->native.get();
#else
    return nullptr;
#endif
  }

  PortableModuleManager* ProjectModuleManager::portable() noexcept {
    return storage_->portable.get();
  }

  const PortableModuleManager* ProjectModuleManager::portable() const noexcept {
    return storage_->portable.get();
  }

  LockedProjectResult open_locked_project(
      const std::filesystem::path& manifest_path, LockedProjectOptions options,
      LockedProjectServices services
  ) {
    LockedProjectResult result;
    auto source = detail::read_project_manifest_bounded(manifest_path);
    if (!source.ok()) {
      result.issues.push_back({
          .code = LockedProjectIssueCode::ReadManifest,
          .message = std::move(source.error),
      });
      return result;
    }
    auto parsed = modules::parse_product_manifest(
        *source.contents, source.absolute_path.generic_string()
    );
    if (!parsed.ok()) {
      result.issues.push_back({
          .code = LockedProjectIssueCode::ParseManifest,
          .message = "mobagen.yaml is invalid",
          .manifest_errors = std::move(parsed.errors),
      });
      return result;
    }
    const auto profile = std::ranges::find(
        parsed.descriptor->profiles, options.profile,
        &modules::ProfileDescriptor::name
    );
    if (profile == parsed.descriptor->profiles.end()) {
      result.issues.push_back({
          .code = LockedProjectIssueCode::ProfileUnavailable,
          .message = "selected profile is absent from mobagen.yaml",
      });
      return result;
    }

    if (profile->linkage == modules::LinkageMode::Wasm) {
      if (services.portable_backend == nullptr) {
        result.issues.push_back({
            .code = LockedProjectIssueCode::PortableBackendUnavailable,
            .message = "selected wasm profile requires a portable backend",
        });
        return result;
      }
      auto opened = open_locked_portable_project(
          source.absolute_path,
          {
              .sdk_version = options.sdk_version,
              .target = options.target,
              .profile = std::move(options.profile),
          },
          *services.portable_backend, services.builtin_providers,
          services.wasm_host_services
      );
      if (!opened.ok()) {
        append_open_issues(
            result, LockedProjectIssueCode::PortableProject,
            std::move(opened.issues)
        );
        return result;
      }
      auto storage = std::make_unique<ProjectModuleManager::Storage>();
      storage->kind = ProjectModuleRuntimeKind::Portable;
      storage->portable = std::move(opened.manager);
      result.manager = std::unique_ptr<ProjectModuleManager>(
          new ProjectModuleManager(std::move(storage))
      );
      result.product = std::move(opened.product);
      return result;
    }

    if (profile->linkage == modules::LinkageMode::Dynamic) {
#if defined(MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE)
      auto opened = open_locked_native_project(
          source.absolute_path,
          {
              .sdk_version = options.sdk_version,
              .target = options.target,
              .profile = std::move(options.profile),
          }
      );
      if (!opened.ok()) {
        append_open_issues(
            result, LockedProjectIssueCode::NativeProject,
            std::move(opened.issues)
        );
        return result;
      }
      auto storage = std::make_unique<ProjectModuleManager::Storage>();
      storage->kind = ProjectModuleRuntimeKind::Native;
      storage->native = std::move(opened.manager);
      result.manager = std::unique_ptr<ProjectModuleManager>(
          new ProjectModuleManager(std::move(storage))
      );
      result.product = std::move(opened.product);
      return result;
#else
      result.issues.push_back({
          .code = LockedProjectIssueCode::UnsupportedLinkage,
          .message = "dynamic profiles are unavailable on this platform",
      });
      return result;
#endif
    }

    result.issues.push_back({
        .code = LockedProjectIssueCode::UnsupportedLinkage,
        .message = "selected profile linkage has no runtime module manager",
    });
    return result;
  }

}  // namespace mobagen::compositions
