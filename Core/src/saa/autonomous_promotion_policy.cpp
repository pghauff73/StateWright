#include "statewright/saa/autonomous_promotion_policy.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using contracts::Json;

[[noreturn]] void policy_error(std::string message) {
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

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

[[nodiscard]] std::string lowercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

[[nodiscard]] std::vector<std::string> canonical_values(
    std::vector<std::string> values, bool make_uppercase = true) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = make_uppercase ? uppercase(std::move(value))
                           : lowercase(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

void require_count(int value, int minimum, int maximum,
                   std::string_view label) {
  if (value < minimum || value > maximum) {
    policy_error(std::string(label) + " is outside the bounded range");
  }
}

[[nodiscard]] Json gate_json(
    const std::vector<std::pair<std::string, bool>> &values) {
  Json result = Json::object();
  for (const auto &[name, passed] : values) {
    result[name] = passed;
  }
  return result;
}

[[nodiscard]] Json assessment_material(
    const AutonomousPromotionAssessment &value) {
  return {{"automatic_demotion_predicates",
           value.automatic_demotion_predicates},
          {"blocking_reasons", value.blocking_reasons},
          {"candidate_ref", value.candidate_ref},
          {"canonical_governance", to_json(value.canonical_governance)},
          {"experiment_qualification_ref",
           value.experiment_qualification_ref},
          {"gate_results", gate_json(value.gate_results)},
          {"human_approval_required", value.human_approval_required},
          {"maximum_canary_share_bp", value.maximum_canary_share_bp},
          {"minimum_probation_observations",
           value.minimum_probation_observations},
          {"minimum_probation_uses", value.minimum_probation_uses},
          {"policy_signature", value.policy_signature},
          {"probation_window_count", value.probation_window_count},
          {"promotion_allowed", value.promotion_allowed},
          {"resulting_state", value.resulting_state},
          {"retrieval_receipt_ref", value.retrieval_receipt_ref},
          {"snapshot_ref", value.snapshot_ref},
          {"source_age_seconds", value.source_age_seconds},
          {"source_policy_assessment_ref",
           value.source_policy_assessment_ref},
          {"version", autonomous_promotion_policy_version}};
}

[[nodiscard]] CanonicalPromotionGovernanceAssessment
canonical_governance_from_json(const Json &value) {
  CanonicalPromotionGovernanceAssessment result{
      .candidate_ref = value.at("candidate_ref").get<std::string>(),
      .benchmark_gate_signature =
          value.at("benchmark_gate_signature").get<std::string>(),
      .integrity_trajectory_signature =
          value.at("integrity_trajectory_signature").get<std::string>(),
      .benchmark_required = value.at("benchmark_required").get<bool>(),
      .integrity_required = value.at("integrity_required").get<bool>(),
      .blocking_reasons =
          value.at("blocking_reasons").get<std::vector<std::string>>(),
      .status = value.at("status").get<std::string>(),
      .canonical_promotion_allowed =
          value.at("canonical_promotion_allowed").get<bool>(),
      .assessment_signature =
          value.at("assessment_signature").get<std::string>()};
  Json material = {{"benchmark_gate_signature",
                    result.benchmark_gate_signature},
                   {"benchmark_required", result.benchmark_required},
                   {"blocking_reasons", result.blocking_reasons},
                   {"candidate_ref", result.candidate_ref},
                   {"integrity_required", result.integrity_required},
                   {"integrity_trajectory_signature",
                    result.integrity_trajectory_signature},
                   {"status", result.status},
                   {"version", promotion_governance_version}};
  if (to_json(result) != value ||
      result.assessment_signature != contracts::sha256_json(material)) {
    policy_error("persisted canonical promotion governance is invalid");
  }
  return result;
}

[[nodiscard]] bool scoped(const AutonomousPromotionPolicy &policy,
                          const AutonomousPromotionInputs &inputs) {
  const bool domain_allowed =
      std::ranges::find(policy.domain_scopes, "*") !=
          policy.domain_scopes.end() ||
      std::ranges::find(policy.domain_scopes,
                        lowercase(inputs.candidate_domain)) !=
          policy.domain_scopes.end();
  const bool class_allowed =
      std::ranges::find(policy.allowed_candidate_classes,
                        uppercase(inputs.candidate_class)) !=
      policy.allowed_candidate_classes.end();
  const bool primitive_allowed = std::ranges::none_of(
      inputs.candidate_primitives, [&](const auto &primitive) {
        return std::ranges::find(policy.prohibited_primitives,
                                 uppercase(primitive)) !=
               policy.prohibited_primitives.end();
      });
  const bool capabilities_allowed = std::ranges::none_of(
      inputs.candidate_capability_classes, [&](const auto &capability) {
        return std::ranges::find(policy.prohibited_capability_classes,
                                 uppercase(capability)) !=
               policy.prohibited_capability_classes.end();
      });
  return domain_allowed && class_allowed && primitive_allowed &&
         capabilities_allowed;
}

} // namespace

AutonomousPromotionPolicy
canonical_autonomous_promotion_policy(AutonomousPromotionPolicy policy) {
  if (policy.schema_version != 1 ||
      policy.policy_version != autonomous_promotion_policy_version) {
    policy_error("unsupported autonomous promotion policy version");
  }
  policy.domain_scopes =
      canonical_values(std::move(policy.domain_scopes), false);
  policy.allowed_candidate_classes =
      canonical_values(std::move(policy.allowed_candidate_classes));
  policy.prohibited_primitives =
      canonical_values(std::move(policy.prohibited_primitives));
  policy.prohibited_capability_classes =
      canonical_values(std::move(policy.prohibited_capability_classes));
  policy.automatic_demotion_predicates =
      canonical_values(std::move(policy.automatic_demotion_predicates));
  policy.required_semantic_strength =
      uppercase(std::move(policy.required_semantic_strength));
  policy.required_mathematical_strength =
      uppercase(std::move(policy.required_mathematical_strength));
  if (policy.domain_scopes.empty() ||
      policy.allowed_candidate_classes.empty() ||
      policy.prohibited_capability_classes.empty() ||
      policy.required_semantic_strength.empty() ||
      policy.required_mathematical_strength.empty() ||
      policy.automatic_demotion_predicates.empty()) {
    policy_error("autonomous promotion policy has missing hard constraints");
  }
  require_count(policy.minimum_independent_source_groups, 1, 16,
                "minimum source groups");
  require_count(policy.minimum_independent_experiment_groups, 2, 64,
                "minimum experiment groups");
  require_count(policy.maximum_unresolved_hard_contradictions, 0, 10000,
                "maximum contradictions");
  require_count(policy.maximum_successful_falsifiers, 0, 10000,
                "maximum falsifiers");
  require_count(policy.maximum_source_age_seconds, 1, 31536000,
                "maximum source age");
  require_count(policy.probation_window_count, 1, 64,
                "probation window count");
  require_count(policy.minimum_probation_observations, 1, 100000,
                "minimum probation observations");
  require_count(policy.minimum_probation_uses, 1, 100000,
                "minimum probation uses");
  require_count(policy.maximum_canary_share_bp, 1, 5000,
                "maximum canary share");
  const std::string supplied_signature = lowercase(policy.policy_signature);
  policy.policy_signature.clear();
  const std::string expected = contracts::sha256_json(to_json(policy));
  if (!supplied_signature.empty() && supplied_signature != expected) {
    policy_error("autonomous promotion policy signature mismatch");
  }
  policy.policy_signature = expected;
  return policy;
}

AutonomousPromotionPolicy
autonomous_promotion_policy_from_json(const contracts::Json &value) {
  if (!value.is_object()) {
    policy_error("autonomous promotion policy resource must be an object");
  }
  AutonomousPromotionPolicy policy;
  policy.schema_version = value.at("schema_version").get<int>();
  policy.policy_version = value.at("policy_version").get<std::string>();
  policy.domain_scopes =
      value.at("domain_scopes").get<std::vector<std::string>>();
  policy.allowed_candidate_classes =
      value.at("allowed_candidate_classes").get<std::vector<std::string>>();
  policy.prohibited_primitives =
      value.at("prohibited_primitives").get<std::vector<std::string>>();
  policy.prohibited_capability_classes =
      value.at("prohibited_capability_classes")
          .get<std::vector<std::string>>();
  policy.minimum_independent_source_groups =
      value.at("minimum_independent_source_groups").get<int>();
  policy.minimum_independent_experiment_groups =
      value.at("minimum_independent_experiment_groups").get<int>();
  policy.required_semantic_strength =
      value.at("required_semantic_strength").get<std::string>();
  policy.required_mathematical_strength =
      value.at("required_mathematical_strength").get<std::string>();
  policy.maximum_unresolved_hard_contradictions =
      value.at("maximum_unresolved_hard_contradictions").get<int>();
  policy.maximum_successful_falsifiers =
      value.at("maximum_successful_falsifiers").get<int>();
  policy.maximum_source_age_seconds =
      value.at("maximum_source_age_seconds").get<int>();
  policy.probation_window_count = value.at("probation_window_count").get<int>();
  policy.minimum_probation_observations =
      value.at("minimum_probation_observations").get<int>();
  policy.minimum_probation_uses =
      value.at("minimum_probation_uses").get<int>();
  policy.maximum_canary_share_bp =
      value.at("maximum_canary_share_bp").get<int>();
  policy.automatic_demotion_predicates =
      value.at("automatic_demotion_predicates")
          .get<std::vector<std::string>>();
  policy.policy_signature = value.value("policy_signature", "");
  return canonical_autonomous_promotion_policy(std::move(policy));
}

AutonomousPromotionAssessment
autonomous_promotion_assessment_from_json(const contracts::Json &value) {
  std::vector<std::pair<std::string, bool>> gates;
  for (const auto &[name, passed] : value.at("gate_results").items()) {
    gates.emplace_back(name, passed.get<bool>());
  }
  std::ranges::sort(gates, {}, &std::pair<std::string, bool>::first);
  AutonomousPromotionAssessment assessment{
      .schema_version = value.at("schema_version").get<int>(),
      .policy_signature = value.at("policy_signature").get<std::string>(),
      .candidate_ref = value.at("candidate_ref").get<std::string>(),
      .source_policy_assessment_ref =
          value.at("source_policy_assessment_ref").get<std::string>(),
      .snapshot_ref = value.at("snapshot_ref").get<std::string>(),
      .retrieval_receipt_ref =
          value.at("retrieval_receipt_ref").get<std::string>(),
      .experiment_qualification_ref =
          value.at("experiment_qualification_ref").get<std::string>(),
      .gate_results = std::move(gates),
      .canonical_governance =
          canonical_governance_from_json(value.at("canonical_governance")),
      .blocking_reasons =
          value.at("blocking_reasons").get<std::vector<std::string>>(),
      .source_age_seconds = value.value("source_age_seconds", 0),
      .probation_window_count = value.at("probation_window_count").get<int>(),
      .minimum_probation_observations =
          value.at("minimum_probation_observations").get<int>(),
      .minimum_probation_uses =
          value.at("minimum_probation_uses").get<int>(),
      .maximum_canary_share_bp =
          value.at("maximum_canary_share_bp").get<int>(),
      .automatic_demotion_predicates =
          value.at("automatic_demotion_predicates")
              .get<std::vector<std::string>>(),
      .human_approval_required =
          value.at("human_approval_required").get<bool>(),
      .resulting_state = value.at("resulting_state").get<std::string>(),
      .promotion_allowed = value.at("promotion_allowed").get<bool>(),
      .decision_signature =
          value.at("decision_signature").get<std::string>()};
  if (to_json(assessment) != value || assessment.schema_version != 1 ||
      assessment.human_approval_required ||
      assessment.decision_signature !=
          contracts::sha256_json(assessment_material(assessment))) {
    policy_error("persisted autonomous promotion assessment is invalid");
  }
  return assessment;
}

AutonomousPromotionAssessment evaluate_autonomous_promotion(
    AutonomousPromotionPolicy policy, AutonomousPromotionInputs inputs) {
  policy = canonical_autonomous_promotion_policy(std::move(policy));
  inputs.candidate_status = uppercase(std::move(inputs.candidate_status));
  inputs.candidate_class = uppercase(std::move(inputs.candidate_class));
  inputs.candidate_domain = lowercase(std::move(inputs.candidate_domain));
  inputs.semantic_strength = uppercase(std::move(inputs.semantic_strength));
  inputs.mathematical_strength =
      uppercase(std::move(inputs.mathematical_strength));
  inputs.candidate_primitives =
      canonical_values(std::move(inputs.candidate_primitives));
  inputs.candidate_capability_classes =
      canonical_values(std::move(inputs.candidate_capability_classes));
  if (inputs.candidate_ref.empty() ||
      inputs.source_policy_assessment_ref.empty() ||
      inputs.snapshot_ref.empty() || inputs.retrieval_receipt_ref.empty() ||
      inputs.experiment_qualification_ref.empty() ||
      inputs.candidate_status != "EXPERIMENT_QUALIFIED" ||
      inputs.unresolved_hard_contradictions < 0 ||
      inputs.successful_falsifiers < 0 || inputs.source_age_seconds < 0) {
    policy_error("autonomous promotion inputs are invalid");
  }

  const auto canonical_governance = assess_canonical_promotion_governance(
      inputs.benchmark_gate.candidate_ref, &inputs.benchmark_gate,
      &inputs.integrity_trajectory, true, true);
  std::vector<std::pair<std::string, bool>> gates = {
      {"BENCHMARK_GATE", inputs.benchmark_gate.canonical_promotion_eligible},
      {"DEMOTION_PATH", inputs.demotion_path_valid},
      {"EXISTING_KNOWLEDGE_SEARCH",
       inputs.existing_knowledge_search_complete},
      {"EXPERIMENT", inputs.experiment_qualified &&
                         inputs.independent_experiment_groups >=
                             policy.minimum_independent_experiment_groups},
      {"FALSIFIERS", inputs.successful_falsifiers <=
                          policy.maximum_successful_falsifiers},
      {"INVARIANTS", inputs.invariants_passed},
      {"KNOWLEDGE_INTEGRITY",
       inputs.integrity_trajectory.knowledge_integrity_qualified},
      {"MATHEMATICAL_IDENTITY",
       inputs.mathematical_strength ==
           policy.required_mathematical_strength},
      {"PROBATION_PLAN", inputs.probation_plan_valid},
      {"SCOPE", scoped(policy, inputs)},
      {"SEMANTIC_PROOF",
       inputs.semantic_strength == policy.required_semantic_strength},
      {"SNAPSHOT_INTEGRITY", inputs.snapshot_integrity_passed},
      {"SOURCE_FRESHNESS",
       inputs.source_age_seconds <= policy.maximum_source_age_seconds},
      {"SOURCE_INDEPENDENCE",
       inputs.independent_source_groups >=
           policy.minimum_independent_source_groups},
      {"SOURCE_POLICY", inputs.source_policy_passed},
      {"UNRESOLVED_HARD_CONTRADICTIONS",
       inputs.unresolved_hard_contradictions <=
           policy.maximum_unresolved_hard_contradictions}};
  std::ranges::sort(gates, {},
                    &std::pair<std::string, bool>::first);
  std::vector<std::string> blockers;
  for (const auto &[name, passed] : gates) {
    if (!passed) {
      blockers.push_back(name);
    }
  }
  if (!canonical_governance.canonical_promotion_allowed) {
    blockers.push_back("CANONICAL_PROMOTION_GOVERNANCE");
  }
  std::ranges::sort(blockers);
  blockers.erase(std::unique(blockers.begin(), blockers.end()), blockers.end());
  const bool allowed = blockers.empty();
  const std::string resulting_state =
      allowed ? "POLICY_QUALIFIED" : "EXPERIMENT_QUALIFIED";
  AutonomousPromotionAssessment result{
      .schema_version = 1,
      .policy_signature = policy.policy_signature,
      .candidate_ref = inputs.candidate_ref,
      .source_policy_assessment_ref =
          inputs.source_policy_assessment_ref,
      .snapshot_ref = inputs.snapshot_ref,
      .retrieval_receipt_ref = inputs.retrieval_receipt_ref,
      .experiment_qualification_ref =
          inputs.experiment_qualification_ref,
      .gate_results = std::move(gates),
      .canonical_governance = canonical_governance,
      .blocking_reasons = std::move(blockers),
      .source_age_seconds = inputs.source_age_seconds,
      .probation_window_count = policy.probation_window_count,
      .minimum_probation_observations =
          policy.minimum_probation_observations,
      .minimum_probation_uses = policy.minimum_probation_uses,
      .maximum_canary_share_bp = policy.maximum_canary_share_bp,
      .automatic_demotion_predicates =
          policy.automatic_demotion_predicates,
      .human_approval_required = false,
      .resulting_state = resulting_state,
      .promotion_allowed = allowed,
      .decision_signature = {}};
  result.decision_signature =
      contracts::sha256_json(assessment_material(result));
  return result;
}

contracts::Json to_json(const AutonomousPromotionPolicy &value) {
  return {{"allowed_candidate_classes", value.allowed_candidate_classes},
          {"automatic_demotion_predicates",
           value.automatic_demotion_predicates},
          {"domain_scopes", value.domain_scopes},
          {"maximum_canary_share_bp", value.maximum_canary_share_bp},
          {"maximum_source_age_seconds", value.maximum_source_age_seconds},
          {"maximum_successful_falsifiers",
           value.maximum_successful_falsifiers},
          {"maximum_unresolved_hard_contradictions",
           value.maximum_unresolved_hard_contradictions},
          {"minimum_independent_experiment_groups",
           value.minimum_independent_experiment_groups},
          {"minimum_independent_source_groups",
           value.minimum_independent_source_groups},
          {"minimum_probation_observations",
           value.minimum_probation_observations},
          {"minimum_probation_uses", value.minimum_probation_uses},
          {"policy_signature", value.policy_signature},
          {"policy_version", value.policy_version},
          {"probation_window_count", value.probation_window_count},
          {"prohibited_capability_classes",
           value.prohibited_capability_classes},
          {"prohibited_primitives", value.prohibited_primitives},
          {"required_mathematical_strength",
           value.required_mathematical_strength},
          {"required_semantic_strength", value.required_semantic_strength},
          {"schema_version", value.schema_version}};
}

contracts::Json to_json(const AutonomousPromotionInputs &value) {
  return {{"benchmark_gate", to_json(value.benchmark_gate)},
          {"candidate_capability_classes", value.candidate_capability_classes},
          {"candidate_class", value.candidate_class},
          {"candidate_domain", value.candidate_domain},
          {"candidate_primitives", value.candidate_primitives},
          {"candidate_ref", value.candidate_ref},
          {"candidate_status", value.candidate_status},
          {"demotion_path_valid", value.demotion_path_valid},
          {"existing_knowledge_search_complete",
           value.existing_knowledge_search_complete},
          {"experiment_qualification_ref",
           value.experiment_qualification_ref},
          {"experiment_qualified", value.experiment_qualified},
          {"independent_experiment_groups",
           value.independent_experiment_groups},
          {"independent_source_groups", value.independent_source_groups},
          {"integrity_trajectory", to_json(value.integrity_trajectory)},
          {"invariants_passed", value.invariants_passed},
          {"mathematical_strength", value.mathematical_strength},
          {"probation_plan_valid", value.probation_plan_valid},
          {"retrieval_receipt_ref", value.retrieval_receipt_ref},
          {"semantic_strength", value.semantic_strength},
          {"snapshot_integrity_passed", value.snapshot_integrity_passed},
          {"snapshot_ref", value.snapshot_ref},
          {"source_age_seconds", value.source_age_seconds},
          {"source_policy_assessment_ref",
           value.source_policy_assessment_ref},
          {"source_policy_passed", value.source_policy_passed},
          {"successful_falsifiers", value.successful_falsifiers},
          {"unresolved_hard_contradictions",
           value.unresolved_hard_contradictions}};
}

contracts::Json to_json(const AutonomousPromotionAssessment &value) {
  return {{"automatic_demotion_predicates",
           value.automatic_demotion_predicates},
          {"blocking_reasons", value.blocking_reasons},
          {"candidate_ref", value.candidate_ref},
          {"canonical_governance", to_json(value.canonical_governance)},
          {"decision_signature", value.decision_signature},
          {"experiment_qualification_ref",
           value.experiment_qualification_ref},
          {"gate_results", gate_json(value.gate_results)},
          {"human_approval_required", value.human_approval_required},
          {"maximum_canary_share_bp", value.maximum_canary_share_bp},
          {"minimum_probation_observations",
           value.minimum_probation_observations},
          {"minimum_probation_uses", value.minimum_probation_uses},
          {"policy_signature", value.policy_signature},
          {"probation_window_count", value.probation_window_count},
          {"promotion_allowed", value.promotion_allowed},
          {"resulting_state", value.resulting_state},
          {"retrieval_receipt_ref", value.retrieval_receipt_ref},
          {"schema_version", value.schema_version},
          {"snapshot_ref", value.snapshot_ref},
          {"source_age_seconds", value.source_age_seconds},
          {"source_policy_assessment_ref",
           value.source_policy_assessment_ref}};
}

} // namespace statewright::saa
