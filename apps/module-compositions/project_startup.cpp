#include "project_startup.hpp"

#include <utility>

namespace mobagen::compositions {

  ProjectStartupResult prepare_and_open_project(const std::filesystem::path& manifest_path, ProjectBootstrapOptions options,
                                                ProjectStartupServices services) {
    ProjectStartupResult result;
    LockedProjectOptions locked_options{
        .sdk_version = options.sdk_version,
        .target = options.resolver.target,
        .profile = options.resolver.profile,
    };
    result.bootstrap = bootstrap_project(manifest_path, std::move(options), services.http_client);
    if (!result.bootstrap.ok()) return result;
    result.project = open_locked_project(manifest_path, std::move(locked_options), services.modules);
    return result;
  }

}  // namespace mobagen::compositions
