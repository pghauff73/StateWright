#include "statewright/egcf/governance.hpp"

#include "ledger_support.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void governance_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied,
                      "EGCF governance: " + std::move(message));
}

[[nodiscard]] std::vector<std::string>
stable_unique(std::vector<std::string> values) {
  std::set<std::string> seen;
  std::vector<std::string> result;
  for (auto &value : values) {
    if (seen.insert(value).second) {
      result.push_back(std::move(value));
    }
  }
  return result;
}

[[nodiscard]] std::string normalized(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const auto character : value) {
    lowered.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(character))));
  }
  while (!lowered.empty() &&
         (std::isspace(static_cast<unsigned char>(lowered.back())) != 0 ||
          lowered.back() == '.')) {
    lowered.pop_back();
  }
  std::istringstream input(lowered);
  std::string word;
  std::string result;
  while (input >> word) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += word;
  }
  return result;
}

[[nodiscard]] bool scope_overlap(const std::vector<std::string> &left,
                                 const std::vector<std::string> &right) {
  if (std::ranges::find(left, "**") != left.end() ||
      std::ranges::find(right, "**") != right.end()) {
    return true;
  }
  return std::ranges::any_of(left, [&right](const auto &value) {
    return std::ranges::find(right, value) != right.end();
  });
}

[[nodiscard]] bool opposite(std::string_view left, std::string_view right) {
  const auto normalized_left = normalized(left);
  const auto normalized_right = normalized(right);
  return normalized_left == "not " + normalized_right ||
         normalized_right == "not " + normalized_left;
}

} // namespace

std::string InvariantRecord::object_id() const {
  return contracts::typed_id("invariant", to_json(*this));
}

std::string DecisionRecord::object_id() const {
  return contracts::typed_id("decision", to_json(*this));
}

Json to_json(const InvariantRecord &record) {
  return {{"authority", record.authority},
          {"counterexamples", record.counterexamples},
          {"created_at", record.created_at},
          {"evidence_ids", record.evidence_ids},
          {"falsifier", record.falsifier},
          {"name", record.name},
          {"scope", record.scope},
          {"statement", record.statement},
          {"status", record.status},
          {"supersedes", record.supersedes},
          {"validator", record.validator}};
}

Json to_json(const DecisionRecord &record) {
  return {{"alternatives", record.alternatives},
          {"choice", record.choice},
          {"constraints", record.constraints},
          {"created_at", record.created_at},
          {"evidence_ids", record.evidence_ids},
          {"owner", record.owner},
          {"question", record.question},
          {"rationale", record.rationale},
          {"scope", record.scope},
          {"status", record.status},
          {"supersedes", record.supersedes}};
}

InvariantRecord invariant_from_json(const Json &value) {
  try {
    return {.name = value.at("name"),
            .statement = value.at("statement"),
            .scope = value.at("scope"),
            .status = value.at("status"),
            .validator = value.at("validator"),
            .evidence_ids = value.at("evidence_ids"),
            .falsifier = value.at("falsifier"),
            .counterexamples = value.at("counterexamples"),
            .authority = value.at("authority"),
            .created_at = value.at("created_at"),
            .supersedes = value.at("supersedes")};
  } catch (const std::exception &exception) {
    governance_error(std::string("invalid invariant: ") + exception.what());
  }
}

DecisionRecord decision_from_json(const Json &value) {
  try {
    return {.question = value.at("question"),
            .alternatives = value.at("alternatives"),
            .choice = value.at("choice"),
            .rationale = value.at("rationale"),
            .evidence_ids = value.at("evidence_ids"),
            .constraints = value.at("constraints"),
            .owner = value.at("owner"),
            .scope = value.at("scope"),
            .status = value.at("status"),
            .created_at = value.at("created_at"),
            .supersedes = value.at("supersedes")};
  } catch (const std::exception &exception) {
    governance_error(std::string("invalid decision: ") + exception.what());
  }
}

InvariantManager::InvariantManager(EgcfStore &store) : store_(store) {}

std::vector<std::string>
InvariantManager::discover(std::vector<std::string> statements,
                           std::vector<std::string> scope,
                           std::string source) {
  std::vector<std::string> identifiers;
  std::set<std::string> seen;
  for (auto &statement : statements) {
    const auto canonical = normalized(statement);
    if (canonical.empty() || !seen.insert(canonical).second) {
      continue;
    }
    std::istringstream words(canonical);
    std::string word;
    std::string name;
    int count = 0;
    while (count < 8 && words >> word) {
      if (!name.empty()) {
        name.push_back('-');
      }
      name += word;
      ++count;
    }
    InvariantRecord record = {
        .name = name.empty() ? "unnamed" : name,
        .statement = statement,
        .scope = scope,
        .status = "DISCOVERED_CANDIDATE",
        .validator = {{"kind", "unvalidated"}, {"source", source}},
        .evidence_ids = {},
        .falsifier = "Find a case in scope where: not (" + statement + ")",
        .counterexamples = {},
        .authority = "proposal-only",
        .created_at = ledger_support::utc_now(),
        .supersedes = {}};
    identifiers.push_back(store_.register_record(
        {.object_type = "invariant", .payload = to_json(record)},
        "egcf_invariant_discovered"));
  }
  return identifiers;
}

std::string InvariantManager::register_invariant(
    std::string name, std::string statement, std::vector<std::string> scope,
    Json validator, std::vector<std::string> evidence_ids,
    std::string falsifier, std::string authority,
    std::vector<std::string> counterexamples) {
  evidence_ids = stable_unique(std::move(evidence_ids));
  if (name.empty() || statement.empty() || scope.empty() || authority.empty() ||
      validator.empty() || evidence_ids.empty() || falsifier.empty()) {
    governance_error(
        "registered invariant requires identity, scope, authority, validator, evidence, and falsifier");
  }
  const InvariantRecord record = {
      .name = std::move(name),
      .statement = std::move(statement),
      .scope = std::move(scope),
      .status = "REGISTERED",
      .validator = std::move(validator),
      .evidence_ids = std::move(evidence_ids),
      .falsifier = std::move(falsifier),
      .counterexamples = stable_unique(std::move(counterexamples)),
      .authority = std::move(authority),
      .created_at = ledger_support::utc_now(),
      .supersedes = {}};
  return store_.register_record(
      {.object_type = "invariant", .payload = to_json(record)},
      "egcf_invariant_registered");
}

std::vector<InvariantRecord> InvariantManager::records(bool active_only) {
  std::set<std::string> active;
  if (active_only) {
    const auto ids = store_.active_ids("invariant");
    active.insert(ids.begin(), ids.end());
  }
  std::vector<InvariantRecord> result;
  for (const auto &stored : store_.list("invariant")) {
    auto record = invariant_from_json(stored.payload);
    if (!active_only ||
        (active.contains(stored.object_id) && record.status == "REGISTERED")) {
      result.push_back(std::move(record));
    }
  }
  return result;
}

std::string InvariantManager::validate(
    std::string_view invariant_id, bool success,
    std::vector<std::string> evidence_ids) {
  const auto stored = store_.get(invariant_id);
  if (stored.object_type != "invariant") {
    governance_error("not an invariant: " + std::string(invariant_id));
  }
  auto record = invariant_from_json(stored.payload);
  record.status = success ? "VALIDATED" : "CONFLICTED";
  record.evidence_ids.insert(record.evidence_ids.end(), evidence_ids.begin(),
                             evidence_ids.end());
  record.evidence_ids = stable_unique(std::move(record.evidence_ids));
  record.created_at = ledger_support::utc_now();
  record.supersedes = std::string(invariant_id);
  const auto new_id = store_.register_record(
      {.object_type = "invariant", .payload = to_json(record)},
      "egcf_invariant_validated");
  static_cast<void>(store_.supersede(std::string(invariant_id), new_id,
                                     "invariant validation result",
                                     "deterministic validator"));
  return new_id;
}

Json InvariantManager::conflicts() {
  const auto values = records(true);
  Json result = Json::array();
  for (std::size_t left_index = 0; left_index < values.size(); ++left_index) {
    for (std::size_t right_index = left_index + 1U;
         right_index < values.size(); ++right_index) {
      const auto &left = values.at(left_index);
      const auto &right = values.at(right_index);
      const bool same_name_different =
          left.name == right.name &&
          normalized(left.statement) != normalized(right.statement);
      if (scope_overlap(left.scope, right.scope) &&
          (same_name_different || opposite(left.statement, right.statement))) {
        result.push_back(
            {{"left", left.object_id()},
             {"reason",
              "active invariant statements conflict in overlapping scope"},
             {"right", right.object_id()}});
      }
    }
  }
  return result;
}

std::string InvariantManager::supersede(std::string_view old_id,
                                        InvariantRecord replacement,
                                        std::string reason,
                                        std::string authority) {
  if (authority.empty()) {
    governance_error("invariant supersedence requires authority");
  }
  static_cast<void>(store_.get(old_id));
  replacement.supersedes = std::string(old_id);
  const auto new_id = store_.register_record(
      {.object_type = "invariant", .payload = to_json(replacement)},
      "egcf_invariant_superseded");
  static_cast<void>(store_.supersede(std::string(old_id), new_id,
                                     std::move(reason), std::move(authority)));
  return new_id;
}

DecisionManager::DecisionManager(EgcfStore &store) : store_(store) {}

std::string DecisionManager::create(
    std::string question, std::vector<std::string> alternatives,
    std::string choice, std::string rationale,
    std::vector<std::string> evidence_ids,
    std::vector<std::string> constraints, std::string owner,
    std::vector<std::string> scope, bool activate, std::string authority) {
  static_cast<void>(authority);
  if (activate) {
    governance_error(
        "direct decision activation is forbidden; use approved supersedence");
  }
  if (question.empty() || alternatives.empty() || choice.empty() ||
      rationale.empty() || owner.empty() || scope.empty()) {
    governance_error("decision fields are incomplete");
  }
  const DecisionRecord record = {
      .question = std::move(question),
      .alternatives = stable_unique(std::move(alternatives)),
      .choice = std::move(choice),
      .rationale = std::move(rationale),
      .evidence_ids = stable_unique(std::move(evidence_ids)),
      .constraints = stable_unique(std::move(constraints)),
      .owner = std::move(owner),
      .scope = stable_unique(std::move(scope)),
      .status = "PROPOSED",
      .created_at = ledger_support::utc_now(),
      .supersedes = {}};
  return store_.register_record(
      {.object_type = "decision", .payload = to_json(record)},
      "egcf_decision_proposed");
}

std::vector<DecisionRecord> DecisionManager::records(bool active_only) {
  std::set<std::string> active;
  if (active_only) {
    const auto ids = store_.active_ids("decision");
    active.insert(ids.begin(), ids.end());
  }
  std::vector<DecisionRecord> result;
  for (const auto &stored : store_.list("decision")) {
    auto record = decision_from_json(stored.payload);
    if (!active_only ||
        (active.contains(stored.object_id) && record.status == "ACTIVE")) {
      result.push_back(std::move(record));
    }
  }
  return result;
}

Json DecisionManager::query(std::string_view text,
                            std::vector<std::string> scope) {
  std::string query_text(text);
  std::ranges::transform(query_text, query_text.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  Json result = Json::array();
  for (const auto &record : records(false)) {
    std::string haystack =
        record.question + " " + record.choice + " " + record.rationale;
    std::ranges::transform(haystack, haystack.begin(), [](unsigned char value) {
      return static_cast<char>(std::tolower(value));
    });
    if ((!query_text.empty() && haystack.find(query_text) == std::string::npos) ||
        (!scope.empty() && !scope_overlap(record.scope, scope))) {
      continue;
    }
    auto value = to_json(record);
    value["object_id"] = record.object_id();
    result.push_back(std::move(value));
  }
  return result;
}

Json DecisionManager::conflicts() {
  const auto values = records(true);
  Json result = Json::array();
  for (std::size_t left_index = 0; left_index < values.size(); ++left_index) {
    for (std::size_t right_index = left_index + 1U;
         right_index < values.size(); ++right_index) {
      const auto &left = values.at(left_index);
      const auto &right = values.at(right_index);
      if (normalized(left.question) == normalized(right.question) &&
          normalized(left.choice) != normalized(right.choice) &&
          scope_overlap(left.scope, right.scope)) {
        result.push_back(
            {{"left", left.object_id()},
             {"reason",
              "active decisions choose different alternatives for the same question"},
             {"right", right.object_id()}});
      }
    }
  }
  return result;
}

std::string DecisionManager::supersede(
    std::string_view old_id, std::string choice, std::string rationale,
    std::vector<std::string> evidence_ids, std::string authority) {
  if (authority.empty()) {
    governance_error("decision supersedence requires authority");
  }
  const auto stored = store_.get(old_id);
  if (stored.object_type != "decision") {
    governance_error("not a decision: " + std::string(old_id));
  }
  auto record = decision_from_json(stored.payload);
  record.choice = std::move(choice);
  record.rationale = std::move(rationale);
  record.evidence_ids = stable_unique(std::move(evidence_ids));
  record.status = "ACTIVE";
  record.created_at = ledger_support::utc_now();
  record.supersedes = std::string(old_id);
  const auto new_id = store_.register_record(
      {.object_type = "decision", .payload = to_json(record)},
      "egcf_decision_superseded");
  static_cast<void>(store_.supersede(std::string(old_id), new_id,
                                     "decision superseded",
                                     std::move(authority)));
  return new_id;
}

} // namespace statewright::egcf
