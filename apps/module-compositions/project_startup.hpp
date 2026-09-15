#pragma once

#include "project_bootstrap.hpp"
#include "project_module_manager.hpp"

#include <filesystem>

namespace mobagen::compositions {

  struct ProjectStartupServices {
    http::Client* http_client{};
    LockedProjectServices modules;
  };

  struct ProjectStartupResult {
    ProjectBootstrapResult bootstrap;
    LockedProjectResult project;

    [[nodiscard]] bool ok() const noexcept { return bootstrap.ok() && project.ok(); }
  };

  /* First-run composition for a module-managed product. Missing or stale
     modules are synchronized, then a cold manager is opened. No capability is
     activated and no plugin body is read on an already prepared lazy run. */
  [[nodiscard]] ProjectStartupResult prepare_and_open_project(const std::filesystem::path& manifest_path, ProjectBootstrapOptions options,
                                                              ProjectStartupServices services = {});

}  // namespace mobagen::compositions
