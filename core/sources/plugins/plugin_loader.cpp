#include "plugin_loader.hpp"

#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#elif !defined(__EMSCRIPTEN__)
#  include <dlfcn.h>
#endif

namespace mobagen::plugins {
  namespace {

    [[nodiscard]] bool valid_host(const MobagenHostApiV1& host) {
      return host.struct_size >= MOBAGEN_PLUGIN_HOST_API_V1_SIZE && host.abi_version == MOBAGEN_PLUGIN_ABI_VERSION && host.allocate != nullptr
             && host.deallocate != nullptr && host.log != nullptr && host.publish_capability != nullptr && host.find_capability != nullptr;
    }

    void add_issue(NativePluginLoadResult& result, NativePluginLoadIssueCode code, const std::filesystem::path& path, std::string message,
                   std::error_code system_error = {}, std::vector<PluginContractIssue> issues = {}) {
      result.issues.push_back({code, path, system_error, std::move(message), std::move(issues)});
    }

    [[nodiscard]] void* open_library(const std::filesystem::path& path, std::error_code& error, std::string& message) {
#ifdef _WIN32
      const auto handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
      if (handle == nullptr) {
        error = {static_cast<int>(GetLastError()), std::system_category()};
        message = error.message();
      }
      return handle;
#elif defined(__EMSCRIPTEN__)
      (void)path;
      error = std::make_error_code(std::errc::not_supported);
      message = "native dynamic libraries are unavailable on WebAssembly";
      return nullptr;
#else
      void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (handle == nullptr) {
        if (const char* detail = dlerror(); detail != nullptr) {
          message = detail;
        } else {
          message = "dlopen failed without a diagnostic";
        }
      }
      return handle;
#endif
    }

    void close_library(void* handle) noexcept {
      if (handle == nullptr) {
        return;
      }
#ifdef _WIN32
      FreeLibrary(static_cast<HMODULE>(handle));
#elif !defined(__EMSCRIPTEN__)
      dlclose(handle);
#endif
    }

    [[nodiscard]] MobagenPluginEntryV1Fn find_entry(void* handle, std::error_code& error, std::string& message) {
#ifdef _WIN32
      const auto symbol = GetProcAddress(static_cast<HMODULE>(handle), MOBAGEN_PLUGIN_ENTRY_V1_SYMBOL);
      if (symbol == nullptr) {
        error = {static_cast<int>(GetLastError()), std::system_category()};
        message = error.message();
        return nullptr;
      }
      return reinterpret_cast<MobagenPluginEntryV1Fn>(symbol);
#elif defined(__EMSCRIPTEN__)
      (void)handle;
      error = std::make_error_code(std::errc::not_supported);
      message = "native dynamic symbols are unavailable on WebAssembly";
      return nullptr;
#else
      dlerror();
      void* symbol = dlsym(handle, MOBAGEN_PLUGIN_ENTRY_V1_SYMBOL);
      if (const char* detail = dlerror(); detail != nullptr) {
        message = detail;
        return nullptr;
      }
      static_assert(sizeof(symbol) == sizeof(MobagenPluginEntryV1Fn));
      MobagenPluginEntryV1Fn entry = nullptr;
      std::memcpy(&entry, &symbol, sizeof(entry));
      return entry;
#endif
    }

    void destroy_invalid_descriptor(MobagenPluginDescriptorV1& descriptor) noexcept {
      if (descriptor.lifecycle.struct_size < MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE || descriptor.lifecycle.destroy == nullptr) {
        return;
      }
      try {
        descriptor.lifecycle.destroy(descriptor.plugin_state);
      } catch (...) {
      }
    }

  }  // namespace

  NativePlugin::NativePlugin(std::filesystem::path path, void* library_handle, NativePluginContract contract)
      : path_(std::move(path)), library_handle_(library_handle), contract_(std::move(contract)) {}

  NativePlugin::~NativePlugin() { reset(); }

  NativePlugin::NativePlugin(NativePlugin&& other) noexcept
      : path_(std::move(other.path_)), library_handle_(std::exchange(other.library_handle_, nullptr)), contract_(std::move(other.contract_)) {
    other.contract_.reset();
  }

  NativePlugin& NativePlugin::operator=(NativePlugin&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    reset();
    path_ = std::move(other.path_);
    library_handle_ = std::exchange(other.library_handle_, nullptr);
    contract_ = std::move(other.contract_);
    other.contract_.reset();
    return *this;
  }

  bool NativePlugin::loaded() const noexcept { return library_handle_ != nullptr && contract_.has_value(); }

  void NativePlugin::reset() noexcept {
    if (contract_.has_value() && contract_->lifecycle.destroy != nullptr) {
      try {
        contract_->lifecycle.destroy(contract_->plugin_state);
      } catch (...) {
      }
    }
    contract_.reset();
    close_library(std::exchange(library_handle_, nullptr));
    path_.clear();
  }

  NativePluginLoadResult load_native_plugin_binary(const std::filesystem::path& path, const MobagenHostApiV1& host) {
    NativePluginLoadResult result;
    if (!valid_host(host)) {
      add_issue(result, NativePluginLoadIssueCode::invalid_host, path, "host API is incomplete or incompatible");
      return result;
    }
    if (path.empty()) {
      add_issue(result, NativePluginLoadIssueCode::invalid_path, path, "plugin binary path must name a file");
      return result;
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error || !std::filesystem::is_regular_file(absolute, error)) {
      add_issue(result, NativePluginLoadIssueCode::open_failed, path, "plugin binary is not a readable regular file", error);
      return result;
    }

    std::string detail;
    void* library = open_library(absolute, error, detail);
    if (library == nullptr) {
      add_issue(result, NativePluginLoadIssueCode::open_failed, absolute, "could not open plugin binary: " + detail, error);
      return result;
    }

    auto entry = find_entry(library, error, detail);
    if (entry == nullptr) {
      add_issue(result, NativePluginLoadIssueCode::missing_entry_point, absolute,
                "plugin does not export " MOBAGEN_PLUGIN_ENTRY_V1_SYMBOL ": " + detail, error);
      close_library(library);
      return result;
    }

    MobagenPluginDescriptorV1 descriptor{};
    descriptor.struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE;
    descriptor.abi_version = MOBAGEN_PLUGIN_ABI_VERSION;
    descriptor.lifecycle.struct_size = MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE;
    MobagenStatus status = MOBAGEN_STATUS_FAILED;
    try {
      status = entry(&host, &descriptor);
    } catch (...) {
      status = MOBAGEN_STATUS_FAILED;
    }
    if (status != MOBAGEN_STATUS_OK) {
      add_issue(result, NativePluginLoadIssueCode::entry_failed, absolute,
                "plugin entry point rejected the host with status " + std::to_string(status));
      close_library(library);
      return result;
    }

    auto validated = validate_native_plugin(descriptor);
    if (!validated.contract.has_value()) {
      add_issue(result, NativePluginLoadIssueCode::invalid_descriptor, absolute, "plugin returned an invalid descriptor", {},
                std::move(validated.issues));
      destroy_invalid_descriptor(descriptor);
      close_library(library);
      return result;
    }

    result.plugin = NativePlugin{absolute, library, std::move(*validated.contract)};
    return result;
  }

}  // namespace mobagen::plugins
