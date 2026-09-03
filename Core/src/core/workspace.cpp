#include "statewright/core/workspace.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <fnmatch.h>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace statewright::core {
namespace {

const std::set<std::string, std::less<>> ignored_names = {
    ".git",          ".hg",          ".svn",      ".idea",
    ".vscode",       ".ourd-agent",  ".statewright", "__pycache__",
    ".pytest_cache", ".mypy_cache", "node_modules", "dist",
    "build",         ".venv",       "venv",
};

[[nodiscard]] std::string normalized_pattern(std::string_view value) {
  std::string result(value);
  std::replace(result.begin(), result.end(), '\\', '/');
  const auto first = result.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = result.find_last_not_of(" \t\n\r\f\v");
  return result.substr(first, last - first + 1);
}

[[nodiscard]] bool internal_path(std::string_view canonical) {
  for (const std::string_view name : {std::string_view(".statewright"),
                                      std::string_view(".ourd-agent")}) {
    if (canonical == name || canonical.starts_with(std::string(name) + "/")) {
      return true;
    }
  }
  return false;
}

} // namespace

Workspace::Workspace(std::filesystem::path root) {
  std::error_code error;
  root_ = std::filesystem::canonical(std::move(root), error);
  if (error || !std::filesystem::is_directory(root_)) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "workspace root is not an accessible directory");
  }
}

const std::filesystem::path &Workspace::root() const noexcept { return root_; }

bool Workspace::is_within(const std::filesystem::path &path,
                          const std::filesystem::path &root) {
  auto path_part = path.begin();
  auto root_part = root.begin();
  for (; root_part != root.end(); ++root_part, ++path_part) {
    if (path_part == path.end() || *path_part != *root_part) {
      return false;
    }
  }
  return true;
}

std::string Workspace::canonical(std::string_view relative,
                                 bool allow_internal) const {
  if (relative.empty() ||
      relative.find_first_not_of(" \t\n\r\f\v") == std::string_view::npos) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "path must be a non-empty string");
  }
  if (relative.find('\0') != std::string_view::npos) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "path contains a NUL byte");
  }

  const std::filesystem::path requested(relative);
  if (requested.is_absolute()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "absolute path is not allowed");
  }

  std::error_code error;
  const std::filesystem::path resolved =
      std::filesystem::weakly_canonical(root_ / requested, error);
  if (error || !is_within(resolved, root_)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "path escapes workspace");
  }

  std::string result;
  if (resolved == root_) {
    result = ".";
  } else {
    result = resolved.lexically_relative(root_).generic_string();
  }
  if (!allow_internal && internal_path(result)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "internal StateWright state is not accessible");
  }
  return result;
}

std::filesystem::path Workspace::resolve(std::string_view relative,
                                         bool allow_internal) const {
  const std::string canonical_path = canonical(relative, allow_internal);
  return canonical_path == "." ? root_ : root_ / canonical_path;
}

std::string Workspace::relative(const std::filesystem::path &path,
                                bool allow_internal) const {
  std::error_code error;
  const auto resolved = std::filesystem::weakly_canonical(path, error);
  if (error || !is_within(resolved, root_)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "path escapes workspace");
  }
  return canonical(resolved.lexically_relative(root_).generic_string(),
                   allow_internal);
}

bool Workspace::matches(std::string_view path,
                        const std::vector<std::string> &patterns) {
  if (patterns.empty()) {
    return false;
  }
  std::string normalized(path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  for (const auto &raw_pattern : patterns) {
    const std::string pattern = normalized_pattern(raw_pattern);
    if (pattern == "*" || pattern == "**" || pattern == ".") {
      return true;
    }
    if (!pattern.empty() && fnmatch(pattern.c_str(), normalized.c_str(), 0) == 0) {
      return true;
    }
    if (pattern.ends_with("/**")) {
      const std::string base = pattern.substr(0, pattern.size() - 3);
      if (normalized == base || normalized == base + "/") {
        return true;
      }
    }
    std::string exact = pattern;
    while (exact.ends_with('/')) {
      exact.pop_back();
    }
    if (normalized == exact) {
      return true;
    }
  }
  return false;
}

std::string Workspace::require_scope(
    std::string_view relative,
    const std::vector<std::string> &allowed_patterns,
    const std::vector<std::string> &forbidden_patterns) const {
  const std::string canonical_path = canonical(relative);
  if (!matches(canonical_path, allowed_patterns)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "path is outside allowed scope");
  }
  if (matches(canonical_path, forbidden_patterns)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "path intersects forbidden scope");
  }
  return canonical_path;
}

bool Workspace::ignored_component(std::string_view component) {
  return ignored_names.contains(component) || component.ends_with(".egg-info");
}

std::vector<std::filesystem::path> Workspace::files(std::string_view relative) const {
  const std::filesystem::path start = resolve(relative);
  std::vector<std::filesystem::path> result;
  std::error_code error;

  const auto accept = [&](const std::filesystem::path &path) {
    const auto relative_path = path.lexically_relative(root_);
    for (const auto &part : relative_path) {
      if (ignored_component(part.string())) {
        return false;
      }
    }
    if (std::filesystem::is_symlink(path)) {
      return false;
    }
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    return !error && is_within(resolved, root_) &&
           std::filesystem::is_regular_file(resolved);
  };

  if (std::filesystem::is_regular_file(start) && accept(start)) {
    result.push_back(start);
  } else if (std::filesystem::is_directory(start)) {
    std::filesystem::recursive_directory_iterator iterator(
        start, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
      const auto &entry = *iterator;
      const auto relative_path = entry.path().lexically_relative(root_);
      bool ignored = false;
      for (const auto &part : relative_path) {
        if (ignored_component(part.string())) {
          ignored = true;
          break;
        }
      }
      if (ignored && entry.is_directory()) {
        iterator.disable_recursion_pending();
      } else if (!ignored && accept(entry.path())) {
        result.push_back(entry.path());
      }
      iterator.increment(error);
    }
  }

  if (error) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot iterate workspace files");
  }
  std::sort(result.begin(), result.end(), [&](const auto &left, const auto &right) {
    return this->relative(left) < this->relative(right);
  });
  return result;
}

std::optional<std::string> Workspace::file_hash(std::string_view relative) const {
  const auto path = resolve(relative);
  if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path)) {
    return std::nullopt;
  }
  return contracts::sha256_file(path);
}

std::map<std::string, std::string> Workspace::snapshot() const {
  std::map<std::string, std::string> result;
  for (const auto &path : files()) {
    result.emplace(relative(path), contracts::sha256_file(path));
  }
  return result;
}

std::string Workspace::file_mode(const std::filesystem::path &path) {
  std::error_code error;
  const auto permissions = std::filesystem::status(path, error).permissions();
  if (error) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot read workspace file mode");
  }
  const auto mode = static_cast<unsigned int>(permissions) & 0777U;
  std::ostringstream output;
  output << "0o" << std::oct << mode;
  return output.str();
}

std::string Workspace::snapshot_hash() const {
  std::string material;
  for (const auto &[path, file_hash_value] : snapshot()) {
    material.append(path);
    material.push_back('\0');
    material.append(file_hash_value);
    material.push_back('\0');
    material.append(file_mode(root_ / path));
    material.push_back('\0');
  }
  return contracts::sha256_text(material);
}

} // namespace statewright::core

