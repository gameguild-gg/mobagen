#include "descriptor.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <set>
#include <string_view>
#include <utility>

namespace mobagen::modules {

  namespace {

    bool is_decimal_digit(char value) noexcept { return value >= '0' && value <= '9'; }

    bool is_lower_alphanumeric(char value) noexcept { return (value >= 'a' && value <= 'z') || is_decimal_digit(value); }

    bool is_dotted_id(std::string_view value) noexcept {
      if (value.empty() || value.front() == '.' || value.back() == '.') return false;

      bool has_dot = false;
      bool at_segment_start = true;
      char previous = '\0';
      for (const char value_char : value) {
        if (value_char == '.') {
          if (at_segment_start || !is_lower_alphanumeric(previous)) return false;
          has_dot = true;
          at_segment_start = true;
          previous = value_char;
          continue;
        }
        if (at_segment_start && !is_lower_alphanumeric(value_char)) return false;
        if (!is_lower_alphanumeric(value_char) && value_char != '-') return false;
        at_segment_start = false;
        previous = value_char;
      }
      return has_dot && is_lower_alphanumeric(value.back());
    }

    void add_issue(std::vector<DescriptorIssue>& issues, DescriptorIssueCode code, std::string field, std::string message) {
      issues.push_back({code, std::move(field), std::move(message)});
    }

    template <typename Value>
    void check_enum_duplicates(const std::vector<Value>& values, std::string_view field, std::vector<DescriptorIssue>& issues) {
      std::set<Value> seen;
      for (std::size_t index = 0; index < values.size(); ++index) {
        if (!seen.insert(values[index]).second) {
          add_issue(issues, DescriptorIssueCode::DuplicateEntry, std::string(field) + '[' + std::to_string(index) + ']', "duplicate entry");
        }
      }
    }

    void check_capability_list(const std::vector<std::string>& values, std::string_view field, std::vector<DescriptorIssue>& issues) {
      std::set<std::string> seen;
      for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string item_field = std::string(field) + '[' + std::to_string(index) + ']';
        if (!is_capability_id(values[index])) {
          add_issue(issues, DescriptorIssueCode::InvalidCapability, item_field, "expected a lowercase dotted capability ending in .vN");
        }
        if (!seen.insert(values[index]).second) {
          add_issue(issues, DescriptorIssueCode::DuplicateEntry, item_field, "duplicate capability");
        }
      }
    }

    void check_permission_list(const std::vector<std::string>& values, std::string_view field, std::vector<DescriptorIssue>& issues) {
      std::set<std::string> seen;
      for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string item_field = std::string(field) + '[' + std::to_string(index) + ']';
        if (!is_slug(values[index])) {
          add_issue(issues, DescriptorIssueCode::InvalidIdentifier, item_field, "expected a lowercase permission slug");
        }
        if (!seen.insert(values[index]).second) {
          add_issue(issues, DescriptorIssueCode::DuplicateEntry, item_field, "permissions must be unique");
        }
      }
    }

    void check_self_dependencies(const std::vector<std::string>& dependencies, std::string_view field, const std::vector<std::string>& provided,
                                 std::vector<DescriptorIssue>& issues) {
      for (std::size_t index = 0; index < dependencies.size(); ++index) {
        if (std::ranges::find(provided, dependencies[index]) != provided.end()) {
          add_issue(issues, DescriptorIssueCode::SelfDependency, std::string(field) + '[' + std::to_string(index) + ']',
                    "provider cannot depend on a capability it provides");
        }
      }
    }

  }  // namespace

  bool is_slug(std::string_view value) noexcept {
    if (value.empty() || !is_lower_alphanumeric(value.front()) || !is_lower_alphanumeric(value.back())) {
      return false;
    }
    return std::ranges::all_of(value, [](char value_char) { return is_lower_alphanumeric(value_char) || value_char == '-'; });
  }

  bool is_provider_id(std::string_view value) noexcept { return is_dotted_id(value); }

  bool is_capability_id(std::string_view value) noexcept {
    if (!is_dotted_id(value)) return false;

    const auto version_separator = value.rfind(".v");
    if (version_separator == std::string_view::npos || version_separator + 2 >= value.size()) {
      return false;
    }
    return std::ranges::all_of(value.begin() + static_cast<std::ptrdiff_t>(version_separator + 2), value.end(),
                               [](char value_char) { return is_decimal_digit(value_char); });
  }

  bool is_secure_https_url(std::string_view value) noexcept {
    constexpr std::string_view scheme = "https://";
    if (!value.starts_with(scheme) || value.size() > max_module_source_url_bytes || value.contains('#') || value.contains('@')
        || value.contains('\\')) {
      return false;
    }

    const auto remainder = value.substr(scheme.size());
    const auto authority = remainder.substr(0, remainder.find_first_of("/?"));
    if (authority.empty()) return false;
    if (!std::ranges::all_of(value, [](unsigned char character) { return character > 0x20U && character < 0x7fU; })) return false;

    std::string_view host = authority;
    std::string_view port;
    if (authority.front() == '[') {
      const auto closing_bracket = authority.find(']');
      if (closing_bracket == std::string_view::npos || closing_bracket == 1) return false;
      host = authority.substr(1, closing_bracket - 1);
      const auto suffix = authority.substr(closing_bracket + 1);
      if (!suffix.empty()) {
        if (!suffix.starts_with(':')) return false;
        port = suffix.substr(1);
      }
      if (!std::ranges::all_of(host, [](char character) {
            return is_decimal_digit(character) || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F')
                   || character == ':' || character == '.';
          })) {
        return false;
      }
    } else {
      const auto separator = authority.rfind(':');
      if (separator != std::string_view::npos) {
        if (authority.find(':') != separator) return false;
        host = authority.substr(0, separator);
        port = authority.substr(separator + 1);
      }
      if (host.empty() || host.front() == '.' || host.back() == '.' || !std::ranges::all_of(host, [](char character) {
            return is_lower_alphanumeric(character) || (character >= 'A' && character <= 'Z') || character == '-' || character == '.';
          })) {
        return false;
      }
      std::size_t label_start = 0;
      while (label_start < host.size()) {
        const auto label_end = host.find('.', label_start);
        const auto label = host.substr(label_start, label_end == std::string_view::npos ? host.size() - label_start : label_end - label_start);
        if (label.empty() || label.front() == '-' || label.back() == '-') return false;
        if (label_end == std::string_view::npos) break;
        label_start = label_end + 1;
      }
    }

    if (!port.empty()) {
      std::uint32_t port_number = 0;
      const auto parsed = std::from_chars(port.data(), port.data() + port.size(), port_number);
      if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || port_number == 0 || port_number > 65535) return false;
    } else if (authority.ends_with(':')) {
      return false;
    }
    return true;
  }

  std::vector<DescriptorIssue> validate(const ProductDescriptor& descriptor) {
    std::vector<DescriptorIssue> issues;
    if (descriptor.schema != project_schema_version) {
      add_issue(issues, DescriptorIssueCode::UnsupportedSchema, "schema", "only product schema version 1 is supported");
    }
    if (!is_slug(descriptor.name)) {
      add_issue(issues, DescriptorIssueCode::InvalidIdentifier, "name", "expected a lowercase product slug");
    }

    std::set<std::string> source_names;
    for (const auto& source : descriptor.sources) {
      const std::string field = "sources." + source.name;
      if (!is_slug(source.name)) {
        add_issue(issues, DescriptorIssueCode::InvalidIdentifier, field, "expected a lowercase source slug");
      }
      if (!is_secure_https_url(source.url)) {
        add_issue(issues, DescriptorIssueCode::InvalidSourceUrl, field + ".url",
                  "source URL must be an HTTPS URL without credentials, fragments, whitespace, or control characters");
      }
      if (!source_names.insert(source.name).second) {
        add_issue(issues, DescriptorIssueCode::DuplicateEntry, field, "source names must be unique");
      }
    }

    std::set<std::string> module_aliases;
    for (const auto& request : descriptor.modules) {
      const std::string field = "modules." + request.alias;
      if (!is_slug(request.alias)) {
        add_issue(issues, DescriptorIssueCode::InvalidIdentifier, field, "expected a lowercase module alias");
      }
      if (request.provider != "default" && !is_provider_id(request.provider)) {
        add_issue(issues, DescriptorIssueCode::InvalidIdentifier, field + ".use", "expected 'default' or a lowercase dotted provider ID");
      }
      if (!module_aliases.insert(request.alias).second) {
        add_issue(issues, DescriptorIssueCode::DuplicateEntry, field, "module aliases must be unique");
      }
      if (request.configuration.has_value()) {
        if (!is_capability_id(request.configuration->schema)) {
          add_issue(issues, DescriptorIssueCode::InvalidCapability, field + ".config.schema",
                    "expected a lowercase dotted configuration schema ending in .vN");
        }
        if (request.configuration->data.size() > max_module_configuration_bytes) {
          add_issue(issues, DescriptorIssueCode::LimitExceeded, field + ".config.data", "module configuration exceeds the 1 MiB limit");
        }
      }
    }

    std::set<std::string> plugin_paths;
    for (std::size_t index = 0; index < descriptor.plugins.size(); ++index) {
      const auto& path = descriptor.plugins[index];
      const std::string field = "plugins[" + std::to_string(index) + ']';
      if (path.size() <= std::string_view{".plugin"}.size() || !path.ends_with(".plugin")) {
        add_issue(issues, DescriptorIssueCode::InvalidPluginPath, field, "plugin package paths must end in .plugin");
      }
      if (!plugin_paths.insert(path).second) {
        add_issue(issues, DescriptorIssueCode::DuplicateEntry, field, "plugin package paths must be unique");
      }
    }

    std::set<std::string> profile_names;
    for (const auto& profile : descriptor.profiles) {
      const std::string field = "profiles." + profile.name;
      if (!is_slug(profile.name)) {
        add_issue(issues, DescriptorIssueCode::InvalidIdentifier, field, "expected a lowercase profile slug");
      }
      if (!profile_names.insert(profile.name).second) {
        add_issue(issues, DescriptorIssueCode::DuplicateEntry, field, "profile names must be unique");
      }
      check_permission_list(profile.permissions, field + ".permissions", issues);
    }
    return issues;
  }

  std::vector<DescriptorIssue> validate(const ProviderDescriptor& descriptor) {
    std::vector<DescriptorIssue> issues;
    if (!is_provider_id(descriptor.id)) {
      add_issue(issues, DescriptorIssueCode::InvalidIdentifier, "id", "expected a lowercase dotted provider ID");
    }
    if (descriptor.provides.empty()) {
      add_issue(issues, DescriptorIssueCode::MissingEntry, "provides", "a provider must expose at least one capability");
    }

    check_capability_list(descriptor.provides, "provides", issues);
    check_capability_list(descriptor.required, "required", issues);
    check_capability_list(descriptor.optional, "optional", issues);
    check_self_dependencies(descriptor.required, "required", descriptor.provides, issues);
    check_self_dependencies(descriptor.optional, "optional", descriptor.provides, issues);

    std::set<std::string> conflicts;
    for (std::size_t index = 0; index < descriptor.conflicts.size(); ++index) {
      const std::string field = "conflicts[" + std::to_string(index) + ']';
      if (!is_provider_id(descriptor.conflicts[index])) {
        add_issue(issues, DescriptorIssueCode::InvalidIdentifier, field, "expected a lowercase dotted provider ID");
      }
      if (descriptor.conflicts[index] == descriptor.id) {
        add_issue(issues, DescriptorIssueCode::SelfDependency, field, "provider cannot conflict with itself");
      }
      if (!conflicts.insert(descriptor.conflicts[index]).second) {
        add_issue(issues, DescriptorIssueCode::DuplicateEntry, field, "conflicting providers must be unique");
      }
    }

    if (descriptor.targets.empty()) {
      add_issue(issues, DescriptorIssueCode::MissingEntry, "targets", "a provider must support at least one target");
    }
    if (descriptor.linkages.empty()) {
      add_issue(issues, DescriptorIssueCode::MissingEntry, "linkages", "a provider must support at least one linkage mode");
    }
    check_enum_duplicates(descriptor.targets, "targets", issues);
    check_enum_duplicates(descriptor.linkages, "linkages", issues);

    if (!descriptor.configuration_schema.empty() && !is_capability_id(descriptor.configuration_schema)) {
      add_issue(issues, DescriptorIssueCode::InvalidCapability, "configuration_schema",
                "expected an empty value or a lowercase dotted schema ending in .vN");
    }

    check_permission_list(descriptor.permissions, "permissions", issues);
    return issues;
  }

}  // namespace mobagen::modules
