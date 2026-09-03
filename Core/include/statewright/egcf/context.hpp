#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

struct Budget final {
  std::optional<std::int64_t> tokens;
  std::optional<double> wall_seconds;
  std::optional<std::int64_t> actions;
  std::optional<std::int64_t> subprocesses;
  std::optional<std::int64_t> writes;
  std::optional<std::int64_t> write_bytes;
  std::optional<std::int64_t> network_requests;
  std::optional<std::int64_t> network_bytes;
  std::optional<std::int64_t> cost_micros;
  std::optional<std::int64_t> retries;

  [[nodiscard]] Budget narrow(const Budget &child) const;
  void validate() const;
};

struct CommandContext final {
  bool dry_run = false;
  bool why = false;
  std::vector<std::string> scope{"**"};
  std::vector<std::string> evidence;
  std::string approval = "automatic";
  std::string risk = "L0";
  std::string rollback = "none";
  Budget budget;
  std::optional<double> timeout;
  bool trace = false;
  bool json_output = false;
  bool graph = false;
  bool record = false;
  std::string replay;
  bool strict = false;
  bool simulate = false;

  [[nodiscard]] CommandContext inherit(const CommandContext &child) const;
  void validate() const;
};

[[nodiscard]] bool scope_contains(std::string_view parent,
                                  std::string_view child);
[[nodiscard]] std::vector<std::string>
narrow_scope(const std::vector<std::string> &parent,
             const std::vector<std::string> &child);
[[nodiscard]] Budget budget_from_json(const contracts::Json &value);
[[nodiscard]] CommandContext
command_context_from_json(const contracts::Json &value);
[[nodiscard]] contracts::Json to_json(const Budget &budget);
[[nodiscard]] contracts::Json to_json(const CommandContext &context);

} // namespace statewright::egcf
