#include "statewright/egcf/simulation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf {
namespace {

[[noreturn]] void simulation_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "EGCF simulation: " + std::move(message));
}

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    const auto base = std::filesystem::temp_directory_path();
    const auto seed =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
      path_ = base / ("statewright-egcf-simulation-" + std::to_string(seed) +
                      "-" + std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(path_, error)) {
        return;
      }
      if (error && error != std::errc::file_exists) {
        simulation_error("cannot create temporary directory: " +
                         error.message());
      }
    }
    simulation_error("cannot allocate a unique temporary directory");
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

[[nodiscard]] bool ignored_path(const std::filesystem::path &relative) {
  for (const auto &component : relative) {
    const auto value = component.string();
    if (value == ".ourd-agent" || value == ".ourd" ||
        value == "__pycache__") {
      return true;
    }
  }
  return relative.extension() == ".pyc";
}

[[nodiscard]] std::vector<std::filesystem::path>
regular_files(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> result;
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  if (error) {
    simulation_error("cannot enumerate source tree: " + error.message());
  }
  for (; iterator != end; iterator.increment(error)) {
    if (error) {
      simulation_error("cannot enumerate source tree: " + error.message());
    }
    const auto relative = iterator->path().lexically_relative(root);
    if (ignored_path(relative)) {
      if (iterator->is_directory(error)) {
        iterator.disable_recursion_pending();
      }
      error.clear();
      continue;
    }
    const auto status = iterator->symlink_status(error);
    if (error) {
      simulation_error("cannot inspect source tree entry: " + error.message());
    }
    if (std::filesystem::is_symlink(status)) {
      continue;
    }
    if (std::filesystem::is_regular_file(status)) {
      result.push_back(relative);
    }
  }
  std::ranges::sort(result, {}, [](const auto &path) {
    return path.generic_string();
  });
  return result;
}

[[nodiscard]] std::string tree_hash(const std::filesystem::path &root) {
  std::string material;
  for (const auto &relative : regular_files(root)) {
    const auto path_text = relative.generic_string();
    const auto content = core::read_bytes(root / relative);
    material.append(path_text);
    material.push_back('\0');
    material.append(reinterpret_cast<const char *>(content.data()),
                    content.size());
    material.push_back('\0');
  }
  return contracts::sha256_text(material);
}

void copy_tree(const std::filesystem::path &source,
               const std::filesystem::path &target) {
  std::filesystem::create_directories(target);
  for (const auto &relative : regular_files(source)) {
    const auto destination = target / relative;
    std::filesystem::create_directories(destination.parent_path());
    std::error_code error;
    std::filesystem::copy_file(source / relative, destination,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error) {
      simulation_error("cannot copy source tree entry: " + error.message());
    }
  }
}

[[nodiscard]] std::filesystem::path
safe_relative_path(const contracts::Json &value) {
  if (!value.is_string()) {
    simulation_error("worktree simulation change path must be a string");
  }
  const std::filesystem::path path(value.get<std::string>());
  if (path.empty() || path.is_absolute() || path.has_root_name() ||
      path.has_root_directory()) {
    simulation_error("worktree simulation change path must remain relative");
  }
  for (const auto &component : path) {
    if (component == "..") {
      simulation_error("worktree simulation change path must remain relative");
    }
  }
  const auto normalized = path.lexically_normal();
  if (normalized.empty() || normalized == "." || ignored_path(normalized)) {
    simulation_error("worktree simulation change path is not permitted");
  }
  return normalized;
}

[[nodiscard]] std::string replace_text(std::string original,
                                       std::string_view old_text,
                                       std::string_view new_text, int count) {
  if (old_text.empty() || count < 1) {
    simulation_error("worktree replacement requires text and positive count");
  }
  std::size_t position = 0;
  int replaced = 0;
  while (replaced < count &&
         (position = original.find(old_text, position)) != std::string::npos) {
    original.replace(position, old_text.size(), new_text);
    position += new_text.size();
    ++replaced;
  }
  if (replaced == 0) {
    simulation_error("worktree replace text not found");
  }
  return original;
}

[[nodiscard]] std::string required_string(const contracts::Json &object,
                                          std::string_view key) {
  const auto iterator = object.find(key);
  if (iterator == object.end() || !iterator->is_string() ||
      iterator->get_ref<const std::string &>().empty()) {
    simulation_error("migration operations require a non-empty " +
                     std::string(key));
  }
  return iterator->get<std::string>();
}

} // namespace

contracts::Json
SimulationEngine::migration(const contracts::Json &before,
                            const contracts::Json &operations) const {
  if (!before.is_object() || !operations.is_array()) {
    simulation_error("migration requires an object and an operation array");
  }
  contracts::Json state = before;
  contracts::Json rollback_operations = contracts::Json::array();
  contracts::Json applied = contracts::Json::array();
  for (const auto &operation : operations) {
    if (!operation.is_object()) {
      simulation_error("migration operation must be an object");
    }
    const auto kind = required_string(operation, "operation");
    const auto key = required_string(operation, "key");
    contracts::Json inverse;
    if (kind == "add") {
      if (state.contains(key)) {
        simulation_error("migration add target already exists: " + key);
      }
      state[key] = operation.value("value", contracts::Json(nullptr));
      inverse = {{"operation", "remove"}, {"key", key}};
    } else if (kind == "remove") {
      if (!state.contains(key)) {
        simulation_error("migration remove target does not exist: " + key);
      }
      const auto previous = state.at(key);
      state.erase(key);
      inverse = {{"operation", "add"}, {"key", key}, {"value", previous}};
    } else if (kind == "set") {
      if (state.contains(key)) {
        inverse = {{"operation", "set"},
                   {"key", key},
                   {"value", state.at(key)}};
      } else {
        inverse = {{"operation", "remove"}, {"key", key}};
      }
      state[key] = operation.value("value", contracts::Json(nullptr));
    } else if (kind == "rename") {
      const auto target = required_string(operation, "target");
      if (!state.contains(key) || state.contains(target)) {
        simulation_error(
            "migration rename requires existing source and unused target");
      }
      state[target] = state.at(key);
      state.erase(key);
      inverse = {{"operation", "rename"}, {"key", target}, {"target", key}};
    } else {
      simulation_error("unsupported migration operation: " + kind);
    }
    rollback_operations.insert(rollback_operations.begin(), std::move(inverse));
    applied.push_back(operation);
  }

  std::set<std::string> keys;
  for (const auto &[key, value] : before.items()) {
    static_cast<void>(value);
    keys.insert(key);
  }
  for (const auto &[key, value] : state.items()) {
    static_cast<void>(value);
    keys.insert(key);
  }
  contracts::Json diff = contracts::Json::object();
  for (const auto &key : keys) {
    const bool before_contains = before.contains(key);
    const bool after_contains = state.contains(key);
    const contracts::Json before_value =
        before_contains ? before.at(key) : contracts::Json(nullptr);
    const contracts::Json after_value =
        after_contains ? state.at(key) : contracts::Json(nullptr);
    if (before_contains != after_contains || before_value != after_value) {
      diff[key] = {{"before", before_value}, {"after", after_value}};
    }
  }
  return {{"simulated", true},
          {"before", before},
          {"after", state},
          {"operations", applied},
          {"rollback_operations", rollback_operations},
          {"diff", diff},
          {"fidelity_limits",
           {"dictionary-state migration model", "no external side effects"}}};
}

contracts::Json
SimulationEngine::worktree(const std::filesystem::path &root,
                           const contracts::Json &changes) const {
  if (!std::filesystem::is_directory(root) || !changes.is_array()) {
    simulation_error("worktree requires a source directory and change array");
  }
  const auto source = std::filesystem::canonical(root);
  const auto before_hash = tree_hash(source);
  TemporaryDirectory temporary;
  const auto clone = temporary.path() / "repo";
  copy_tree(source, clone);

  contracts::Json applied = contracts::Json::array();
  std::set<std::string> changed_paths;
  for (const auto &raw : changes) {
    if (!raw.is_object()) {
      simulation_error("worktree change must be an object");
    }
    const auto type_iterator = raw.find("type");
    const auto operation_iterator = raw.find("operation");
    const auto &kind_value = type_iterator != raw.end() ? *type_iterator
                                                        : *operation_iterator;
    if ((type_iterator == raw.end() && operation_iterator == raw.end()) ||
        !kind_value.is_string()) {
      simulation_error("worktree change requires an operation type");
    }
    const auto operation = kind_value.get<std::string>();
    const auto relative = safe_relative_path(raw.at("path"));
    const auto target = clone / relative;
    std::filesystem::create_directories(target.parent_path());
    if (operation == "write") {
      const auto content = raw.value("content", std::string{});
      core::atomic_write_text(target, content);
    } else if (operation == "replace") {
      if (!std::filesystem::is_regular_file(target)) {
        simulation_error("worktree replace target does not exist: " +
                         relative.generic_string());
      }
      const auto old_text = raw.value("old", std::string{});
      const auto new_text = raw.value("new", std::string{});
      const int count = raw.value("count", 1);
      core::atomic_write_text(
          target, replace_text(core::read_text(target), old_text, new_text,
                               count));
    } else {
      simulation_error("unsupported worktree simulation operation: " +
                       operation);
    }
    auto normalized = raw;
    normalized["path"] = relative.generic_string();
    normalized["type"] = operation;
    applied.push_back(std::move(normalized));
    changed_paths.insert(relative.generic_string());
  }
  const auto after_hash = tree_hash(clone);
  if (tree_hash(source) != before_hash) {
    simulation_error("source tree changed during disposable simulation");
  }
  return {{"simulated", true},
          {"source_tree_hash", before_hash},
          {"simulated_tree_hash", after_hash},
          {"changed", before_hash != after_hash},
          {"changed_paths", changed_paths},
          {"operations", applied},
          {"disposed", true},
          {"fidelity_limits",
           {"filesystem-only disposable copy",
            "no repository-native commands executed",
            "symlink semantics are not preserved"}}};
}

contracts::Json
SimulationEngine::rollback(const contracts::Json &simulation) const {
  if (!simulation.is_object() || !simulation.contains("before") ||
      !simulation.contains("after") ||
      !simulation.contains("rollback_operations")) {
    simulation_error("rollback requires a migration simulation receipt");
  }
  const auto result = migration(simulation.at("after"),
                                simulation.at("rollback_operations"));
  return {{"simulated", true},
          {"restored", result.at("after") == simulation.at("before")},
          {"expected", simulation.at("before")},
          {"observed", result.at("after")}};
}

} // namespace statewright::egcf
