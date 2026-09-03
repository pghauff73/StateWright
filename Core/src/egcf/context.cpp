#include "statewright/egcf/context.hpp"

#include "statewright/common/error.hpp"

#include <fnmatch.h>

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void context_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "EGCF command context: " + std::move(message));
}

[[nodiscard]] std::string normalize_scope(std::string_view value) {
  std::string result(value);
  std::ranges::replace(result, '\\', '/');
  while (!result.empty() && result.front() == ' ') {
    result.erase(result.begin());
  }
  while (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }
  return result;
}

template <typename Value>
[[nodiscard]] std::optional<Value>
narrow_value(const std::optional<Value> &parent,
             const std::optional<Value> &child) {
  if (!parent) {
    return child;
  }
  if (!child) {
    return parent;
  }
  return std::min(*parent, *child);
}

[[nodiscard]] std::string maximum_by_order(
    std::string_view left, std::string_view right,
    const std::map<std::string, int> &order, std::string_view label) {
  const auto left_item = order.find(std::string(left));
  const auto right_item = order.find(std::string(right));
  if (left_item == order.end() || right_item == order.end()) {
    context_error("invalid " + std::string(label) + ": " +
                  std::string(left) + " or " + std::string(right));
  }
  return left_item->second >= right_item->second ? left_item->first
                                                  : right_item->first;
}

[[nodiscard]] std::vector<std::string>
unique_union(const std::vector<std::string> &left,
             const std::vector<std::string> &right) {
  std::vector<std::string> result;
  std::set<std::string> seen;
  for (const auto *values : {&left, &right}) {
    for (const auto &item : *values) {
      if (seen.insert(item).second) {
        result.push_back(item);
      }
    }
  }
  return result;
}

template <typename Value>
void assign_optional(const contracts::Json &object, std::string_view key,
                     std::optional<Value> &target) {
  const auto found = object.find(key);
  if (found != object.end() && !found->is_null()) {
    target = found->get<Value>();
  }
}

} // namespace

Budget Budget::narrow(const Budget &child) const {
  return {.tokens = narrow_value(tokens, child.tokens),
          .wall_seconds = narrow_value(wall_seconds, child.wall_seconds),
          .actions = narrow_value(actions, child.actions),
          .subprocesses = narrow_value(subprocesses, child.subprocesses),
          .writes = narrow_value(writes, child.writes),
          .write_bytes = narrow_value(write_bytes, child.write_bytes),
          .network_requests =
              narrow_value(network_requests, child.network_requests),
          .network_bytes = narrow_value(network_bytes, child.network_bytes),
          .cost_micros = narrow_value(cost_micros, child.cost_micros),
          .retries = narrow_value(retries, child.retries)};
}

void Budget::validate() const {
  const auto require_nonnegative = [](const auto &value,
                                      std::string_view name) {
    if (value && *value < 0) {
      context_error("budget " + std::string(name) + " cannot be negative");
    }
  };
  require_nonnegative(tokens, "tokens");
  require_nonnegative(wall_seconds, "wall_seconds");
  require_nonnegative(actions, "actions");
  require_nonnegative(subprocesses, "subprocesses");
  require_nonnegative(writes, "writes");
  require_nonnegative(write_bytes, "write_bytes");
  require_nonnegative(network_requests, "network_requests");
  require_nonnegative(network_bytes, "network_bytes");
  require_nonnegative(cost_micros, "cost_micros");
  require_nonnegative(retries, "retries");
}

bool scope_contains(std::string_view parent, std::string_view child) {
  const std::string normalized_parent = normalize_scope(parent);
  const std::string normalized_child = normalize_scope(child);
  if (normalized_parent == "*" || normalized_parent == "**" ||
      normalized_parent == ".") {
    return true;
  }
  if (normalized_parent == normalized_child) {
    return true;
  }
  if (normalized_parent.ends_with("/**")) {
    std::string prefix =
        normalized_parent.substr(0, normalized_parent.size() - 3U);
    while (!prefix.empty() && prefix.back() == '/') {
      prefix.pop_back();
    }
    return normalized_child == prefix ||
           normalized_child.starts_with(prefix + "/");
  }
  if (normalized_child.find_first_of("*?[") != std::string::npos) {
    return false;
  }
  return ::fnmatch(normalized_parent.c_str(), normalized_child.c_str(),
                   FNM_PATHNAME) == 0;
}

std::vector<std::string>
narrow_scope(const std::vector<std::string> &parent,
             const std::vector<std::string> &child) {
  std::vector<std::string> parent_scope;
  std::set<std::string> parent_seen;
  for (const auto &item : parent) {
    if (parent_seen.insert(item).second) {
      parent_scope.push_back(item);
    }
  }
  if (child.empty()) {
    return parent_scope;
  }
  std::vector<std::string> child_scope;
  std::vector<std::string> uncovered;
  std::set<std::string> child_seen;
  for (const auto &item : child) {
    if (!child_seen.insert(item).second) {
      continue;
    }
    child_scope.push_back(item);
    if (std::ranges::none_of(parent_scope, [&](const std::string &container) {
          return scope_contains(container, item);
        })) {
      uncovered.push_back(item);
    }
  }
  if (!uncovered.empty()) {
    context_error("child scope broadens parent scope: " +
                  contracts::canonical_json(uncovered));
  }
  return child_scope;
}

CommandContext CommandContext::inherit(const CommandContext &child) const {
  static const std::map<std::string, int> approval_order = {
      {"automatic", 0}, {"policy", 1}, {"human", 2}, {"quorum", 3}};
  static const std::map<std::string, int> risk_order = {
      {"L0", 0}, {"L1", 1}, {"L2", 2}};
  static const std::map<std::string, int> rollback_order = {
      {"none", 0}, {"best_effort", 1}, {"compensating", 2}, {"exact", 3}};
  const std::optional<double> effective_timeout =
      !timeout ? child.timeout
               : !child.timeout
                     ? timeout
                     : std::optional<double>(std::min(*timeout, *child.timeout));
  CommandContext result = {
      .dry_run = dry_run || child.dry_run,
      .why = why || child.why,
      .scope = narrow_scope(scope, child.scope),
      .evidence = unique_union(evidence, child.evidence),
      .approval = maximum_by_order(approval, child.approval, approval_order,
                                   "approval"),
      .risk = maximum_by_order(risk, child.risk, risk_order, "risk"),
      .rollback = maximum_by_order(rollback, child.rollback, rollback_order,
                                   "rollback"),
      .budget = budget.narrow(child.budget),
      .timeout = effective_timeout,
      .trace = trace || child.trace,
      .json_output = json_output || child.json_output,
      .graph = graph || child.graph,
      .record = record || child.record,
      .replay = child.replay.empty() ? replay : child.replay,
      .strict = strict || child.strict,
      .simulate = simulate || child.simulate};
  result.validate();
  return result;
}

void CommandContext::validate() const {
  static const std::set<std::string> approvals = {"automatic", "policy",
                                                   "human", "quorum"};
  static const std::set<std::string> risks = {"L0", "L1", "L2"};
  static const std::set<std::string> rollbacks = {
      "none", "best_effort", "compensating", "exact"};
  if (!approvals.contains(approval)) {
    context_error("invalid approval: " + approval);
  }
  if (!risks.contains(risk)) {
    context_error("invalid risk: " + risk);
  }
  if (!rollbacks.contains(rollback)) {
    context_error("invalid rollback: " + rollback);
  }
  if (timeout && *timeout <= 0.0) {
    context_error("timeout must be greater than zero");
  }
  budget.validate();
}

Budget budget_from_json(const contracts::Json &value) {
  if (value.is_null()) {
    return {};
  }
  if (!value.is_object()) {
    context_error("budget must be an object");
  }
  static const std::set<std::string> fields = {
      "tokens",        "wall_seconds",    "actions", "subprocesses",
      "writes",        "write_bytes",     "network_requests",
      "network_bytes", "cost_micros",     "retries"};
  for (const auto &[key, unused] : value.items()) {
    static_cast<void>(unused);
    if (!fields.contains(key)) {
      context_error("invalid budget item: " + key);
    }
  }
  Budget result;
  assign_optional(value, "tokens", result.tokens);
  assign_optional(value, "wall_seconds", result.wall_seconds);
  assign_optional(value, "actions", result.actions);
  assign_optional(value, "subprocesses", result.subprocesses);
  assign_optional(value, "writes", result.writes);
  assign_optional(value, "write_bytes", result.write_bytes);
  assign_optional(value, "network_requests", result.network_requests);
  assign_optional(value, "network_bytes", result.network_bytes);
  assign_optional(value, "cost_micros", result.cost_micros);
  assign_optional(value, "retries", result.retries);
  result.validate();
  return result;
}

CommandContext command_context_from_json(const contracts::Json &value) {
  if (value.is_null()) {
    return {};
  }
  if (!value.is_object()) {
    context_error("context must be an object");
  }
  static const std::set<std::string> fields = {
      "dry_run", "why",         "scope",  "evidence", "approval",
      "risk",    "rollback",    "budget", "timeout",  "trace",
      "json",    "json_output", "graph",  "record",   "replay",
      "strict",  "simulate"};
  for (const auto &[key, unused] : value.items()) {
    static_cast<void>(unused);
    if (!fields.contains(key)) {
      context_error("unknown context field: " + key);
    }
  }
  CommandContext result;
  result.dry_run = value.value("dry_run", false);
  result.why = value.value("why", false);
  result.scope = value.value("scope", std::vector<std::string>{"**"});
  result.evidence = value.value("evidence", std::vector<std::string>{});
  result.approval = value.value("approval", "automatic");
  result.risk = value.value("risk", "L0");
  result.rollback = value.value("rollback", "none");
  result.budget = budget_from_json(value.value("budget", contracts::Json()));
  assign_optional(value, "timeout", result.timeout);
  result.trace = value.value("trace", false);
  result.json_output =
      value.contains("json_output") ? value.at("json_output").get<bool>()
                                    : value.value("json", false);
  result.graph = value.value("graph", false);
  result.record = value.value("record", false);
  result.replay = value.value("replay", "");
  result.strict = value.value("strict", false);
  result.simulate = value.value("simulate", false);
  result.validate();
  return result;
}

contracts::Json to_json(const Budget &budget) {
  return {{"actions", budget.actions},
          {"cost_micros", budget.cost_micros},
          {"network_bytes", budget.network_bytes},
          {"network_requests", budget.network_requests},
          {"retries", budget.retries},
          {"subprocesses", budget.subprocesses},
          {"tokens", budget.tokens},
          {"wall_seconds", budget.wall_seconds},
          {"write_bytes", budget.write_bytes},
          {"writes", budget.writes}};
}

contracts::Json to_json(const CommandContext &context) {
  return {{"approval", context.approval},
          {"budget", to_json(context.budget)},
          {"dry_run", context.dry_run},
          {"evidence", context.evidence},
          {"graph", context.graph},
          {"json_output", context.json_output},
          {"record", context.record},
          {"replay", context.replay},
          {"risk", context.risk},
          {"rollback", context.rollback},
          {"scope", context.scope},
          {"simulate", context.simulate},
          {"strict", context.strict},
          {"timeout", context.timeout},
          {"trace", context.trace},
          {"why", context.why}};
}

} // namespace statewright::egcf
