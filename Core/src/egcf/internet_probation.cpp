#include "statewright/egcf/internet_probation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace statewright::egcf {
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

[[nodiscard]] std::string only_id(const std::vector<std::string> &values,
                                  std::string_view label) {
  if (values.size() != 1U) {
    probation_error(std::string(label) + " requires exactly one immutable ID");
  }
  return values.front();
}

[[nodiscard]] saa::AutonomousPromotionAssessment promotion_assessment(
    const EgcfRecord &stored) {
  if (stored.object_type != "internet-promotion-assessment") {
    probation_error("candidate promotion assessment reference is invalid");
  }
  auto payload = stored.payload;
  payload.erase("policy_id");
  return saa::autonomous_promotion_assessment_from_json(payload);
}

[[nodiscard]] mpq_class rational_value(const Json &value) {
  if (!value.is_string()) {
    probation_error("experiment bound value must be an exact rational string");
  }
  try {
    mpq_class result(value.get<std::string>());
    result.canonicalize();
    return result;
  } catch (const std::exception &) {
    probation_error("experiment bound contains an invalid rational value");
  }
}

struct CanonicalFormBundle final {
  saa::CanonicalRepresentativeAlgorithmForm form;
  std::vector<saa::SemanticRepresentationIssue> issues;
  std::vector<saa::SemanticCandidateMeaning> candidates;
  std::vector<saa::SemanticResolution> resolutions;
};

[[nodiscard]] CanonicalFormBundle build_canonical_form(
    const InternetAlgorithmCandidate &candidate,
    const EgcfRecord &qualification) {
  if (qualification.object_type != "internet-experiment-qualification" ||
      !qualification.payload.at("experiment_qualified").get<bool>() ||
      !qualification.payload.at("invariants_passed").get<bool>()) {
    probation_error("probation requires a qualified experiment record");
  }
  const auto spec = saa::structure_from_mapping(candidate.proposed_saa_ir);
  if (spec.inputs.size() != 1U || spec.outputs.size() != 1U ||
      spec.nodes.size() != 1U || spec.nodes.front().primitive != "IDENTITY" ||
      spec.nodes.front().operands.size() != 1U ||
      spec.nodes.front().operands.front().kind != "input" ||
      spec.nodes.front().operands.front().position != 0) {
    probation_error(
        "first-release probationary canonical admission supports exact identity IR only");
  }

  std::vector<mpq_class> inputs;
  std::vector<mpq_class> outputs;
  for (const auto &run : qualification.payload.at("experiment_runs")) {
    const auto &group = run.at("group");
    if (group.at("inputs").size() != 1U ||
        group.at("expected_outputs").size() != 1U) {
      probation_error("probation experiment bounds must be scalar");
    }
    inputs.push_back(rational_value(group.at("inputs").front()));
    outputs.push_back(rational_value(group.at("expected_outputs").front()));
  }
  if (inputs.size() < 2U || outputs.size() < 2U) {
    probation_error("probation canonical admission requires repeated bounds");
  }
  const auto [input_minimum, input_maximum] =
      std::minmax_element(inputs.begin(), inputs.end());
  const auto [output_minimum, output_maximum] =
      std::minmax_element(outputs.begin(), outputs.end());
  if (*input_minimum == *input_maximum || *output_minimum == *output_maximum) {
    probation_error("probation canonical bounds must have non-zero width");
  }
  const double input_minimum_double = input_minimum->get_d();
  const double input_maximum_double = input_maximum->get_d();
  const double output_minimum_double = output_minimum->get_d();
  const double output_maximum_double = output_maximum->get_d();
  if (!std::isfinite(input_minimum_double) ||
      !std::isfinite(input_maximum_double) ||
      !std::isfinite(output_minimum_double) ||
      !std::isfinite(output_maximum_double)) {
    probation_error("probation canonical bounds exceed finite range");
  }
  const std::string context_signature =
      qualification.payload.at("context_signature").get<std::string>();
  const saa::ProvenanceItems provenance = {
      {"experiment_qualification_id", qualification.object_id()},
      {"frozen_context_signature", context_signature}};
  saa::BoundMap input_bounds = {
      {0, saa::NumericBound(input_minimum_double, input_maximum_double,
                            "EXACT_BOUND", {}, provenance)}};
  saa::BoundMap output_bounds = {
      {0, saa::NumericBound(output_minimum_double, output_maximum_double,
                            "EXACT_BOUND", {}, provenance)}};
  const auto normalization = saa::build_normalization_contract(
      spec, input_bounds, {}, {}, output_bounds,
      saa::TimeNormalization(1.0, "EXACT_BOUND", {}, provenance));
  const saa::MIMOTransferMatrix transfer{
      "CONTINUOUS", {{saa::LinearTransferFunction(
                        "CONTINUOUS", {saa::NumericCoefficient(1)},
                        {saa::NumericCoefficient(1)})}}};
  const auto mimo =
      saa::canonicalize_mimo_transfer_matrix(transfer, normalization);
  const auto search = saa::discover_representative_inputs(mimo);
  saa::SemanticMap input_semantics = {{0, candidate.semantic_inputs.front()}};
  saa::SemanticMap output_semantics = {{0, candidate.semantic_outputs.front()}};
  auto issues = saa::assess_representative_candidate_semantics(
      mimo, search, input_semantics, output_semantics);
  std::vector<std::string> evidence_ids =
      qualification.payload.at("evidence_ids")
          .get<std::vector<std::string>>();
  if (evidence_ids.empty()) {
    probation_error("probation canonical semantics require experiment evidence");
  }
  std::vector<saa::SemanticCandidateMeaning> candidates;
  std::vector<saa::SemanticResolution> resolutions;
  for (const auto &issue : issues) {
    std::vector<int> expected;
    std::vector<int> excluded;
    for (std::size_t output = 0; output < mimo.output_count; ++output) {
      const bool affected =
          std::ranges::find(issue.affected_output_indices, output) !=
          issue.affected_output_indices.end();
      (affected ? expected : excluded).push_back(static_cast<int>(output));
    }
    const std::string falsifier =
        "representative input changes an excluded output";
    candidates.push_back(saa::make_semantic_candidate(
        issue, candidate.semantic_inputs.at(
                   static_cast<std::size_t>(issue.coordinate_index)),
        expected, excluded, {}, {falsifier}));
    resolutions.push_back(saa::evaluate_semantic_candidate(
        issue, candidates.back(), evidence_ids,
        {{falsifier, "SURVIVED", evidence_ids.front()}}, true));
  }
  auto form = saa::canonicalize_representative_algorithm(
      saa::canonicalize_mapping(candidate.proposed_saa_ir), normalization, mimo,
      search, issues, candidates, resolutions);
  return {.form = std::move(form),
          .issues = std::move(issues),
          .candidates = std::move(candidates),
          .resolutions = std::move(resolutions)};
}

[[nodiscard]] saa::ProbationPlan plan_from_admission(
    const EgcfRecord &admission) {
  if (admission.object_type != "internet-probation-admission") {
    probation_error("candidate probation admission reference is invalid");
  }
  return saa::probation_plan_from_json(admission.payload.at("plan"));
}

[[nodiscard]] std::string joined(const std::vector<std::string> &values) {
  std::string result;
  for (const auto &value : values) {
    if (!result.empty()) {
      result += ",";
    }
    result += value;
  }
  return result;
}

[[nodiscard]] std::string failure_class_for(
    const std::vector<std::string> &reasons) {
  if (std::ranges::find(reasons, "INVARIANT_FAILURE") != reasons.end()) {
    return "INVARIANT_VIOLATION";
  }
  if (std::ranges::find(reasons, "SEMANTIC_CONTRADICTION") != reasons.end() ||
      std::ranges::find(reasons, "FALSIFIER_SUCCEEDED") != reasons.end()) {
    return "SEMANTIC_MISMATCH";
  }
  if (std::ranges::find(reasons, "RETRIEVAL_PRECISION_REGRESSION") !=
          reasons.end() ||
      std::ranges::find(reasons, "EQUIVALENT_FAILURE_RETRY_REGRESSION") !=
          reasons.end() ||
      std::ranges::find(reasons, "CORRECTED_ERROR_RECURRENCE") !=
          reasons.end()) {
    return "RETRIEVAL_MISMATCH";
  }
  if (std::ranges::find(reasons, "BENCHMARK_REGRESSION") != reasons.end() ||
      std::ranges::find(reasons, "REPRODUCTION_FAILURE") != reasons.end()) {
    return "EXPERIMENT_REGRESSION";
  }
  if (std::ranges::find(reasons, "SOURCE_RETRACTION") != reasons.end() ||
      std::ranges::find(reasons, "EVIDENCE_STALE") != reasons.end() ||
      std::ranges::find(reasons, "INSUFFICIENT_INDEPENDENCE") !=
          reasons.end()) {
    return "EVIDENCE_FAILURE";
  }
  return "QUALIFICATION_FAILURE";
}

} // namespace

InternetProbationController::InternetProbationController(EgcfStore &store)
    : store_(store), internet_(store), canonical_(store), governance_(store) {}

InternetProbationAdmissionResult InternetProbationController::admit(
    const InternetAlgorithmCandidate &candidate,
    std::string previous_preferred_canonical_ref) {
  const auto canonical_candidate =
      canonical_internet_algorithm_candidate(candidate);
  const std::string candidate_id = canonical_candidate.object_id();
  const auto stored_candidate = store_.get(candidate_id);
  if (stored_candidate.object_type != "internet-algorithm-candidate" ||
      canonical_candidate.status != "POLICY_QUALIFIED" ||
      !canonical_candidate.probation_admission_ids.empty()) {
    probation_error(
        "probation admission requires a registered POLICY_QUALIFIED candidate");
  }
  const std::string assessment_id = only_id(
      canonical_candidate.promotion_assessment_ids, "probation admission");
  const auto assessment = promotion_assessment(store_.get(assessment_id));
  if (!assessment.promotion_allowed || assessment.human_approval_required) {
    probation_error("probation admission requires an autonomous policy pass");
  }
  const auto stored_assessment = store_.get(assessment_id);
  const std::string policy_id =
      stored_assessment.payload.at("policy_id").get<std::string>();
  const auto policy = store_.get(policy_id);
  if (policy.object_type != "internet-promotion-policy" ||
      policy.payload.at("policy_signature").get<std::string>() !=
          assessment.policy_signature) {
    probation_error("probation admission policy binding is invalid");
  }
  const std::string qualification_id = only_id(
      canonical_candidate.experiment_qualification_ids,
      "probation admission experiment");
  const auto qualification = store_.get(qualification_id);
  auto bundle = build_canonical_form(canonical_candidate, qualification);
  const auto canonical_admission = canonical_.admit(
      bundle.form, bundle.issues, bundle.candidates, bundle.resolutions);
  const auto plan = saa::make_probation_plan(
      assessment, policy_id, assessment_id, candidate_id,
      trimmed(std::move(previous_preferred_canonical_ref)));
  const std::string baseline_ref =
      qualification.payload.at("baseline_ref").get<std::string>();
  const std::string admission_id = internet_.register_probation_admission(
      plan, canonical_admission.canonical_id, canonical_admission.source_id,
      baseline_ref, canonical_admission.store_generation,
      "PROBATIONARY_CANONICAL");

  auto updated_candidate = canonical_candidate;
  updated_candidate.status = "PROBATIONARY_CANONICAL";
  updated_candidate.probation_admission_ids.push_back(admission_id);
  updated_candidate.canonical_algorithm_ids.push_back(
      canonical_admission.canonical_id);
  updated_candidate =
      canonical_internet_algorithm_candidate(std::move(updated_candidate));
  const std::string updated_candidate_id =
      internet_.supersede_algorithm_candidate(
          candidate_id, updated_candidate,
          "autonomous probationary canonical admission");
  InternetProbationAdmissionResult result{
      .plan = plan,
      .canonical_admission = canonical_admission,
      .updated_candidate = std::move(updated_candidate),
      .admission_id = admission_id,
      .updated_candidate_id = updated_candidate_id,
      .result_signature = {}};
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

InternetProbationSelection InternetProbationController::select(
    const InternetAlgorithmCandidate &candidate,
    std::string query_signature) {
  const auto canonical_candidate =
      canonical_internet_algorithm_candidate(candidate);
  const std::string candidate_id = canonical_candidate.object_id();
  if (store_.get(candidate_id).object_type != "internet-algorithm-candidate" ||
      (canonical_candidate.status != "PROBATIONARY_CANONICAL" &&
       canonical_candidate.status != "CANONICAL" &&
       canonical_candidate.status != "DEMOTED")) {
    probation_error("probation selection requires an admitted candidate");
  }
  const std::string admission_id = only_id(
      canonical_candidate.probation_admission_ids, "probation selection");
  const auto admission = store_.get(admission_id);
  const auto plan = plan_from_admission(admission);
  const std::string baseline_ref =
      admission.payload.at("baseline_ref").get<std::string>();
  query_signature = trimmed(std::move(query_signature));
  bool candidate_selected = false;
  std::string explanation;
  if (canonical_candidate.status == "PROBATIONARY_CANONICAL") {
    candidate_selected = saa::probation_canary_selected(plan, query_signature);
    explanation = candidate_selected
                      ? "DETERMINISTIC_CANARY_BUCKET_SELECTED"
                      : "BASELINE_RETAINED_OUTSIDE_CANARY_BUCKET";
  } else if (canonical_candidate.status == "CANONICAL") {
    candidate_selected = true;
    explanation = "AUTOMATIC_PROMOTION_PREFERRED";
  } else {
    explanation = "AUTOMATIC_DEMOTION_RESTORED_PREVIOUS_PREFERENCE";
  }
  const std::string fallback =
      plan.previous_preferred_canonical_ref.empty()
          ? baseline_ref
          : plan.previous_preferred_canonical_ref;
  InternetProbationSelection result{
      .admission_id = admission_id,
      .plan_signature = plan.plan_signature,
      .query_signature = query_signature,
      .selected_canonical_ref =
          candidate_selected
              ? admission.payload.at("canonical_algorithm_ref")
                    .get<std::string>()
              : fallback,
      .baseline_ref = baseline_ref,
      .candidate_selected = candidate_selected,
      .explanation = std::move(explanation),
      .selection_signature = {}};
  auto material = to_json(result);
  material.erase("selection_signature");
  result.selection_signature = contracts::sha256_json(material);
  return result;
}

InternetProbationObservationResult InternetProbationController::observe(
    const InternetAlgorithmCandidate &candidate,
    InternetProbationObservationRequest request) {
  const auto canonical_candidate =
      canonical_internet_algorithm_candidate(candidate);
  const std::string candidate_id = canonical_candidate.object_id();
  if (store_.get(candidate_id).object_type != "internet-algorithm-candidate" ||
      (canonical_candidate.status != "PROBATIONARY_CANONICAL" &&
       canonical_candidate.status != "CANONICAL")) {
    probation_error("probation observation requires an active admitted candidate");
  }
  const std::string admission_id = only_id(
      canonical_candidate.probation_admission_ids, "probation observation");
  const auto admission = store_.get(admission_id);
  const auto plan = plan_from_admission(admission);
  const std::string canonical_algorithm_ref =
      admission.payload.at("canonical_algorithm_ref").get<std::string>();
  const auto observation = saa::make_probation_observation(
      plan, admission.payload.at("baseline_ref").get<std::string>(),
      std::move(request.query_signature), std::move(request.context_signature),
      std::move(request.observed_at), request.window_index,
      request.candidate_correct, request.baseline_correct,
      request.invariant_passed, request.benchmark_passed,
      request.integrity_passed, request.source_valid,
      request.reproduction_passed, std::move(request.evidence_ids),
      request.regression_signals);
  const std::string observation_id =
      internet_.register_probation_observation(admission_id, observation);

  std::vector<saa::ProbationObservation> observations;
  std::set<std::string> observation_ids(
      canonical_candidate.probation_observation_ids.begin(),
      canonical_candidate.probation_observation_ids.end());
  observation_ids.insert(observation_id);
  for (const auto &id : observation_ids) {
    auto payload = store_.get(id).payload;
    payload.erase("admission_id");
    observations.push_back(saa::probation_observation_from_json(payload));
  }
  const auto assessment = saa::assess_probation(plan, observations);
  std::optional<saa::AutomaticPromotionDecision> promotion_decision;
  std::optional<saa::AutomaticDemotionDecision> demotion_decision;
  std::string promotion_decision_id;
  std::string demotion_decision_id;
  std::string failure_observation_ref;
  std::string reevaluation_schedule_ref;

  auto updated_candidate = canonical_candidate;
  updated_candidate.probation_observation_ids.assign(observation_ids.begin(),
                                                     observation_ids.end());
  if (assessment.demotion_required) {
    std::vector<std::string> roles = canonical_candidate.semantic_inputs;
    roles.insert(roles.end(), canonical_candidate.semantic_outputs.begin(),
                 canonical_candidate.semantic_outputs.end());
    const auto failure = saa::make_failure_observation(
        "internet-candidate", "internet-probation",
        failure_class_for(assessment.regression_reasons),
        joined(assessment.regression_reasons), roles,
        assessment.regression_reasons, plan.plan_signature,
        observation.context_signature, observation.evidence_ids, candidate_id);
    const auto failure_registration =
        governance_.register_failure_observation(failure);
    failure_observation_ref = failure_registration.occurrence_ref;
    const auto opportunity = saa::make_improvement_opportunity(
        governance_.evidence_resolver(),
        plan.candidate_ref + ":probation-demotion", "FAILURE_PATTERN",
        plan.plan_signature,
        "re-evaluate automatically demoted internet candidate", 10000, 9000,
        9000, 2500, 3000, observation.evidence_ids);
    static_cast<void>(governance_.register_opportunity(opportunity));
    const auto schedule = saa::schedule_improvements(
        std::vector<saa::ImprovementOpportunity>{opportunity});
    reevaluation_schedule_ref = governance_.register_schedule(schedule);
    demotion_decision = saa::make_automatic_demotion_decision(
        plan, assessment, canonical_algorithm_ref,
        plan.previous_preferred_canonical_ref, failure_observation_ref,
        reevaluation_schedule_ref);
    demotion_decision_id = internet_.register_demotion_decision(
        admission_id, *demotion_decision);
    updated_candidate.status = "DEMOTED";
    updated_candidate.demotion_decision_ids.push_back(demotion_decision_id);
    updated_candidate.failure_match_ids.push_back(failure_observation_ref);
  } else if (assessment.promotion_ready &&
             canonical_candidate.status == "PROBATIONARY_CANONICAL") {
    promotion_decision = saa::make_automatic_promotion_decision(
        plan, assessment, canonical_algorithm_ref);
    promotion_decision_id = internet_.register_promotion_decision(
        admission_id, *promotion_decision);
    updated_candidate.status = "CANONICAL";
    updated_candidate.promotion_decision_ids.push_back(promotion_decision_id);
  }
  updated_candidate =
      canonical_internet_algorithm_candidate(std::move(updated_candidate));
  const std::string updated_candidate_id =
      internet_.supersede_algorithm_candidate(
          candidate_id, updated_candidate,
          assessment.demotion_required
              ? "automatic probation demotion"
              : assessment.promotion_ready
                    ? "automatic probation promotion"
                    : "probation observation recorded");
  InternetProbationObservationResult result{
      .observation = observation,
      .assessment = assessment,
      .promotion_decision = std::move(promotion_decision),
      .demotion_decision = std::move(demotion_decision),
      .updated_candidate = std::move(updated_candidate),
      .observation_id = observation_id,
      .promotion_decision_id = promotion_decision_id,
      .demotion_decision_id = demotion_decision_id,
      .failure_observation_ref = failure_observation_ref,
      .reevaluation_schedule_ref = reevaluation_schedule_ref,
      .updated_candidate_id = updated_candidate_id,
      .result_signature = {}};
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

contracts::Json to_json(const InternetProbationAdmissionResult &value) {
  return {{"admission_id", value.admission_id},
          {"canonical_admission", to_json(value.canonical_admission)},
          {"plan", saa::to_json(value.plan)},
          {"result_signature", value.result_signature},
          {"updated_candidate", to_json(value.updated_candidate)},
          {"updated_candidate_id", value.updated_candidate_id}};
}

contracts::Json to_json(const InternetProbationSelection &value) {
  return {{"admission_id", value.admission_id},
          {"baseline_ref", value.baseline_ref},
          {"candidate_selected", value.candidate_selected},
          {"explanation", value.explanation},
          {"plan_signature", value.plan_signature},
          {"query_signature", value.query_signature},
          {"selected_canonical_ref", value.selected_canonical_ref},
          {"selection_signature", value.selection_signature}};
}

contracts::Json to_json(const InternetProbationObservationRequest &value) {
  return {{"baseline_correct", value.baseline_correct},
          {"benchmark_passed", value.benchmark_passed},
          {"candidate_correct", value.candidate_correct},
          {"context_signature", value.context_signature},
          {"evidence_ids", value.evidence_ids},
          {"integrity_passed", value.integrity_passed},
          {"invariant_passed", value.invariant_passed},
          {"observed_at", value.observed_at},
          {"query_signature", value.query_signature},
          {"regression_signals", saa::to_json(value.regression_signals)},
          {"reproduction_passed", value.reproduction_passed},
          {"source_valid", value.source_valid},
          {"window_index", value.window_index}};
}

contracts::Json to_json(const InternetProbationObservationResult &value) {
  return {{"assessment", saa::to_json(value.assessment)},
          {"demotion_decision",
           value.demotion_decision
               ? saa::to_json(*value.demotion_decision)
               : Json(nullptr)},
          {"demotion_decision_id", value.demotion_decision_id},
          {"failure_observation_ref", value.failure_observation_ref},
          {"observation", saa::to_json(value.observation)},
          {"observation_id", value.observation_id},
          {"promotion_decision",
           value.promotion_decision
               ? saa::to_json(*value.promotion_decision)
               : Json(nullptr)},
          {"promotion_decision_id", value.promotion_decision_id},
          {"reevaluation_schedule_ref", value.reevaluation_schedule_ref},
          {"result_signature", value.result_signature},
          {"updated_candidate", to_json(value.updated_candidate)},
          {"updated_candidate_id", value.updated_candidate_id}};
}

} // namespace statewright::egcf
