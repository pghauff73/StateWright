#include "statewright/egcf/lifecycle.hpp"

#include "statewright/common/error.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>

namespace statewright::egcf {
namespace {

const std::map<std::string, std::set<std::string>> transitions = {
    {"DISCOVERED", {"INTERPRETED", "REFUSED"}},
    {"INTERPRETED", {"MODELLED", "REFUSED"}},
    {"MODELLED", {"RESOLVED", "REFUSED"}},
    {"RESOLVED", {"QUALIFIED", "REFUSED"}},
    {"QUALIFIED", {"COMPILED", "REFUSED"}},
    {"COMPILED",
     {"SIMULATED", "AWAITING_APPROVAL", "AUTHORIZED", "COMPLETED",
      "REFUSED"}},
    {"SIMULATED", {"AWAITING_APPROVAL", "COMPLETED", "FAILED", "REFUSED"}},
    {"AWAITING_APPROVAL", {"AUTHORIZED", "REFUSED"}},
    {"AUTHORIZED", {"EXECUTING", "REFUSED"}},
    {"EXECUTING",
     {"VERIFYING", "FAILED", "ROLLED_BACK", "PARTIALLY_COMPENSATED"}},
    {"VERIFYING",
     {"COMPLETED", "FAILED", "ROLLED_BACK", "PARTIALLY_COMPENSATED"}},
};

const std::set<std::string> terminal_states = {
    "COMPLETED", "FAILED", "PARTIALLY_COMPENSATED", "REFUSED",
    "ROLLED_BACK", "SUPERSEDED"};

[[noreturn]] void lifecycle_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

} // namespace

const std::vector<std::string> &canonical_lifecycle_stages() {
  static const std::vector<std::string> stages = {
      "DISCOVERED",       "INTERPRETED", "MODELLED",  "RESOLVED",
      "QUALIFIED",        "COMPILED",    "SIMULATED", "AWAITING_APPROVAL",
      "AUTHORIZED",       "EXECUTING",   "VERIFYING", "COMPLETED"};
  return stages;
}

bool is_terminal_lifecycle_state(std::string_view state) {
  return terminal_states.contains(std::string(state));
}

Lifecycle::Lifecycle(std::string initial) : state_(std::move(initial)) {
  if (!transitions.contains(state_) && !is_terminal_lifecycle_state(state_)) {
    lifecycle_error("unknown EGCF lifecycle state: " + state_);
  }
  history_.push_back(state_);
}

const std::string &Lifecycle::state() const noexcept { return state_; }

const std::vector<std::string> &Lifecycle::history() const noexcept {
  return history_;
}

std::string Lifecycle::transition(std::string target) {
  const auto iterator = transitions.find(state_);
  if (iterator == transitions.end() || !iterator->second.contains(target)) {
    lifecycle_error("illegal EGCF lifecycle transition: " + state_ + " -> " +
                    target);
  }
  state_ = std::move(target);
  history_.push_back(state_);
  return state_;
}

std::vector<std::string>
Lifecycle::compress(const std::vector<std::string> &stages) {
  for (const auto &stage : stages) {
    static_cast<void>(transition(stage));
  }
  return history_;
}

contracts::Json Lifecycle::projection() const {
  const std::set<std::string> visited(history_.begin(), history_.end());
  const bool terminal = is_terminal_lifecycle_state(state_);
  contracts::Json result = contracts::Json::array();
  for (const auto &stage : canonical_lifecycle_stages()) {
    std::string status;
    std::string reason;
    if (visited.contains(stage)) {
      status = stage != state_ || terminal ? "completed" : "current";
    } else if (terminal) {
      status = "not_required";
      reason = "not traversed for terminal outcome " + state_;
    } else {
      status = "blocked";
      reason = "awaiting completion of " + state_;
    }
    result.push_back(
        {{"reason", std::move(reason)}, {"stage", stage}, {"status", status}});
  }
  for (const auto &state : history_) {
    const auto &canonical = canonical_lifecycle_stages();
    if (std::find(canonical.begin(), canonical.end(), state) != canonical.end()) {
      continue;
    }
    result.push_back(
        {{"reason", "terminal or control state"},
         {"stage", state},
         {"status", terminal ? "completed" : "current"}});
  }
  return result;
}

} // namespace statewright::egcf
