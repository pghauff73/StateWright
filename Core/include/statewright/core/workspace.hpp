#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::core {

class Workspace final {
public:
  explicit Workspace(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] std::string canonical(std::string_view relative,
                                      bool allow_internal = false) const;
  [[nodiscard]] std::filesystem::path resolve(
      std::string_view relative, bool allow_internal = false) const;
  [[nodiscard]] std::string relative(
      const std::filesystem::path &path, bool allow_internal = false) const;

  [[nodiscard]] static bool matches(
      std::string_view path, const std::vector<std::string> &patterns);
  [[nodiscard]] std::string require_scope(
      std::string_view relative, const std::vector<std::string> &allowed_patterns,
      const std::vector<std::string> &forbidden_patterns = {}) const;

  [[nodiscard]] std::vector<std::filesystem::path>
  files(std::string_view relative = ".") const;
  [[nodiscard]] std::optional<std::string>
  file_hash(std::string_view relative) const;
  [[nodiscard]] std::map<std::string, std::string> snapshot() const;
  [[nodiscard]] std::string snapshot_hash() const;

private:
  [[nodiscard]] static bool ignored_component(std::string_view component);
  [[nodiscard]] static bool is_within(const std::filesystem::path &path,
                                      const std::filesystem::path &root);
  [[nodiscard]] static std::string file_mode(const std::filesystem::path &path);

  std::filesystem::path root_;
};

} // namespace statewright::core

