#include "statewright/egcf/assurance.hpp"

#include "ledger_support.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace statewright::egcf {
namespace {

[[noreturn]] void assurance_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "EGCF assurance: " + std::move(message));
}

[[nodiscard]] std::vector<std::string>
canonical_strings(std::vector<std::string> values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

} // namespace

std::string AssuranceCase::object_id() const {
  return contracts::typed_id("assurance-case", to_json(*this));
}

contracts::Json to_json(const AssuranceCase &value) {
  return {{"approval_facts", value.approval_facts},
          {"arguments", value.arguments},
          {"capability_facts", value.capability_facts},
          {"conclusion", value.conclusion},
          {"conflicts", value.conflicts},
          {"created_at", value.created_at},
          {"decision_ids", value.decision_ids},
          {"gaps", value.gaps},
          {"invariant_ids", value.invariant_ids},
          {"refuting_evidence", value.refuting_evidence},
          {"rollback_argument", value.rollback_argument},
          {"subclaims", value.subclaims},
          {"subject_id", value.subject_id},
          {"supporting_evidence", value.supporting_evidence},
          {"top_claim", value.top_claim},
          {"uncertainties", value.uncertainties}};
}

AssuranceManager::AssuranceManager(EgcfStore &store, EvidenceManager &evidence,
                                   InvariantManager &invariants,
                                   DecisionManager &decisions)
    : store_(store), evidence_(evidence), invariants_(invariants),
      decisions_(decisions) {}

AssuranceCase AssuranceManager::generate(
    std::string subject_id, std::string top_claim,
    contracts::Json capability_facts, contracts::Json approval_facts,
    contracts::Json rollback_argument,
    std::vector<std::string> uncertainties) {
  if (subject_id.empty() || top_claim.empty() ||
      !capability_facts.is_object() || !approval_facts.is_object() ||
      !rollback_argument.is_object()) {
    assurance_error("assurance inputs are invalid");
  }
  const auto artifacts = evidence_.artifacts(subject_id);
  const auto confidence = evidence_.confidence(subject_id);
  const auto evidence_conflicts = evidence_.conflicts(subject_id);
  const auto invariant_conflicts = invariants_.conflicts();
  const auto decision_conflicts = decisions_.conflicts();
  const auto active_invariants = invariants_.records(true);
  const auto active_decisions = decisions_.records(true);
  std::vector<std::string> supporting;
  std::vector<std::string> refuting;
  for (const auto &artifact : artifacts) {
    if (artifact.success && !*artifact.success) {
      refuting.push_back(artifact.object_id());
    } else {
      supporting.push_back(artifact.object_id());
    }
  }
  auto gaps = confidence.blocking_gaps;
  std::vector<std::string> conflicts;
  for (const auto *collection :
       {&evidence_conflicts, &invariant_conflicts, &decision_conflicts}) {
    for (const auto &conflict : *collection) {
      conflicts.push_back(conflict.at("reason").get<std::string>());
    }
  }
  const bool approval_satisfied = approval_facts.value("satisfied", false);
  const bool rollback_required = rollback_argument.value("required", false);
  const bool rollback_covered = rollback_argument.value("covered", false);
  if (!approval_satisfied) {
    gaps.push_back("approval not satisfied");
  }
  if (rollback_required && !rollback_covered) {
    gaps.push_back("rollback coverage missing");
  }
  if (uncertainties.empty()) {
    uncertainties = confidence.known_unknowns;
  }
  std::vector<std::string> invariant_ids;
  for (const auto &record : active_invariants) {
    invariant_ids.push_back(record.object_id());
  }
  std::vector<std::string> decision_ids;
  for (const auto &record : active_decisions) {
    decision_ids.push_back(record.object_id());
  }
  AssuranceCase result = {
      .subject_id = std::move(subject_id),
      .top_claim = std::move(top_claim),
      .subclaims =
          {{{"claim", "capability requirements are authorized"},
            {"status", !capability_facts.empty()}},
           {{"claim", "evidence requirements are covered"},
            {"status", confidence.blocking_gaps.empty()}},
           {{"claim", "approval requirements are satisfied"},
            {"status", approval_satisfied}},
           {{"claim", "rollback requirements are covered"},
            {"status", !rollback_required || rollback_covered}}},
      .arguments = {{{"assessment_id", confidence.object_id()},
                     {"conclusion", confidence.conclusion},
                     {"kind", "confidence"}}},
      .supporting_evidence = canonical_strings(std::move(supporting)),
      .refuting_evidence = canonical_strings(std::move(refuting)),
      .invariant_ids = canonical_strings(std::move(invariant_ids)),
      .decision_ids = canonical_strings(std::move(decision_ids)),
      .capability_facts = std::move(capability_facts),
      .approval_facts = std::move(approval_facts),
      .rollback_argument = std::move(rollback_argument),
      .gaps = canonical_strings(std::move(gaps)),
      .conflicts = canonical_strings(std::move(conflicts)),
      .uncertainties = canonical_strings(std::move(uncertainties)),
      .conclusion = {},
      .created_at = ledger_support::utc_now()};
  result.conclusion = !result.supporting_evidence.empty() &&
                              result.refuting_evidence.empty() &&
                              result.gaps.empty() && result.conflicts.empty()
                          ? "SUPPORTED"
                          : "NOT_SUPPORTED";
  const auto result_id = store_.register_record(
      {.object_type = "assurance-case", .payload = to_json(result)},
      "egcf_assurance_generated");
  if (result_id != result.object_id()) {
    assurance_error("assurance identity mismatch");
  }
  return result;
}

} // namespace statewright::egcf
