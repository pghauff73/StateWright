#include "statewright/saa/probation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using contracts::Json;

[[noreturn]] void probation_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool is_sha256(std::string_view value) {
  return value.size() == 64U &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return std::isdigit(character) != 0 ||
                  (character >= 'a' && character <= 'f');
         });
}

[[nodiscard]] std::vector<std::string>
canonical_strings(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] Json observation_material(const ProbationObservation &value) {
  return {{"baseline_ref", value.baseline_ref},
          {"baseline_correct", value.baseline_correct},
          {"benchmark_passed", value.benchmark_passed},
          {"candidate_correct", value.candidate_correct},
          {"candidate_ref", value.candidate_ref},
          {"candidate_selected", value.candidate_selected},
          {"context_signature", value.context_signature},
          {"evidence_ids", value.evidence_ids},
          {"integrity_passed", value.integrity_passed},
          {"invariant_passed", value.invariant_passed},
          {"observed_at", value.observed_at},
          {"plan_signature", value.plan_signature},
          {"query_signature", value.query_signature},
          {"regression_signals", to_json(value.regression_signals)},
          {"reproduction_passed", value.reproduction_passed},
          {"selection_explanation", value.selection_explanation},
          {"source_valid", value.source_valid},
          {"version", probation_version},
          {"window_index", value.window_index}};
}

[[nodiscard]] Json plan_material(const ProbationPlan &value) {
  return {{"automatic_demotion_predicates",
           value.automatic_demotion_predicates},
          {"candidate_ref", value.candidate_ref},
          {"maximum_canary_share_bp", value.maximum_canary_share_bp},
          {"minimum_observations", value.minimum_observations},
          {"minimum_uses", value.minimum_uses},
          {"policy_ref", value.policy_ref},
          {"policy_signature", value.policy_signature},
          {"previous_preferred_canonical_ref",
           value.previous_preferred_canonical_ref},
          {"promotion_assessment_ref", value.promotion_assessment_ref},
          {"version", probation_version},
          {"window_count", value.window_count}};
}

[[nodiscard]] Json assessment_material(const ProbationAssessment &value) {
  return {{"candidate_ref", value.candidate_ref},
          {"candidate_use_count", value.candidate_use_count},
          {"demotion_required", value.demotion_required},
          {"observation_count", value.observation_count},
          {"observation_signatures", value.observation_signatures},
          {"observed_window_count", value.observed_window_count},
          {"plan_signature", value.plan_signature},
          {"promotion_ready", value.promotion_ready},
          {"regression_reasons", value.regression_reasons},
          {"status", value.status},
          {"version", probation_version}};
}

[[nodiscard]] Json promotion_decision_material(
    const AutomaticPromotionDecision &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"candidate_ref", value.candidate_ref},
          {"canonical_algorithm_ref", value.canonical_algorithm_ref},
          {"human_approval_required", value.human_approval_required},
          {"plan_signature", value.plan_signature},
          {"previous_preferred_canonical_ref",
           value.previous_preferred_canonical_ref},
          {"status", value.status},
          {"version", probation_version}};
}

[[nodiscard]] Json demotion_decision_material(
    const AutomaticDemotionDecision &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"candidate_ref", value.candidate_ref},
          {"demoted_canonical_algorithm_ref",
           value.demoted_canonical_algorithm_ref},
          {"failure_observation_ref", value.failure_observation_ref},
          {"human_approval_required", value.human_approval_required},
          {"plan_signature", value.plan_signature},
          {"reasons", value.reasons},
          {"reevaluation_schedule_ref", value.reevaluation_schedule_ref},
          {"restored_canonical_algorithm_ref",
           value.restored_canonical_algorithm_ref},
          {"status", value.status},
          {"version", probation_version}};
}

} // namespace

ProbationPlan make_probation_plan(
    const AutonomousPromotionAssessment &assessment, std::string policy_ref,
    std::string promotion_assessment_ref,
    std::string probation_candidate_ref,
    std::string previous_preferred_canonical_ref) {
  policy_ref = trimmed(std::move(policy_ref));
  promotion_assessment_ref = trimmed(std::move(promotion_assessment_ref));
  probation_candidate_ref = trimmed(std::move(probation_candidate_ref));
  previous_preferred_canonical_ref =
      trimmed(std::move(previous_preferred_canonical_ref));
  if (!assessment.promotion_allowed ||
      assessment.resulting_state != "POLICY_QUALIFIED" ||
      assessment.human_approval_required || policy_ref.empty() ||
      promotion_assessment_ref.empty() || probation_candidate_ref.empty() ||
      assessment.candidate_ref.empty() ||
      assessment.probation_window_count < 1 ||
      assessment.minimum_probation_observations < 1 ||
      assessment.minimum_probation_uses < 1 ||
      assessment.maximum_canary_share_bp < 1 ||
      assessment.maximum_canary_share_bp > 5000 ||
      assessment.automatic_demotion_predicates.empty()) {
    probation_error("probation plan requires an eligible autonomous assessment");
  }
  const auto predicates =
      canonical_strings(assessment.automatic_demotion_predicates);
  ProbationPlan result{
      .schema_version = 1,
      .probation_version_value = std::string(probation_version),
      .policy_ref = std::move(policy_ref),
      .policy_signature = assessment.policy_signature,
      .promotion_assessment_ref = std::move(promotion_assessment_ref),
      .candidate_ref = std::move(probation_candidate_ref),
      .previous_preferred_canonical_ref =
          std::move(previous_preferred_canonical_ref),
      .window_count = assessment.probation_window_count,
      .minimum_observations = assessment.minimum_probation_observations,
      .minimum_uses = assessment.minimum_probation_uses,
      .maximum_canary_share_bp = assessment.maximum_canary_share_bp,
      .automatic_demotion_predicates = predicates,
      .plan_signature = {}};
  result.plan_signature = contracts::sha256_json(plan_material(result));
  return result;
}

bool probation_canary_selected(const ProbationPlan &plan,
                               std::string_view query_signature) {
  if (!is_sha256(plan.plan_signature) || !is_sha256(query_signature) ||
      plan.maximum_canary_share_bp < 1 ||
      plan.maximum_canary_share_bp > 5000) {
    probation_error("probation canary selection input is invalid");
  }
  const std::string digest = contracts::sha256_json(
      {{"plan_signature", plan.plan_signature},
       {"query_signature", query_signature},
       {"version", probation_version}});
  const unsigned long bucket =
      std::stoul(digest.substr(0, 8), nullptr, 16) % 10000UL;
  return bucket < static_cast<unsigned long>(plan.maximum_canary_share_bp);
}

ProbationObservation make_probation_observation(
    const ProbationPlan &plan, std::string baseline_ref,
    std::string query_signature, std::string context_signature,
    std::string observed_at, int window_index, bool candidate_correct,
    bool baseline_correct,
    bool invariant_passed, bool benchmark_passed, bool integrity_passed,
    bool source_valid, bool reproduction_passed,
    std::vector<std::string> evidence_ids,
    ProbationRegressionSignals regression_signals) {
  baseline_ref = trimmed(std::move(baseline_ref));
  query_signature = trimmed(std::move(query_signature));
  context_signature = trimmed(std::move(context_signature));
  observed_at = trimmed(std::move(observed_at));
  evidence_ids = canonical_strings(std::move(evidence_ids));
  if (baseline_ref.empty() || !is_sha256(query_signature) ||
      !is_sha256(context_signature) || observed_at.empty() || window_index < 0 ||
      window_index >= plan.window_count || evidence_ids.empty()) {
    probation_error("probation observation is invalid");
  }
  ProbationObservation result{
      .schema_version = 1,
      .probation_version_value = std::string(probation_version),
      .plan_signature = plan.plan_signature,
      .candidate_ref = plan.candidate_ref,
      .baseline_ref = std::move(baseline_ref),
      .query_signature = std::move(query_signature),
      .context_signature = std::move(context_signature),
      .observed_at = std::move(observed_at),
      .selection_explanation = {},
      .window_index = window_index,
      .candidate_selected = false,
      .candidate_correct = candidate_correct,
      .baseline_correct = baseline_correct,
      .invariant_passed = invariant_passed,
      .benchmark_passed = benchmark_passed,
      .integrity_passed = integrity_passed,
      .source_valid = source_valid,
      .reproduction_passed = reproduction_passed,
      .regression_signals = regression_signals,
      .evidence_ids = std::move(evidence_ids),
      .observation_signature = {}};
  result.candidate_selected =
      probation_canary_selected(plan, result.query_signature);
  result.selection_explanation =
      result.candidate_selected
          ? "DETERMINISTIC_CANARY_BUCKET_SELECTED"
          : "BASELINE_RETAINED_OUTSIDE_CANARY_BUCKET";
  result.observation_signature =
      contracts::sha256_json(observation_material(result));
  return result;
}

ProbationAssessment assess_probation(
    const ProbationPlan &plan,
    std::vector<ProbationObservation> observations) {
  if (observations.empty() || observations.size() > 100000U) {
    probation_error("probation assessment requires bounded observations");
  }
  std::set<std::string> signatures;
  std::set<int> windows;
  std::set<std::string> regressions;
  int uses = 0;
  for (const auto &observation : observations) {
    if (observation.plan_signature != plan.plan_signature ||
        observation.candidate_ref != plan.candidate_ref ||
        observation.window_index < 0 ||
        observation.window_index >= plan.window_count ||
        observation.observation_signature !=
            contracts::sha256_json(observation_material(observation)) ||
        !signatures.insert(observation.observation_signature).second) {
      probation_error("probation observation does not match the plan");
    }
    windows.insert(observation.window_index);
    if (observation.candidate_selected) {
      ++uses;
    }
    if (!observation.source_valid) {
      regressions.insert("SOURCE_RETRACTION");
    }
    if (!observation.invariant_passed) {
      regressions.insert("INVARIANT_FAILURE");
    }
    if (!observation.benchmark_passed) {
      regressions.insert("BENCHMARK_REGRESSION");
    }
    if (!observation.integrity_passed) {
      regressions.insert("INTEGRITY_FAILURE");
    }
    if (!observation.reproduction_passed) {
      regressions.insert("REPRODUCTION_FAILURE");
    }
    if (observation.candidate_selected && !observation.candidate_correct &&
        observation.baseline_correct) {
      regressions.insert("RETRIEVAL_PRECISION_REGRESSION");
    }
    if (observation.regression_signals.semantic_contradiction) {
      regressions.insert("SEMANTIC_CONTRADICTION");
    }
    if (observation.regression_signals.falsifier_succeeded) {
      regressions.insert("FALSIFIER_SUCCEEDED");
    }
    if (observation.regression_signals.corrected_error_recurrence) {
      regressions.insert("CORRECTED_ERROR_RECURRENCE");
    }
    if (observation.regression_signals.equivalent_failure_retry_regression) {
      regressions.insert("EQUIVALENT_FAILURE_RETRY_REGRESSION");
    }
    if (!observation.regression_signals.independence_passed) {
      regressions.insert("INSUFFICIENT_INDEPENDENCE");
    }
    if (!observation.regression_signals.evidence_fresh) {
      regressions.insert("EVIDENCE_STALE");
    }
    if (!observation.regression_signals.projection_integrity_passed) {
      regressions.insert("PROJECTION_INTEGRITY_FAILURE");
    }
  }
  std::vector<std::string> permitted_regressions;
  for (const auto &reason : regressions) {
    if (std::ranges::find(plan.automatic_demotion_predicates, reason) !=
        plan.automatic_demotion_predicates.end()) {
      permitted_regressions.push_back(reason);
    }
  }
  const bool demote = !permitted_regressions.empty();
  const bool promote = !demote &&
                       observations.size() >=
                           static_cast<std::size_t>(plan.minimum_observations) &&
                       uses >= plan.minimum_uses &&
                       windows.size() == static_cast<std::size_t>(plan.window_count);
  const std::string status = demote
                                 ? "PROBATION_DEMOTION_REQUIRED"
                                 : promote ? "PROBATION_PROMOTION_READY"
                                           : "PROBATION_CONTINUES";
  const std::vector<std::string> observation_signatures(signatures.begin(),
                                                        signatures.end());
  ProbationAssessment result{
      .schema_version = 1,
      .probation_version_value = std::string(probation_version),
      .plan_signature = plan.plan_signature,
      .candidate_ref = plan.candidate_ref,
      .observation_signatures = observation_signatures,
      .observed_window_count = static_cast<int>(windows.size()),
      .observation_count = static_cast<int>(observations.size()),
      .candidate_use_count = uses,
      .regression_reasons = std::move(permitted_regressions),
      .status = status,
      .promotion_ready = promote,
      .demotion_required = demote,
      .assessment_signature = {}};
  result.assessment_signature =
      contracts::sha256_json(assessment_material(result));
  return result;
}

AutomaticPromotionDecision make_automatic_promotion_decision(
    const ProbationPlan &plan, const ProbationAssessment &assessment,
    std::string canonical_algorithm_ref) {
  canonical_algorithm_ref = trimmed(std::move(canonical_algorithm_ref));
  if (assessment.plan_signature != plan.plan_signature ||
      assessment.candidate_ref != plan.candidate_ref ||
      !assessment.promotion_ready || assessment.demotion_required ||
      canonical_algorithm_ref.empty()) {
    probation_error("automatic promotion decision is not eligible");
  }
  AutomaticPromotionDecision result{
      .schema_version = 1,
      .probation_version_value = std::string(probation_version),
      .plan_signature = plan.plan_signature,
      .assessment_signature = assessment.assessment_signature,
      .candidate_ref = plan.candidate_ref,
      .canonical_algorithm_ref = std::move(canonical_algorithm_ref),
      .previous_preferred_canonical_ref =
          plan.previous_preferred_canonical_ref,
      .status = "CANONICAL_PROMOTED",
      .human_approval_required = false,
      .decision_signature = {}};
  result.decision_signature =
      contracts::sha256_json(promotion_decision_material(result));
  return result;
}

AutomaticDemotionDecision make_automatic_demotion_decision(
    const ProbationPlan &plan, const ProbationAssessment &assessment,
    std::string demoted_canonical_algorithm_ref,
    std::string restored_canonical_algorithm_ref,
    std::string failure_observation_ref,
    std::string reevaluation_schedule_ref) {
  demoted_canonical_algorithm_ref =
      trimmed(std::move(demoted_canonical_algorithm_ref));
  restored_canonical_algorithm_ref =
      trimmed(std::move(restored_canonical_algorithm_ref));
  failure_observation_ref = trimmed(std::move(failure_observation_ref));
  reevaluation_schedule_ref = trimmed(std::move(reevaluation_schedule_ref));
  if (assessment.plan_signature != plan.plan_signature ||
      assessment.candidate_ref != plan.candidate_ref ||
      !assessment.demotion_required || assessment.regression_reasons.empty() ||
      failure_observation_ref.empty() || reevaluation_schedule_ref.empty()) {
    probation_error("automatic demotion decision is not eligible");
  }
  AutomaticDemotionDecision result{
      .schema_version = 1,
      .probation_version_value = std::string(probation_version),
      .plan_signature = plan.plan_signature,
      .assessment_signature = assessment.assessment_signature,
      .candidate_ref = plan.candidate_ref,
      .demoted_canonical_algorithm_ref =
          std::move(demoted_canonical_algorithm_ref),
      .restored_canonical_algorithm_ref =
          std::move(restored_canonical_algorithm_ref),
      .reasons = assessment.regression_reasons,
      .failure_observation_ref = std::move(failure_observation_ref),
      .reevaluation_schedule_ref = std::move(reevaluation_schedule_ref),
      .status = "CANONICAL_DEMOTED",
      .human_approval_required = false,
      .decision_signature = {}};
  result.decision_signature =
      contracts::sha256_json(demotion_decision_material(result));
  return result;
}

ProbationPlan probation_plan_from_json(const contracts::Json &value) {
  ProbationPlan plan{
      .schema_version = value.at("schema_version").get<int>(),
      .probation_version_value =
          value.at("probation_version").get<std::string>(),
      .policy_ref = value.at("policy_ref").get<std::string>(),
      .policy_signature = value.at("policy_signature").get<std::string>(),
      .promotion_assessment_ref =
          value.at("promotion_assessment_ref").get<std::string>(),
      .candidate_ref = value.at("candidate_ref").get<std::string>(),
      .previous_preferred_canonical_ref =
          value.at("previous_preferred_canonical_ref").get<std::string>(),
      .window_count = value.at("window_count").get<int>(),
      .minimum_observations = value.at("minimum_observations").get<int>(),
      .minimum_uses = value.at("minimum_uses").get<int>(),
      .maximum_canary_share_bp =
          value.at("maximum_canary_share_bp").get<int>(),
      .automatic_demotion_predicates =
          value.at("automatic_demotion_predicates")
              .get<std::vector<std::string>>(),
      .plan_signature = value.at("plan_signature").get<std::string>()};
  if (to_json(plan) != value || plan.schema_version != 1 ||
      plan.probation_version_value != probation_version ||
      plan.window_count < 1 || plan.minimum_observations < 1 ||
      plan.minimum_uses < 1 || plan.maximum_canary_share_bp < 1 ||
      plan.maximum_canary_share_bp > 5000 ||
      plan.automatic_demotion_predicates.empty() ||
      plan.plan_signature != contracts::sha256_json(plan_material(plan))) {
    probation_error("persisted probation plan is invalid");
  }
  return plan;
}

ProbationObservation probation_observation_from_json(
    const contracts::Json &value) {
  ProbationObservation observation{
      .schema_version = value.at("schema_version").get<int>(),
      .probation_version_value =
          value.at("probation_version").get<std::string>(),
      .plan_signature = value.at("plan_signature").get<std::string>(),
      .candidate_ref = value.at("candidate_ref").get<std::string>(),
      .baseline_ref = value.at("baseline_ref").get<std::string>(),
      .query_signature = value.at("query_signature").get<std::string>(),
      .context_signature = value.at("context_signature").get<std::string>(),
      .observed_at = value.at("observed_at").get<std::string>(),
      .selection_explanation =
          value.at("selection_explanation").get<std::string>(),
      .window_index = value.at("window_index").get<int>(),
      .candidate_selected = value.at("candidate_selected").get<bool>(),
      .candidate_correct = value.at("candidate_correct").get<bool>(),
      .baseline_correct = value.at("baseline_correct").get<bool>(),
      .invariant_passed = value.at("invariant_passed").get<bool>(),
      .benchmark_passed = value.at("benchmark_passed").get<bool>(),
      .integrity_passed = value.at("integrity_passed").get<bool>(),
      .source_valid = value.at("source_valid").get<bool>(),
      .reproduction_passed = value.at("reproduction_passed").get<bool>(),
      .regression_signals =
          {.semantic_contradiction =
               value.at("regression_signals")
                   .at("semantic_contradiction")
                   .get<bool>(),
           .falsifier_succeeded =
               value.at("regression_signals")
                   .at("falsifier_succeeded")
                   .get<bool>(),
           .corrected_error_recurrence =
               value.at("regression_signals")
                   .at("corrected_error_recurrence")
                   .get<bool>(),
           .equivalent_failure_retry_regression =
               value.at("regression_signals")
                   .at("equivalent_failure_retry_regression")
                   .get<bool>(),
           .independence_passed =
               value.at("regression_signals")
                   .at("independence_passed")
                   .get<bool>(),
           .evidence_fresh =
               value.at("regression_signals").at("evidence_fresh").get<bool>(),
           .projection_integrity_passed =
               value.at("regression_signals")
                   .at("projection_integrity_passed")
                   .get<bool>()},
      .evidence_ids =
          value.at("evidence_ids").get<std::vector<std::string>>(),
      .observation_signature =
          value.at("observation_signature").get<std::string>()};
  if (to_json(observation) != value || observation.schema_version != 1 ||
      observation.probation_version_value != probation_version ||
      !is_sha256(observation.plan_signature) ||
      !is_sha256(observation.query_signature) ||
      !is_sha256(observation.context_signature) ||
      observation.observed_at.empty() ||
      observation.selection_explanation.empty() ||
      observation.evidence_ids.empty() ||
      observation.observation_signature !=
          contracts::sha256_json(observation_material(observation))) {
    probation_error("persisted probation observation is invalid");
  }
  return observation;
}

ProbationAssessment probation_assessment_from_json(
    const contracts::Json &value) {
  ProbationAssessment assessment{
      .schema_version = value.at("schema_version").get<int>(),
      .probation_version_value =
          value.at("probation_version").get<std::string>(),
      .plan_signature = value.at("plan_signature").get<std::string>(),
      .candidate_ref = value.at("candidate_ref").get<std::string>(),
      .observation_signatures =
          value.at("observation_signatures")
              .get<std::vector<std::string>>(),
      .observed_window_count = value.at("observed_window_count").get<int>(),
      .observation_count = value.at("observation_count").get<int>(),
      .candidate_use_count = value.at("candidate_use_count").get<int>(),
      .regression_reasons =
          value.at("regression_reasons").get<std::vector<std::string>>(),
      .status = value.at("status").get<std::string>(),
      .promotion_ready = value.at("promotion_ready").get<bool>(),
      .demotion_required = value.at("demotion_required").get<bool>(),
      .assessment_signature =
          value.at("assessment_signature").get<std::string>()};
  if (to_json(assessment) != value || assessment.schema_version != 1 ||
      assessment.probation_version_value != probation_version ||
      assessment.observation_count < 1 ||
      assessment.candidate_use_count < 0 ||
      assessment.observed_window_count < 1 ||
      (assessment.promotion_ready && assessment.demotion_required) ||
      assessment.assessment_signature !=
          contracts::sha256_json(assessment_material(assessment))) {
    probation_error("persisted probation assessment is invalid");
  }
  return assessment;
}

AutomaticPromotionDecision automatic_promotion_decision_from_json(
    const contracts::Json &value) {
  AutomaticPromotionDecision decision{
      .schema_version = value.at("schema_version").get<int>(),
      .probation_version_value =
          value.at("probation_version").get<std::string>(),
      .plan_signature = value.at("plan_signature").get<std::string>(),
      .assessment_signature =
          value.at("assessment_signature").get<std::string>(),
      .candidate_ref = value.at("candidate_ref").get<std::string>(),
      .canonical_algorithm_ref =
          value.at("canonical_algorithm_ref").get<std::string>(),
      .previous_preferred_canonical_ref =
          value.at("previous_preferred_canonical_ref").get<std::string>(),
      .status = value.at("status").get<std::string>(),
      .human_approval_required =
          value.at("human_approval_required").get<bool>(),
      .decision_signature =
          value.at("decision_signature").get<std::string>()};
  if (to_json(decision) != value || decision.schema_version != 1 ||
      decision.probation_version_value != probation_version ||
      decision.human_approval_required ||
      decision.status != "CANONICAL_PROMOTED" ||
      decision.decision_signature !=
          contracts::sha256_json(promotion_decision_material(decision))) {
    probation_error("persisted automatic promotion decision is invalid");
  }
  return decision;
}

AutomaticDemotionDecision automatic_demotion_decision_from_json(
    const contracts::Json &value) {
  AutomaticDemotionDecision decision{
      .schema_version = value.at("schema_version").get<int>(),
      .probation_version_value =
          value.at("probation_version").get<std::string>(),
      .plan_signature = value.at("plan_signature").get<std::string>(),
      .assessment_signature =
          value.at("assessment_signature").get<std::string>(),
      .candidate_ref = value.at("candidate_ref").get<std::string>(),
      .demoted_canonical_algorithm_ref =
          value.at("demoted_canonical_algorithm_ref").get<std::string>(),
      .restored_canonical_algorithm_ref =
          value.at("restored_canonical_algorithm_ref").get<std::string>(),
      .reasons = value.at("reasons").get<std::vector<std::string>>(),
      .failure_observation_ref =
          value.at("failure_observation_ref").get<std::string>(),
      .reevaluation_schedule_ref =
          value.at("reevaluation_schedule_ref").get<std::string>(),
      .status = value.at("status").get<std::string>(),
      .human_approval_required =
          value.at("human_approval_required").get<bool>(),
      .decision_signature =
          value.at("decision_signature").get<std::string>()};
  if (to_json(decision) != value || decision.schema_version != 1 ||
      decision.probation_version_value != probation_version ||
      decision.human_approval_required ||
      decision.status != "CANONICAL_DEMOTED" || decision.reasons.empty() ||
      decision.decision_signature !=
          contracts::sha256_json(demotion_decision_material(decision))) {
    probation_error("persisted automatic demotion decision is invalid");
  }
  return decision;
}

contracts::Json to_json(const ProbationPlan &value) {
  return {{"automatic_demotion_predicates",
           value.automatic_demotion_predicates},
          {"candidate_ref", value.candidate_ref},
          {"maximum_canary_share_bp", value.maximum_canary_share_bp},
          {"minimum_observations", value.minimum_observations},
          {"minimum_uses", value.minimum_uses},
          {"plan_signature", value.plan_signature},
          {"policy_ref", value.policy_ref},
          {"policy_signature", value.policy_signature},
          {"previous_preferred_canonical_ref",
           value.previous_preferred_canonical_ref},
          {"probation_version", value.probation_version_value},
          {"promotion_assessment_ref", value.promotion_assessment_ref},
          {"schema_version", value.schema_version},
          {"window_count", value.window_count}};
}

contracts::Json to_json(const ProbationRegressionSignals &value) {
  return {{"corrected_error_recurrence", value.corrected_error_recurrence},
          {"equivalent_failure_retry_regression",
           value.equivalent_failure_retry_regression},
          {"evidence_fresh", value.evidence_fresh},
          {"falsifier_succeeded", value.falsifier_succeeded},
          {"independence_passed", value.independence_passed},
          {"projection_integrity_passed",
           value.projection_integrity_passed},
          {"semantic_contradiction", value.semantic_contradiction}};
}

contracts::Json to_json(const ProbationObservation &value) {
  auto result = observation_material(value);
  result["observation_signature"] = value.observation_signature;
  result["probation_version"] = value.probation_version_value;
  result["schema_version"] = value.schema_version;
  result.erase("version");
  return result;
}

contracts::Json to_json(const ProbationAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"candidate_ref", value.candidate_ref},
          {"candidate_use_count", value.candidate_use_count},
          {"demotion_required", value.demotion_required},
          {"observation_count", value.observation_count},
          {"observation_signatures", value.observation_signatures},
          {"observed_window_count", value.observed_window_count},
          {"plan_signature", value.plan_signature},
          {"probation_version", value.probation_version_value},
          {"promotion_ready", value.promotion_ready},
          {"regression_reasons", value.regression_reasons},
          {"schema_version", value.schema_version},
          {"status", value.status}};
}

contracts::Json to_json(const AutomaticPromotionDecision &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"candidate_ref", value.candidate_ref},
          {"canonical_algorithm_ref", value.canonical_algorithm_ref},
          {"decision_signature", value.decision_signature},
          {"human_approval_required", value.human_approval_required},
          {"plan_signature", value.plan_signature},
          {"previous_preferred_canonical_ref",
           value.previous_preferred_canonical_ref},
          {"probation_version", value.probation_version_value},
          {"schema_version", value.schema_version},
          {"status", value.status}};
}

contracts::Json to_json(const AutomaticDemotionDecision &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"candidate_ref", value.candidate_ref},
          {"decision_signature", value.decision_signature},
          {"demoted_canonical_algorithm_ref",
           value.demoted_canonical_algorithm_ref},
          {"failure_observation_ref", value.failure_observation_ref},
          {"human_approval_required", value.human_approval_required},
          {"plan_signature", value.plan_signature},
          {"probation_version", value.probation_version_value},
          {"reasons", value.reasons},
          {"reevaluation_schedule_ref", value.reevaluation_schedule_ref},
          {"restored_canonical_algorithm_ref",
           value.restored_canonical_algorithm_ref},
          {"schema_version", value.schema_version},
          {"status", value.status}};
}

} // namespace statewright::saa
