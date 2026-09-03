#include "statewright/egcf/internet_experiment.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/saa/failure_algebra.hpp"
#include "statewright/saa/improvement_scheduling.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <numeric>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

using contracts::Json;

[[noreturn]] void experiment_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] bool is_sha256(std::string_view value) {
  return value.size() == 64U &&
         std::ranges::all_of(value, [](const unsigned char character) {
           return std::isdigit(character) != 0 ||
                  (character >= 'a' && character <= 'f');
         });
}

void require_sha256(std::string_view value, std::string_view label) {
  if (!is_sha256(value)) {
    experiment_error(std::string(label) + " must be lowercase SHA-256");
  }
}

[[nodiscard]] std::string rational_text(mpq_class value) {
  value.canonicalize();
  return value.get_str();
}

[[nodiscard]] Json rational_values(const std::vector<mpq_class> &values) {
  Json result = Json::array();
  for (auto value : values) {
    result.push_back(rational_text(std::move(value)));
  }
  return result;
}

[[nodiscard]] mpq_class rational_from_json(const Json &value) {
  if (value.is_number_integer()) {
    return mpq_class(value.get<long>());
  }
  if (value.is_number_unsigned()) {
    return mpq_class(value.get<unsigned long>());
  }
  if (value.is_string()) {
    try {
      mpq_class result(value.get<std::string>());
      result.canonicalize();
      return result;
    } catch (const std::exception &) {
      experiment_error("supported SAA constant is not an exact rational");
    }
  }
  experiment_error("supported SAA constant must be an integer or rational string");
}

struct ScalarProgram final {
  std::string primitive;
  mpq_class constant{0};
};

[[nodiscard]] ScalarProgram supported_scalar_program(const Json &mapping) {
  const auto spec = saa::structure_from_mapping(mapping);
  saa::validate_structure(spec);
  if (spec.inputs.size() != 1U || spec.outputs.size() != 1U ||
      !spec.parameters.empty() || !spec.states.empty() ||
      spec.nodes.size() != 1U || !spec.control_edges.empty() ||
      spec.outputs.front().source == std::nullopt) {
    experiment_error(
        "internet qualification supports one-input one-output scalar IR only");
  }
  const auto &node = spec.nodes.front();
  const auto &source = *spec.outputs.front().source;
  if (source.kind != "node" || source.node_id != node.node_id ||
      source.output_index != 0 || node.result_count != 1) {
    experiment_error("supported scalar IR output must bind its only node");
  }
  if (node.primitive == "IDENTITY") {
    if (node.operands.size() != 1U ||
        node.operands.front().kind != "input" ||
        node.operands.front().position != 0) {
      experiment_error("supported IDENTITY IR must read input position zero");
    }
    return {.primitive = "IDENTITY", .constant = 0};
  }
  if (node.primitive == "CONST") {
    if (node.operands.size() != 1U ||
        node.operands.front().kind != "constant") {
      experiment_error("supported CONST IR must contain one exact constant");
    }
    return {.primitive = "CONST",
            .constant = rational_from_json(node.operands.front().value)};
  }
  experiment_error("internet qualification rejects unsupported executable IR primitive: " +
                   node.primitive);
}

[[nodiscard]] mpq_class execute(const ScalarProgram &program,
                                const mpq_class &input) {
  if (program.primitive == "IDENTITY") {
    return input;
  }
  return program.constant;
}

[[nodiscard]] mpq_class mean_absolute_error(
    const ScalarProgram &program, const InternetScalarTrialGroup &group) {
  mpq_class total{0};
  for (std::size_t index = 0; index < group.inputs.size(); ++index) {
    mpq_class difference =
        execute(program, group.inputs[index]) - group.expected_outputs[index];
    if (difference < 0) {
      difference = -difference;
    }
    total += difference;
  }
  total /= static_cast<unsigned long>(group.inputs.size());
  total.canonicalize();
  return total;
}

[[nodiscard]] bool outputs_within_bounds(
    const ScalarProgram &program, const InternetScalarTrialGroup &group,
    const mpq_class &minimum, const mpq_class &maximum) {
  return std::ranges::all_of(group.inputs, [&](const auto &input) {
    const mpq_class output = execute(program, input);
    return output >= minimum && output <= maximum;
  });
}

[[nodiscard]] std::vector<std::string> benchmark_requirements() {
  std::vector<std::string> result;
  for (const auto track : saa::oiec_bench_tracks) {
    std::string name(track);
    std::ranges::transform(name, name.begin(), [](const unsigned char value) {
      return static_cast<char>(std::tolower(value));
    });
    result.push_back("oiec-bench:" + name);
  }
  return result;
}

[[nodiscard]] std::string register_evidence(
    EgcfStore &store, std::string subject_id, std::string recorded_at,
    std::string source_snapshot_hash, std::string independence_group,
    std::string algorithm_id, Json content,
    std::vector<std::string> requirement_ids) {
  EvidenceArtifact artifact{
      .subject_id = std::move(subject_id),
      .claim_ids = {},
      .requirement_ids = std::move(requirement_ids),
      .category = "controlled-experiment",
      .producer = "deterministic-internet-saa-ir-adapter-v1",
      .method = "controlled-ab-experiment",
      .source_snapshot_hash = std::move(source_snapshot_hash),
      .target = "internal-saa-ir",
      .oracle = "exact-rational-frozen-fixture",
      .environment = {{"coordinator_version",
                       internet_experiment_coordinator_version}},
      .command_id = {},
      .algorithm_id = std::move(algorithm_id),
      .created_at = std::move(recorded_at),
      .sha256 = contracts::sha256_json(content),
      .success = true,
      .limitations = {"single-input scalar IDENTITY and CONST subset only"},
      .independence_group = std::move(independence_group),
      .simulated = false,
      .path = {},
      .content = std::move(content)};
  const std::string evidence_id = store.register_record(
      {.object_type = "egcf-evidence", .payload = to_json(artifact)},
      "internet_experiment_evidence_registered");
  if (evidence_id != artifact.object_id()) {
    experiment_error("internet experiment evidence identity mismatch");
  }
  return evidence_id;
}

[[nodiscard]] saa::OIECBenchGatePolicy default_benchmark_policy() {
  saa::OIECBenchGatePolicy result;
  for (const auto track : saa::oiec_bench_tracks) {
    result.minimum_track_scores.emplace_back(std::string(track), 9000);
  }
  result.minimum_independence_groups = 2;
  return result;
}

[[nodiscard]] saa::KnowledgeIntegritySnapshot canonical_integrity_snapshot(
    const saa::KnowledgeIntegritySnapshot &value) {
  const auto canonical = saa::make_integrity_snapshot(
      value.generation, value.canonical_knowledge_count,
      value.semantic_contradictions, value.semantic_drift_events,
      value.false_canonical_admissions, value.corrected_error_opportunities,
      value.corrected_error_recurrences, value.retrieval_queries,
      value.retrieval_correct_selections,
      value.equivalent_failure_opportunities,
      value.equivalent_failure_retries);
  if (!value.snapshot_signature.empty() &&
      value.snapshot_signature != canonical.snapshot_signature) {
    experiment_error("integrity snapshot signature does not match its counts");
  }
  return canonical;
}

[[nodiscard]] Json run_payload(
    const InternetScalarTrialGroup &group,
    const saa::AlgorithmVariantObservation &baseline,
    const saa::AlgorithmVariantObservation &candidate,
    const saa::AlgorithmABExperimentResult &result) {
  return {{"baseline_observation", saa::to_json(baseline)},
          {"candidate_observation", saa::to_json(candidate)},
          {"group", to_json(group)},
          {"result", saa::to_json(result)}};
}

} // namespace

InternetExperimentCoordinator::InternetExperimentCoordinator(EgcfStore &store)
    : store_(store), internet_(store), governance_(store) {}

std::string internet_experiment_context_signature(
    std::vector<std::string> dataset_snapshot_ids,
    std::vector<InternetScalarTrialGroup> trial_groups) {
  std::ranges::sort(dataset_snapshot_ids);
  dataset_snapshot_ids.erase(
      std::unique(dataset_snapshot_ids.begin(), dataset_snapshot_ids.end()),
      dataset_snapshot_ids.end());
  std::ranges::sort(trial_groups, {},
                    &InternetScalarTrialGroup::independence_group);
  Json groups = Json::array();
  for (const auto &group : trial_groups) {
    groups.push_back(
        {{"deterministic_seed", group.deterministic_seed},
         {"expected_outputs", rational_values(group.expected_outputs)},
         {"independence_group", group.independence_group},
         {"inputs", rational_values(group.inputs)}});
  }
  return contracts::sha256_json(
      {{"dataset_snapshot_ids", dataset_snapshot_ids},
       {"trial_groups", std::move(groups)},
       {"version", internet_experiment_coordinator_version}});
}

InternetExperimentResult InternetExperimentCoordinator::qualify(
    const InternetAlgorithmCandidate &candidate,
    InternetExperimentRequest request) {
  const auto canonical_candidate =
      canonical_internet_algorithm_candidate(candidate);
  const std::string candidate_id = canonical_candidate.object_id();
  const auto stored_candidate = store_.get(candidate_id);
  if (stored_candidate.object_type != "internet-algorithm-candidate" ||
      canonical_candidate.status != "VALIDATION_READY") {
    experiment_error(
        "internet experiment requires a registered VALIDATION_READY candidate");
  }
  if (request.baseline_ref.empty() || !request.baseline_saa_ir.is_object() ||
      request.recorded_at.empty()) {
    experiment_error("internet experiment request is incomplete");
  }
  require_sha256(request.context_signature, "experiment context signature");
  if (request.dataset_snapshot_ids.empty()) {
    experiment_error("internet experiment requires dataset snapshots");
  }
  std::ranges::sort(request.dataset_snapshot_ids);
  request.dataset_snapshot_ids.erase(
      std::unique(request.dataset_snapshot_ids.begin(),
                  request.dataset_snapshot_ids.end()),
      request.dataset_snapshot_ids.end());
  for (const auto &snapshot_id : request.dataset_snapshot_ids) {
    if (store_.get(snapshot_id).object_type != "internet-source-snapshot") {
      experiment_error("experiment dataset reference is not an internet snapshot");
    }
  }
  if (request.minimum_experiments < 2 ||
      request.minimum_independence_groups < 2 ||
      request.maximum_total_trials < 1 || request.maximum_total_trials > 10000 ||
      request.minimum_trials_per_group < 1 ||
      request.minimum_material_effect < 0 ||
      request.minimum_output > request.maximum_output) {
    experiment_error("internet experiment limits are invalid");
  }
  if (request.trial_groups.size() <
          static_cast<std::size_t>(request.minimum_experiments) ||
      request.trial_groups.size() > saa::max_aggregated_experiments) {
    experiment_error("internet experiment group count is outside bounded range");
  }
  std::set<std::string> groups;
  std::size_t total_trials = 0U;
  for (auto &group : request.trial_groups) {
    require_sha256(group.baseline_context_signature,
                   "baseline group context signature");
    require_sha256(group.candidate_context_signature,
                   "candidate group context signature");
    if (group.baseline_context_signature != request.context_signature ||
        group.candidate_context_signature != request.context_signature) {
      experiment_error(
          "baseline and candidate must use the identical frozen context");
    }
    if (group.independence_group.empty() ||
        !groups.insert(group.independence_group).second ||
        group.inputs.size() != group.expected_outputs.size() ||
        group.inputs.size() <
            static_cast<std::size_t>(request.minimum_trials_per_group)) {
      experiment_error("internet experiment trial group is invalid");
    }
    total_trials += group.inputs.size();
  }
  if (groups.size() <
          static_cast<std::size_t>(request.minimum_independence_groups) ||
      total_trials > static_cast<std::size_t>(request.maximum_total_trials)) {
    experiment_error("internet experiment independence or trial budget failed");
  }
  if (internet_experiment_context_signature(request.dataset_snapshot_ids,
                                            request.trial_groups) !=
      request.context_signature) {
    experiment_error(
        "experiment context signature does not bind the frozen fixture");
  }
  std::ranges::sort(request.trial_groups, {},
                    &InternetScalarTrialGroup::independence_group);

  const auto candidate_ir =
      saa::canonicalize_mapping(canonical_candidate.proposed_saa_ir);
  const auto baseline_ir = saa::canonicalize_mapping(request.baseline_saa_ir);
  const auto candidate_program =
      supported_scalar_program(canonical_candidate.proposed_saa_ir);
  const auto baseline_program = supported_scalar_program(request.baseline_saa_ir);
  const auto resolver = governance_.evidence_resolver();

  std::vector<saa::KnowledgeIntegritySnapshot> integrity_snapshots;
  std::optional<saa::KnowledgeIntegrityTrajectory> prevalidated_integrity;
  if (canonical_candidate.failure_match_ids.empty()) {
    if (request.benchmark_policy.minimum_track_scores.empty()) {
      request.benchmark_policy = default_benchmark_policy();
    }
    request.benchmark_policy =
        saa::canonical_oiec_bench_policy(std::move(request.benchmark_policy));
    static_cast<void>(saa::make_oiec_bench_profile(
        candidate_id, request.context_signature, request.benchmark_track_scores,
        {"prevalidated-evidence"}));
    if (request.integrity_snapshots.empty()) {
      experiment_error("internet experiment requires integrity snapshots");
    }
    for (const auto &snapshot : request.integrity_snapshots) {
      integrity_snapshots.push_back(canonical_integrity_snapshot(snapshot));
    }
    prevalidated_integrity = saa::assess_integrity_trajectory(
        integrity_snapshots, request.integrity_policy);
  }

  InternetExperimentQualification qualification;
  qualification.candidate_id = candidate_id;
  qualification.baseline_ref = request.baseline_ref;
  qualification.dataset_snapshot_ids = request.dataset_snapshot_ids;
  qualification.context_signature = request.context_signature;
  qualification.canonical_candidate_ir = saa::to_json(candidate_ir);
  qualification.canonical_baseline_ir = saa::to_json(baseline_ir);
  qualification.identical_frozen_contexts = true;

  std::vector<std::string> requirements = benchmark_requirements();
  requirements.push_back("frozen fixture execution");

  if (!canonical_candidate.failure_match_ids.empty()) {
    const std::string evidence_id = register_evidence(
        store_, candidate_id, request.recorded_at, request.context_signature,
        "known-failure-retrieval", candidate_id,
        {{"failure_match_ids", canonical_candidate.failure_match_ids},
         {"status", "KNOWN_EQUIVALENT_FAILURE_RETRY_BLOCKED"}},
        {"known failure assessment"});
    qualification.evidence_ids = {evidence_id};
    qualification.known_failure_retry_blocked = true;
    qualification.blocking_reasons = {
        "KNOWN_EQUIVALENT_FAILURE_RETRY_BLOCKED"};
    const auto opportunity = saa::make_improvement_opportunity(
        resolver, candidate_id + ":known-failure", "FAILURE_PATTERN",
        candidate_ir.structural_hash,
        "collect new independent evidence before retrying a known failure",
        10000, 8000, 9000, 2000, 3000, {evidence_id});
    const std::string opportunity_ref =
        governance_.register_opportunity(opportunity);
    const auto schedule = saa::schedule_improvements({opportunity});
    const std::string schedule_ref = governance_.register_schedule(schedule);
    qualification.improvement_opportunity_ids = {opportunity_ref};
    qualification.improvement_schedule = saa::to_json(schedule);
    qualification.status = "EXPERIMENT_FAILED";
    qualification =
        canonical_internet_experiment_qualification(std::move(qualification));
    const std::string qualification_id =
        internet_.register_experiment_qualification(qualification);
    auto updated_candidate = canonical_candidate;
    updated_candidate.status = "EXPERIMENT_FAILED";
    updated_candidate.experiment_qualification_ids.push_back(qualification_id);
    updated_candidate =
        canonical_internet_algorithm_candidate(std::move(updated_candidate));
    const std::string updated_candidate_id =
        internet_.supersede_algorithm_candidate(
            candidate_id, updated_candidate, "internet experiment blocked");
    InternetExperimentResult result{
        .qualification = std::move(qualification),
        .updated_candidate = std::move(updated_candidate),
        .qualification_id = qualification_id,
        .updated_candidate_id = updated_candidate_id,
        .benchmark_gate_ref = {},
        .integrity_trajectory_ref = {},
        .improvement_schedule_ref = schedule_ref,
        .result_signature = {}};
    auto material = to_json(result);
    material.erase("result_signature");
    result.result_signature = contracts::sha256_json(material);
    return result;
  }

  const auto design = saa::make_ab_experiment_design(
      request.baseline_ref, candidate_id, request.context_signature,
      {{.name = "mean absolute error",
        .direction = "LOWER_IS_BETTER",
        .minimum_material_effect = request.minimum_material_effect}},
      {"bounded execution", "output within bounds"},
      {"frozen fixture execution"}, request.minimum_trials_per_group, true);
  qualification.experiment_design = saa::to_json(design);

  std::vector<saa::AlgorithmABExperimentResult> experiment_results;
  bool invariants_passed = true;
  for (const auto &group : request.trial_groups) {
    const mpq_class baseline_error =
        mean_absolute_error(baseline_program, group);
    const mpq_class candidate_error =
        mean_absolute_error(candidate_program, group);
    const bool baseline_bounds = outputs_within_bounds(
        baseline_program, group, request.minimum_output, request.maximum_output);
    const bool candidate_bounds = outputs_within_bounds(
        candidate_program, group, request.minimum_output, request.maximum_output);
    invariants_passed =
        invariants_passed && baseline_bounds && candidate_bounds;

    const Json common_content =
        {{"context_signature", request.context_signature},
         {"deterministic_seed", group.deterministic_seed},
         {"expected_outputs", rational_values(group.expected_outputs)},
         {"inputs", rational_values(group.inputs)}};
    Json baseline_content = common_content;
    baseline_content["ir_structural_hash"] = baseline_ir.structural_hash;
    baseline_content["mean_absolute_error"] = rational_text(baseline_error);
    baseline_content["output_within_bounds"] = baseline_bounds;
    baseline_content["variant"] = "baseline";
    const std::string baseline_evidence = register_evidence(
        store_, candidate_id, request.recorded_at, request.context_signature,
        group.independence_group, request.baseline_ref,
        std::move(baseline_content), requirements);

    Json candidate_content = common_content;
    candidate_content["ir_structural_hash"] = candidate_ir.structural_hash;
    candidate_content["mean_absolute_error"] = rational_text(candidate_error);
    candidate_content["output_within_bounds"] = candidate_bounds;
    candidate_content["variant"] = "candidate";
    const std::string candidate_evidence = register_evidence(
        store_, candidate_id, request.recorded_at, request.context_signature,
        group.independence_group, candidate_id, std::move(candidate_content),
        requirements);
    qualification.evidence_ids.push_back(baseline_evidence);
    qualification.evidence_ids.push_back(candidate_evidence);

    const auto baseline_observation = saa::make_variant_observation(
        design, request.baseline_ref,
        {{"mean absolute error", baseline_error}}, {baseline_evidence},
        {{"bounded execution", true},
         {"output within bounds", baseline_bounds}},
        static_cast<int>(group.inputs.size()), true);
    const auto candidate_observation = saa::make_variant_observation(
        design, candidate_id, {{"mean absolute error", candidate_error}},
        {candidate_evidence},
        {{"bounded execution", true},
         {"output within bounds", candidate_bounds}},
        static_cast<int>(group.inputs.size()), true);
    const auto experiment_result = saa::qualify_ab_experiment(
        resolver, design, baseline_observation, candidate_observation,
        request.independent_review);
    qualification.experiment_runs.push_back(run_payload(
        group, baseline_observation, candidate_observation, experiment_result));
    experiment_results.push_back(experiment_result);
  }
  qualification.invariants_passed = invariants_passed;

  const auto aggregate = saa::aggregate_repeated_experiments(
      experiment_results, request.minimum_experiments,
      request.minimum_independence_groups);
  qualification.repeated_aggregate = saa::to_json(aggregate);

  const auto benchmark_profile = saa::make_oiec_bench_profile(
      candidate_id, request.context_signature, request.benchmark_track_scores,
      qualification.evidence_ids);
  const auto benchmark_gate = saa::qualify_oiec_bench_gate(
      resolver, benchmark_profile, request.benchmark_policy,
      request.independent_review);
  qualification.benchmark_profile = saa::to_json(benchmark_profile);
  qualification.benchmark_gate = saa::to_json(benchmark_gate);
  qualification.benchmark_passed =
      benchmark_gate.canonical_promotion_eligible;
  const std::string benchmark_gate_ref =
      governance_.register_benchmark_gate(benchmark_gate);

  for (const auto &snapshot : integrity_snapshots) {
    static_cast<void>(governance_.register_integrity_snapshot(snapshot));
    qualification.integrity_snapshots.push_back(saa::to_json(snapshot));
  }
  const auto &integrity_trajectory = *prevalidated_integrity;
  qualification.integrity_trajectory = saa::to_json(integrity_trajectory);
  qualification.integrity_passed =
      integrity_trajectory.knowledge_integrity_qualified;
  const std::string integrity_trajectory_ref =
      governance_.register_integrity_trajectory(integrity_trajectory);

  qualification.experiment_qualified =
      aggregate.sustained_improvement_qualified && invariants_passed;
  if (!aggregate.sustained_improvement_qualified) {
    qualification.blocking_reasons.push_back("EXPERIMENT:" + aggregate.status);
  }
  if (!invariants_passed) {
    qualification.blocking_reasons.push_back("INVARIANT_GATE_FAILED");
  }
  if (!qualification.benchmark_passed) {
    qualification.blocking_reasons.push_back("BENCHMARK:" + benchmark_gate.status);
  }
  if (!qualification.integrity_passed) {
    qualification.blocking_reasons.push_back("INTEGRITY:" +
                                              integrity_trajectory.status);
  }

  std::vector<saa::ImprovementOpportunity> opportunities;
  std::string failure_observation_id;
  if (!qualification.blocking_reasons.empty()) {
    std::vector<std::string> violated;
    if (!invariants_passed) {
      violated.push_back("output within bounds");
    }
    const std::string failure_class =
        !invariants_passed ? "INVARIANT_VIOLATION"
                           : "QUALIFICATION_FAILURE";
    const auto failure = saa::make_failure_observation(
        "internet-candidate", "internet-experiment", failure_class,
        aggregate.status,
        [&]() {
          auto roles = canonical_candidate.semantic_inputs;
          roles.insert(roles.end(), canonical_candidate.semantic_outputs.begin(),
                       canonical_candidate.semantic_outputs.end());
          return roles;
        }(),
        violated, candidate_ir.structural_hash, request.context_signature,
        qualification.evidence_ids, candidate_id);
    const auto registration = governance_.register_failure_observation(failure);
    failure_observation_id = registration.occurrence_ref;
    qualification.failure_observation_ids.push_back(failure_observation_id);

    std::string kind = "EXPERIMENT_TRADEOFF";
    if (!invariants_passed) {
      kind = "FAILURE_PATTERN";
    } else if (!qualification.benchmark_passed) {
      kind = "BENCHMARK_GAP";
    } else if (!qualification.integrity_passed) {
      kind = "INTEGRITY_SIGNAL";
    }
    opportunities.push_back(saa::make_improvement_opportunity(
        resolver, candidate_id + ":qualification", kind,
        candidate_ir.structural_hash,
        "resolve deterministic internet candidate qualification blockers",
        10000, 8000, 8000, 2500, 3000, qualification.evidence_ids));
  }

  for (const auto &opportunity : opportunities) {
    qualification.improvement_opportunity_ids.push_back(
        governance_.register_opportunity(opportunity));
  }
  const auto schedule = saa::schedule_improvements(opportunities);
  qualification.improvement_schedule = saa::to_json(schedule);
  const std::string schedule_ref = governance_.register_schedule(schedule);

  const bool qualified = qualification.blocking_reasons.empty();
  qualification.experiment_qualified = qualified;
  qualification.status =
      qualified ? "EXPERIMENT_QUALIFIED" : "EXPERIMENT_FAILED";
  qualification =
      canonical_internet_experiment_qualification(std::move(qualification));
  const std::string qualification_id =
      internet_.register_experiment_qualification(qualification);

  auto updated_candidate = canonical_candidate;
  updated_candidate.status = qualification.status;
  updated_candidate.experiment_qualification_ids.push_back(qualification_id);
  updated_candidate =
      canonical_internet_algorithm_candidate(std::move(updated_candidate));
  const std::string updated_candidate_id =
      internet_.supersede_algorithm_candidate(
          candidate_id, updated_candidate,
          qualified ? "internet experiment qualified"
                    : "internet experiment failed");

  InternetExperimentResult result{
      .qualification = std::move(qualification),
      .updated_candidate = std::move(updated_candidate),
      .qualification_id = qualification_id,
      .updated_candidate_id = updated_candidate_id,
      .benchmark_gate_ref = benchmark_gate_ref,
      .integrity_trajectory_ref = integrity_trajectory_ref,
      .improvement_schedule_ref = schedule_ref,
      .result_signature = {}};
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

contracts::Json to_json(const InternetScalarTrialGroup &value) {
  return {{"baseline_context_signature", value.baseline_context_signature},
          {"candidate_context_signature", value.candidate_context_signature},
          {"deterministic_seed", value.deterministic_seed},
          {"expected_outputs", rational_values(value.expected_outputs)},
          {"independence_group", value.independence_group},
          {"inputs", rational_values(value.inputs)}};
}

contracts::Json to_json(const InternetExperimentResult &value) {
  return {{"benchmark_gate_ref", value.benchmark_gate_ref},
          {"improvement_schedule_ref", value.improvement_schedule_ref},
          {"integrity_trajectory_ref", value.integrity_trajectory_ref},
          {"qualification", to_json(value.qualification)},
          {"qualification_id", value.qualification_id},
          {"result_signature", value.result_signature},
          {"updated_candidate", to_json(value.updated_candidate)},
          {"updated_candidate_id", value.updated_candidate_id}};
}

} // namespace statewright::egcf
