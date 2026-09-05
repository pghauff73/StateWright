#include "statewright/egcf/autonomous_promotion.hpp"
#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_probation.hpp"
#include "statewright/egcf/grounded_experiment.hpp"
#include "statewright/egcf/exact_affine_expression.hpp"
#include <array>
#include <fstream>

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path temporary_root() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("statewright-internet-experiment-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  return root;
}

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const char character : text) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return result;
}

statewright::contracts::Json constant_ir(int value) {
  return {
      {"entry_nodes", {"constant"}},
      {"inputs", {{{"name", "x"}, {"position", 0}}}},
      {"name", "constant-baseline"},
      {"nodes",
       {{{"id", "constant"},
         {"operands", {{{"constant", value}}}},
         {"primitive", "CONST"}}}},
      {"outputs",
       {{{"name", "y"}, {"position", 0}, {"source", {{"node", "constant"}}}}}}};
}

std::vector<std::pair<std::string, int>> perfect_benchmark_scores() {
  std::vector<std::pair<std::string, int>> result;
  for (const auto track : statewright::saa::oiec_bench_tracks) {
    result.emplace_back(std::string(track), 10000);
  }
  return result;
}

struct StagedCandidate final {
  statewright::egcf::InternetAlgorithmCandidate candidate;
  std::string snapshot_id;
};

StagedCandidate stage_identity_candidate(
    statewright::egcf::EgcfStore &store,
    std::string_view description =
        "Identity algorithm; inputs: x; outputs: y; procedure: return the "
        "input; "
        "source code example: system(\"touch /tmp/must-not-exist\")\n",
    std::string_view expected_status = "VALIDATION_READY",
    std::string_view content_type = "text/plain") {
  using namespace statewright;
  egcf::InternetImprovementStore internet(store);
  const auto policy = sources::canonical_source_policy({});
  const std::string policy_id = internet.register_source_policy(policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/experiment/" +
                        contracts::sha256_json(std::string(description));
  watch.source_policy_id = policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));
  const auto job =
      sources::make_fetch_job(watch, "2026-09-02T01:00:00Z",
                              "2026-09-02T01:00:00Z", "2026-09-02T01:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(job.object_id(), "worker-a",
                                                  "2026-09-02T01:00:01Z",
                                                  "2026-09-02T01:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  sources::FetchResponse response;
  response.requested_url = watch.canonical_url;
  response.final_url = watch.canonical_url;
  response.resolved_addresses = {"93.184.216.34"};
  response.http_status = 200;
  response.headers["content-type"] = content_type;
  response.body = bytes(description);
  response.tls_verified = true;
  response.compressed_bytes = response.body.size();
  response.decompressed_bytes = response.body.size();
  response.provider_identity = "fixture-http-provider-v1";
  const auto capture = internet.capture_success(
      job.object_id(), lease.object_id(), response, watch.source_group);
  const auto snapshot = sources::make_source_snapshot(
      response, capture.artifact_bytes_id, watch.source_group);
  const auto fetch_receipt = sources::make_fetch_receipt(
      job.object_id(), lease.object_id(), response, capture.snapshot_id);
  const auto assessment = sources::assess_internet_source(
      snapshot, fetch_receipt, policy,
      std::span<const std::byte>(response.body), true, "CC0-1.0");
  const auto extraction = sources::extract_internet_snapshot(
      capture.snapshot_id, snapshot.content_type,
      std::span<const std::byte>(response.body));
  egcf::InternetFeedCoordinator coordinator(store);
  const auto feed = coordinator.process(assessment, extraction, "identity");
  const auto selected = std::ranges::find_if(feed.candidates, [](const auto &candidate) {
    return candidate.semantic_inputs == std::vector<std::string>{"css_length_in"};
  });
  REQUIRE(feed.candidates.size() == (selected == feed.candidates.end() ? 1U : 6U));
  const auto &candidate = selected == feed.candidates.end() ? feed.candidates.front() : *selected;
  REQUIRE(candidate.status == expected_status);
  return {.candidate = candidate,
          .snapshot_id = capture.snapshot_id};
}

statewright::egcf::InternetExperimentRequest
request_for(std::string snapshot_id, int input = 3, int expected = 3) {
  using namespace statewright;
  egcf::InternetExperimentRequest request;
  request.baseline_ref = "canonical-algorithm:sha256:" + std::string(64U, 'a');
  request.baseline_saa_ir = constant_ir(0);
  request.dataset_snapshot_ids = {std::move(snapshot_id)};
  request.trial_groups = {{.independence_group = "fixture-a",
                           .baseline_context_signature = {},
                           .candidate_context_signature = {},
                           .deterministic_seed = 11,
                           .inputs = {mpq_class(input)},
                           .expected_outputs = {mpq_class(expected)}},
                          {.independence_group = "fixture-b",
                           .baseline_context_signature = {},
                           .candidate_context_signature = {},
                           .deterministic_seed = 29,
                           .inputs = {mpq_class(input + 1)},
                           .expected_outputs = {mpq_class(expected + 1)}}};
  request.context_signature = egcf::internet_experiment_context_signature(
      request.dataset_snapshot_ids, request.trial_groups);
  for (auto &group : request.trial_groups) {
    group.baseline_context_signature = request.context_signature;
    group.candidate_context_signature = request.context_signature;
  }
  request.minimum_material_effect = mpq_class(1);
  request.minimum_output = mpq_class(-10);
  request.maximum_output = mpq_class(10);
  request.benchmark_track_scores = perfect_benchmark_scores();
  request.integrity_snapshots = {
      saa::make_integrity_snapshot(1, 10, 0, 0, 0, 10, 0, 10, 10, 10, 0),
      saa::make_integrity_snapshot(2, 11, 0, 0, 0, 11, 0, 11, 11, 11, 0)};
  request.recorded_at = "2026-09-02T02:00:00Z";
  return request;
}

statewright::saa::AutonomousPromotionPolicy packaged_promotion_policy() {
  const auto path = std::filesystem::path(STATEWRIGHT_RESOURCE_ROOT) /
                    "policies/internet/default-promotion-policy-v1.json";
  return statewright::saa::autonomous_promotion_policy_from_json(
      statewright::contracts::parse_json(statewright::core::read_text(path)));
}

statewright::egcf::InternetExperimentRequest
affine_request(std::string snapshot_id) {
  auto request = request_for(std::move(snapshot_id));
  request.trial_groups[0].inputs = {mpq_class(-2), mpq_class(0)};
  request.trial_groups[0].expected_outputs = {mpq_class(17, 4),
                                              mpq_class(5, 4)};
  request.trial_groups[1].inputs = {mpq_class(1), mpq_class(3)};
  request.trial_groups[1].expected_outputs = {mpq_class(-1, 4),
                                              mpq_class(-13, 4)};
  request.context_signature =
      statewright::egcf::internet_experiment_context_signature(
          request.dataset_snapshot_ids, request.trial_groups);
  for (auto &group : request.trial_groups) {
    group.baseline_context_signature = request.context_signature;
    group.candidate_context_signature = request.context_signature;
  }
  return request;
}

struct PolicyQualifiedCandidate final {
  statewright::egcf::InternetAlgorithmCandidate candidate;
  std::vector<std::string> evidence_ids;
};

PolicyQualifiedCandidate
policy_qualified_candidate(statewright::egcf::EgcfStore &store) {
  using namespace statewright;
  const auto staged = stage_identity_candidate(store);
  egcf::InternetExperimentCoordinator experiments(store);
  const auto experiment =
      experiments.qualify(staged.candidate, request_for(staged.snapshot_id));
  egcf::InternetImprovementStore internet(store);
  const std::string policy_id =
      internet.register_promotion_policy(packaged_promotion_policy());
  egcf::AutonomousPromotionController promotions(store);
  const auto promotion = promotions.assess(experiment.updated_candidate,
                                           policy_id, "2026-09-02T02:00:00Z");
  return {.candidate = promotion.updated_candidate,
          .evidence_ids = experiment.qualification.evidence_ids};
}

std::string probation_query(const statewright::saa::ProbationPlan &plan,
                            int start, bool selected) {
  for (int index = start; index < start + 100000; ++index) {
    const auto signature =
        statewright::contracts::sha256_json({{"probation-query", index}});
    if (statewright::saa::probation_canary_selected(plan, signature) ==
        selected) {
      return signature;
    }
  }
  FAIL("could not find deterministic probation query bucket");
  return {};
}

statewright::egcf::InternetProbationObservationRequest
probation_observation(const statewright::saa::ProbationPlan &plan,
                      const std::vector<std::string> &evidence_ids,
                      int observation_index, int window_index,
                      bool candidate_correct = true,
                      bool baseline_correct = true) {
  statewright::egcf::InternetProbationObservationRequest request;
  request.query_signature =
      probation_query(plan, observation_index * 100000, true);
  request.context_signature = statewright::contracts::sha256_json(
      {{"probation-context", observation_index}});
  request.observed_at =
      "2026-09-03T00:00:0" + std::to_string(observation_index) + "Z";
  request.window_index = window_index;
  request.candidate_correct = candidate_correct;
  request.baseline_correct = baseline_correct;
  request.invariant_passed = true;
  request.benchmark_passed = true;
  request.integrity_passed = true;
  request.source_valid = true;
  request.reproduction_passed = true;
  request.evidence_ids = evidence_ids;
  return request;
}

} // namespace

TEST_CASE("internet experiment qualifies internal identity IR and rebuilds") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  egcf::InternetExperimentCoordinator coordinator(store);
  const auto result =
      coordinator.qualify(staged.candidate, request_for(staged.snapshot_id));

  REQUIRE(result.qualification.status == "EXPERIMENT_QUALIFIED");
  REQUIRE(result.qualification.experiment_qualified);
  REQUIRE(result.qualification.invariants_passed);
  REQUIRE(result.qualification.benchmark_passed);
  REQUIRE(result.qualification.integrity_passed);
  REQUIRE(result.qualification.internal_ir_only);
  REQUIRE_FALSE(result.qualification.downloaded_code_executed);
  REQUIRE(result.qualification.experiment_runs.size() == 2U);
  REQUIRE(result.updated_candidate.status == "EXPERIMENT_QUALIFIED");
  REQUIRE(result.updated_candidate.experiment_qualification_ids ==
          std::vector<std::string>{result.qualification_id});
  REQUIRE(store.list("algorithm-definition").empty());
  REQUIRE_FALSE(std::filesystem::exists("/tmp/must-not-exist"));

  egcf::InternetImprovementStore internet(store);
  egcf::KnowledgeGovernanceStore governance(store);
  internet.rebuild_projection();
  governance.rebuild_projection();
  REQUIRE(store.get(result.qualification_id).payload ==
          egcf::to_json(result.qualification));
  REQUIRE(internet.list("internet-experiment-qualification").size() == 1U);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet experiment rejects non-identical frozen contexts") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  auto request = request_for(staged.snapshot_id);
  request.trial_groups.front().candidate_context_signature =
      std::string(64U, 'b');
  egcf::InternetExperimentCoordinator coordinator(store);
  REQUIRE_THROWS_AS(coordinator.qualify(staged.candidate, std::move(request)),
                    statewright::common::Error);
  REQUIRE(store.list("egcf-evidence").empty());
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet experiment prevalidates integrity before durable evidence") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  auto request = request_for(staged.snapshot_id);
  request.integrity_snapshots.clear();
  egcf::InternetExperimentCoordinator coordinator(store);
  REQUIRE_THROWS_AS(coordinator.qualify(staged.candidate, std::move(request)),
                    statewright::common::Error);
  REQUIRE(store.list("egcf-evidence").empty());
  egcf::KnowledgeGovernanceStore governance(store);
  REQUIRE(governance.list_objects("saa_benchmark_gates", "gate_ref").empty());
  REQUIRE(governance.list_objects("saa_integrity_snapshots", "snapshot_ref")
              .empty());
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet experiment blocks invariant regression despite score gain") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  auto request = request_for(staged.snapshot_id, 20, 20);
  egcf::InternetExperimentCoordinator coordinator(store);
  const auto result = coordinator.qualify(staged.candidate, std::move(request));
  REQUIRE(result.qualification.status == "EXPERIMENT_FAILED");
  REQUIRE_FALSE(result.qualification.experiment_qualified);
  REQUIRE_FALSE(result.qualification.invariants_passed);
  REQUIRE_FALSE(result.qualification.failure_observation_ids.empty());
  REQUIRE_FALSE(result.qualification.improvement_opportunity_ids.empty());
  REQUIRE(result.updated_candidate.status == "EXPERIMENT_FAILED");
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet experiment blocks known failure before candidate execution") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  egcf::InternetImprovementStore internet(store);
  auto candidate = staged.candidate;
  candidate.failure_match_ids = {"failure:sha256:" + std::string(64U, 'f')};
  candidate =
      egcf::canonical_internet_algorithm_candidate(std::move(candidate));
  static_cast<void>(internet.supersede_algorithm_candidate(
      staged.candidate.object_id(), candidate, "attach known failure"));

  egcf::InternetExperimentCoordinator coordinator(store);
  const auto result =
      coordinator.qualify(candidate, request_for(staged.snapshot_id));
  REQUIRE(result.qualification.status == "EXPERIMENT_FAILED");
  REQUIRE(result.qualification.known_failure_retry_blocked);
  REQUIRE(result.qualification.experiment_runs.empty());
  REQUIRE(result.qualification.evidence_ids.size() == 1U);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet experiment rejects executable and unsupported IR") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  egcf::InternetImprovementStore internet(store);
  auto candidate = staged.candidate;
  candidate.proposed_saa_ir["nodes"][0]["primitive"] = "INVOKE";
  candidate =
      egcf::canonical_internet_algorithm_candidate(std::move(candidate));
  static_cast<void>(internet.supersede_algorithm_candidate(
      staged.candidate.object_id(), candidate, "unsupported executable IR"));

  egcf::InternetExperimentCoordinator coordinator(store);
  REQUIRE_THROWS_AS(
      coordinator.qualify(candidate, request_for(staged.snapshot_id)),
      statewright::common::Error);
  REQUIRE(store.list("egcf-evidence").empty());
  REQUIRE_FALSE(std::filesystem::exists("/tmp/must-not-exist"));
  std::filesystem::remove_all(root);
}

TEST_CASE("autonomous promotion controller qualifies without approval") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  egcf::InternetExperimentCoordinator experiments(store);
  const auto experiment =
      experiments.qualify(staged.candidate, request_for(staged.snapshot_id));
  egcf::InternetImprovementStore internet(store);
  const auto policy = packaged_promotion_policy();
  const std::string policy_id = internet.register_promotion_policy(policy);

  egcf::AutonomousPromotionController promotions(store);
  const auto result = promotions.assess(experiment.updated_candidate, policy_id,
                                        "2026-09-02T02:00:00Z");
  REQUIRE(result.assessment.promotion_allowed);
  REQUIRE(result.assessment.resulting_state == "POLICY_QUALIFIED");
  REQUIRE(result.assessment.source_age_seconds == 3599);
  REQUIRE_FALSE(result.assessment.human_approval_required);
  REQUIRE(result.updated_candidate.status == "POLICY_QUALIFIED");
  REQUIRE(result.updated_candidate.promotion_assessment_ids ==
          std::vector<std::string>{result.assessment_id});
  REQUIRE(internet.list("internet-promotion-policy").size() == 1U);
  REQUIRE(internet.list("internet-promotion-assessment").size() == 1U);
  REQUIRE(store.list("approval").empty());
  std::filesystem::remove_all(root);
}

TEST_CASE("autonomous promotion controller blocks stale source evidence") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  auto request = request_for(staged.snapshot_id);
  request.recorded_at = "2026-09-04T02:00:00Z";
  egcf::InternetExperimentCoordinator experiments(store);
  const auto experiment = experiments.qualify(staged.candidate, request);
  egcf::InternetImprovementStore internet(store);
  const std::string policy_id =
      internet.register_promotion_policy(packaged_promotion_policy());

  egcf::AutonomousPromotionController promotions(store);
  const auto result = promotions.assess(experiment.updated_candidate, policy_id,
                                        request.recorded_at);
  REQUIRE_FALSE(result.assessment.promotion_allowed);
  REQUIRE(result.assessment.source_age_seconds == 176399);
  REQUIRE(result.assessment.blocking_reasons ==
          std::vector<std::string>{"SOURCE_FRESHNESS"});
  REQUIRE(result.updated_candidate.status == "EXPERIMENT_QUALIFIED");
  REQUIRE_FALSE(result.assessment.human_approval_required);
  std::filesystem::remove_all(root);
}

TEST_CASE("autonomous promotion controller blocks failed policy predicates") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  egcf::InternetExperimentCoordinator experiments(store);
  const auto experiment =
      experiments.qualify(staged.candidate, request_for(staged.snapshot_id));
  auto policy = packaged_promotion_policy();
  policy.minimum_independent_source_groups = 2;
  policy.policy_signature.clear();
  policy = saa::canonical_autonomous_promotion_policy(std::move(policy));
  egcf::InternetImprovementStore internet(store);
  const std::string policy_id = internet.register_promotion_policy(policy);

  egcf::AutonomousPromotionController promotions(store);
  const auto result = promotions.assess(experiment.updated_candidate, policy_id,
                                        "2026-09-02T02:00:00Z");
  REQUIRE_FALSE(result.assessment.promotion_allowed);
  REQUIRE(result.assessment.blocking_reasons ==
          std::vector<std::string>{"SOURCE_INDEPENDENCE"});
  REQUIRE(result.updated_candidate.status == "EXPERIMENT_QUALIFIED");
  REQUIRE_FALSE(result.assessment.human_approval_required);
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet probation automatically promotes successful canary windows") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto qualified = policy_qualified_candidate(store);
  egcf::InternetProbationController probation(store);
  const std::string previous =
      "canonical-algorithm:sha256:" + std::string(64U, '7');
  const auto admission =
      probation.admit(qualified.candidate, previous, "2026-09-02T02:00:00Z");
  REQUIRE(admission.updated_candidate.status == "PROBATIONARY_CANONICAL");
  REQUIRE(admission.updated_candidate.probation_admission_ids ==
          std::vector<std::string>{admission.admission_id});
  REQUIRE(admission.updated_candidate.canonical_algorithm_ids ==
          std::vector<std::string>{admission.canonical_admission.canonical_id});
  REQUIRE(store.list("approval").empty());

  const auto selected = probation.select(
      admission.updated_candidate, probation_query(admission.plan, 0, true));
  REQUIRE(selected.candidate_selected);
  REQUIRE(selected.selected_canonical_ref ==
          admission.canonical_admission.canonical_id);
  const auto baseline =
      probation.select(admission.updated_candidate,
                       probation_query(admission.plan, 100000, false));
  REQUIRE_FALSE(baseline.candidate_selected);
  REQUIRE(baseline.selected_canonical_ref == previous);

  auto candidate = admission.updated_candidate;
  egcf::InternetProbationObservationResult final;
  for (int index = 0; index < 4; ++index) {
    final = probation.observe(
        candidate, probation_observation(admission.plan, qualified.evidence_ids,
                                         index, index % 2));
    candidate = final.updated_candidate;
  }
  REQUIRE(final.assessment.status == "PROBATION_PROMOTION_READY");
  REQUIRE(final.promotion_decision.has_value());
  REQUIRE_FALSE(final.promotion_decision->human_approval_required);
  REQUIRE(final.updated_candidate.status == "CANONICAL");
  REQUIRE(final.updated_candidate.promotion_decision_ids ==
          std::vector<std::string>{final.promotion_decision_id});
  const auto preferred = probation.select(
      final.updated_candidate, probation_query(admission.plan, 200000, false));
  REQUIRE(preferred.candidate_selected);
  REQUIRE(preferred.selected_canonical_ref ==
          admission.canonical_admission.canonical_id);
  REQUIRE(store.list("internet-probation-observation").size() == 4U);
  REQUIRE(store.list("internet-promotion-decision").size() == 1U);
  REQUIRE(store.list("approval").empty());

  egcf::InternetImprovementStore internet(store);
  internet.rebuild_projection();
  egcf::CanonicalAlgorithmStore canonical(store);
  canonical.rebuild_projection();
  REQUIRE(canonical.get(admission.canonical_admission.canonical_id)
              .at("payload")
              .at("representative_behavior_signature") ==
          admission.canonical_admission.canonical_id.substr(
              std::string("canonical-algorithm:sha256:").size()));
  std::filesystem::remove_all(root);
}

TEST_CASE("internet probation automatically demotes retrieval regression") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto qualified = policy_qualified_candidate(store);
  egcf::InternetProbationController probation(store);
  const std::string previous =
      "canonical-algorithm:sha256:" + std::string(64U, '8');
  const auto admission =
      probation.admit(qualified.candidate, previous, "2026-09-02T02:00:00Z");
  const auto result = probation.observe(
      admission.updated_candidate,
      probation_observation(admission.plan, qualified.evidence_ids, 0, 0, false,
                            true));
  REQUIRE(result.assessment.status == "PROBATION_DEMOTION_REQUIRED");
  REQUIRE(result.demotion_decision.has_value());
  REQUIRE_FALSE(result.demotion_decision->human_approval_required);
  REQUIRE(result.updated_candidate.status == "DEMOTED");
  REQUIRE_FALSE(result.failure_observation_ref.empty());
  REQUIRE_FALSE(result.reevaluation_schedule_ref.empty());
  REQUIRE(store.list("internet-demotion-decision").size() == 1U);
  REQUIRE(store.list("approval").empty());

  const auto restored = probation.select(
      result.updated_candidate, probation_query(admission.plan, 100000, true));
  REQUIRE_FALSE(restored.candidate_selected);
  REQUIRE(restored.selected_canonical_ref == previous);

  egcf::CanonicalAlgorithmStore canonical(store);
  egcf::CanonicalAlgorithmQuery query;
  query.representative_behavior_signature =
      admission.canonical_admission.canonical_id.substr(
          std::string("canonical-algorithm:sha256:").size());
  const auto search = canonical.search(query);
  REQUIRE_FALSE(search.selected_canonical_id.has_value());
  REQUIRE(search.excluded.size() == 1U);
  REQUIRE(search.excluded.front().at("reasons") ==
          contracts::Json::array({"automatically_demoted_internet_candidate"}));
  std::filesystem::remove_all(root);
}

TEST_CASE("internet exact affine family qualifies promotes and demotes") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto staged = stage_identity_candidate(
        store, "Affine calibration algorithm; inputs: x; outputs: y; "
               "procedure: return -3/2*x+5/4");
    const auto program =
        egcf::internet_exact_scalar_program(staged.candidate.proposed_saa_ir);
    REQUIRE(program.slope == mpq_class(-3, 2));
    REQUIRE(program.bias == mpq_class(5, 4));
    REQUIRE(program.bounded_steps == 2);
    const auto experiment = egcf::InternetExperimentCoordinator(store).qualify(
        staged.candidate, affine_request(staged.snapshot_id));
    REQUIRE(experiment.qualification.experiment_qualified);
    REQUIRE(experiment.qualification.invariants_passed);
    REQUIRE_FALSE(experiment.qualification.downloaded_code_executed);
    egcf::InternetImprovementStore internet(store);
    const auto policy_id =
        internet.register_promotion_policy(packaged_promotion_policy());
    const auto qualified = egcf::AutonomousPromotionController(store).assess(
        experiment.updated_candidate, policy_id, "2026-09-02T02:00:00Z");
    REQUIRE(qualified.assessment.promotion_allowed);
    egcf::InternetProbationController probation(store);
    const std::string previous =
        "canonical-algorithm:sha256:" + std::string(64U, '9');
    const auto admission = probation.admit(qualified.updated_candidate,
                                           previous, "2026-09-02T02:00:00Z");
    REQUIRE(admission.updated_candidate.status == "PROBATIONARY_CANONICAL");
    auto candidate = admission.updated_candidate;
    for (int index = 0; index < 4; ++index) {
      const auto observation = probation.observe(
          candidate, probation_observation(
                         admission.plan, experiment.qualification.evidence_ids,
                         index, index % 2));
      candidate = observation.updated_candidate;
    }
    REQUIRE(candidate.status == "CANONICAL");
    auto regression = probation_observation(
        admission.plan, experiment.qualification.evidence_ids, 4, 1, false,
        true);
    const auto demotion = probation.observe(candidate, regression);
    REQUIRE(demotion.updated_candidate.status == "DEMOTED");
    const auto restored =
        probation.select(demotion.updated_candidate,
                         probation_query(admission.plan, 500000, true));
    REQUIRE_FALSE(restored.candidate_selected);
    REQUIRE(restored.selected_canonical_ref == previous);
    REQUIRE(store.list("approval").empty());
    internet.verify_integrity();
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet affine qualification rejects repeated input evidence and "
          "mismatched source") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto staged = stage_identity_candidate(
        store, "Affine calibration algorithm; inputs: x; outputs: y; "
               "procedure: return -3/2*x+5/4");
    egcf::InternetExperimentCoordinator experiments(store);
    SECTION("independence groups cannot relabel the same input evidence") {
      auto request = affine_request(staged.snapshot_id);
      request.trial_groups[1].inputs = request.trial_groups[0].inputs;
      request.trial_groups[1].expected_outputs =
          request.trial_groups[0].expected_outputs;
      request.context_signature = egcf::internet_experiment_context_signature(
          request.dataset_snapshot_ids, request.trial_groups);
      for (auto &group : request.trial_groups) {
        group.baseline_context_signature = request.context_signature;
        group.candidate_context_signature = request.context_signature;
      }
      REQUIRE_THROWS(experiments.qualify(staged.candidate, request));
      REQUIRE(store.list("internet-experiment-qualification").empty());
    }
    SECTION(
        "modified IR cannot qualify against an unchanged source declaration") {
      auto altered = staged.candidate;
      altered.proposed_saa_ir["nodes"][1]["operands"][1]["constant"] = "0";
      altered =
          egcf::canonical_internet_algorithm_candidate(std::move(altered));
      egcf::InternetImprovementStore internet(store);
      static_cast<void>(internet.supersede_algorithm_candidate(
          staged.candidate.object_id(), altered, "alter translation"));
      REQUIRE_THROWS(
          experiments.qualify(altered, affine_request(staged.snapshot_id)));
      REQUIRE(store.list("internet-experiment-qualification").empty());
    }
    SECTION(
        "a better baseline score cannot override an incorrect exact output") {
      auto request = affine_request(staged.snapshot_id);
      request.trial_groups[0].expected_outputs[0] += mpq_class(1, 100);
      request.context_signature = egcf::internet_experiment_context_signature(
          request.dataset_snapshot_ids, request.trial_groups);
      for (auto &group : request.trial_groups) {
        group.baseline_context_signature = request.context_signature;
        group.candidate_context_signature = request.context_signature;
      }
      const auto result = experiments.qualify(staged.candidate, request);
      REQUIRE_FALSE(result.qualification.experiment_qualified);
      REQUIRE_FALSE(result.qualification.invariants_passed);
    }
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet promotion evaluates freshness at policy admission and "
          "observation time") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto staged = stage_identity_candidate(store);
    const auto experiment = egcf::InternetExperimentCoordinator(store).qualify(
        staged.candidate, request_for(staged.snapshot_id));
    egcf::InternetImprovementStore internet(store);
    const auto policy_id =
        internet.register_promotion_policy(packaged_promotion_policy());
    egcf::AutonomousPromotionController promotions(store);
    SECTION("delayed policy assessment cannot reuse a fresh experiment clock") {
      const auto result = promotions.assess(experiment.updated_candidate,
                                            policy_id, "2026-09-04T02:00:00Z");
      REQUIRE_FALSE(result.assessment.promotion_allowed);
      REQUIRE(std::ranges::find(result.assessment.blocking_reasons,
                                "SOURCE_FRESHNESS") !=
              result.assessment.blocking_reasons.end());
    }
    SECTION("delayed canonical admission rechecks source age") {
      const auto result = promotions.assess(experiment.updated_candidate,
                                            policy_id, "2026-09-02T02:00:00Z");
      REQUIRE(result.assessment.promotion_allowed);
      REQUIRE_THROWS(egcf::InternetProbationController(store).admit(
          result.updated_candidate, {}, "2026-09-04T02:00:00Z"));
      REQUIRE(store.list("internet-probation-admission").empty());
    }
    SECTION(
        "expired observation overrides a claimed valid source and demotes") {
      const auto result = promotions.assess(experiment.updated_candidate,
                                            policy_id, "2026-09-02T02:00:00Z");
      egcf::InternetProbationController probation(store);
      const auto admission =
          probation.admit(result.updated_candidate, {}, "2026-09-02T02:00:00Z");
      auto observation = probation_observation(
          admission.plan, experiment.qualification.evidence_ids, 0, 0);
      observation.observed_at = "2026-09-04T02:00:00Z";
      observation.source_valid = true;
      const auto observed =
          probation.observe(admission.updated_candidate, observation);
      REQUIRE_FALSE(observed.observation.source_valid);
      REQUIRE(observed.updated_candidate.status == "DEMOTED");
    }
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet affine novelty distinguishes related algorithms from exact "
          "source duplicates") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto identity = stage_identity_candidate(
        store, "Identity algorithm; inputs: temperature; outputs: result; "
               "procedure: return the input");
    const auto experiment = egcf::InternetExperimentCoordinator(store).qualify(
        identity.candidate, request_for(identity.snapshot_id));
    egcf::InternetImprovementStore internet(store);
    const auto policy_id =
        internet.register_promotion_policy(packaged_promotion_policy());
    const auto qualified = egcf::AutonomousPromotionController(store).assess(
        experiment.updated_candidate, policy_id, "2026-09-02T02:00:00Z");
    const auto admission = egcf::InternetProbationController(store).admit(
        qualified.updated_candidate, {}, "2026-09-02T02:00:00Z");
    const auto affine = stage_identity_candidate(
        store, "Temperature calibration algorithm; inputs: temperature; "
               "outputs: result; procedure: return 2*temperature+1");
    REQUIRE(affine.candidate.status == "VALIDATION_READY");
    REQUIRE(std::ranges::find(affine.candidate.related_match_ids,
                              admission.canonical_admission.canonical_id) !=
            affine.candidate.related_match_ids.end());
    REQUIRE(affine.candidate.exact_match_ids.empty());
    const auto duplicate = stage_identity_candidate(
        store,
        "Copied identity algorithm; inputs: temperature; outputs: result; "
        "procedure: return the input",
        "DUPLICATE");
    REQUIRE(
        duplicate.candidate.exact_match_ids ==
        std::vector<std::string>{admission.canonical_admission.canonical_id});
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet CSS unit conversion uses independent frozen expected outputs") {
  using namespace statewright;
  const auto section = core::read_text(std::filesystem::path(__FILE__)
      .parent_path().parent_path() / "fixtures/css-absolute-lengths-20240322.html");
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto staged = stage_identity_candidate(store,
        section + "<h2>Next section</h2>", "VALIDATION_READY", "text/html");
    auto request = request_for(staged.snapshot_id);
    request.minimum_output = -1000;
    request.maximum_output = 1000;
    // Frozen source-derived cases, not outputs produced by the translator.
    // Groups are fixture partitions, not claims of independent live review.
    request.trial_groups[0].inputs = {mpq_class(-1), mpq_class(0), mpq_class(1, 2)};
    request.trial_groups[0].expected_outputs = {mpq_class(-96), mpq_class(0), mpq_class(48)};
    request.trial_groups[1].inputs = {mpq_class(1), mpq_class(2)};
    request.trial_groups[1].expected_outputs = {mpq_class(96), mpq_class(192)};
    bool correct = true;
    SECTION("frozen conversion cases") {}
    SECTION("wrong ratio is rejected") {
      request.trial_groups[1].expected_outputs[1] = 190;
      correct = false;
    }
    request.context_signature = egcf::internet_experiment_context_signature(
        request.dataset_snapshot_ids, request.trial_groups);
    for (auto &group : request.trial_groups) {
      group.baseline_context_signature = request.context_signature;
      group.candidate_context_signature = request.context_signature;
    }
    const auto result = egcf::InternetExperimentCoordinator(store).qualify(
        staged.candidate, request);
    REQUIRE(result.qualification.experiment_qualified == correct);
    REQUIRE(result.qualification.invariants_passed == correct);
    REQUIRE_FALSE(result.qualification.downloaded_code_executed);
    REQUIRE(store.list("algorithm-definition").empty());
    if (correct) {
      // Synthetic lifecycle observations test the gates, not live acceptance.
      egcf::InternetImprovementStore internet(store);
      const auto policy_id = internet.register_promotion_policy(packaged_promotion_policy());
      const auto qualified = egcf::AutonomousPromotionController(store).assess(
          result.updated_candidate, policy_id, "2026-09-02T02:00:00Z");
      REQUIRE(qualified.assessment.promotion_allowed);
      egcf::InternetProbationController probation(store);
      const auto admission = probation.admit(qualified.updated_candidate, {},
                                              "2026-09-02T02:00:00Z");
      REQUIRE(admission.updated_candidate.status == "PROBATIONARY_CANONICAL");
      auto candidate = admission.updated_candidate;
      for (int index = 0; index < 4; ++index) {
        candidate = probation.observe(candidate, probation_observation(
            admission.plan, result.qualification.evidence_ids, index, index % 2))
                        .updated_candidate;
      }
      REQUIRE(candidate.status == "CANONICAL");
      const auto regression = probation_observation(admission.plan,
          result.qualification.evidence_ids, 4, 1, false, true);
      REQUIRE(probation.observe(candidate, regression).updated_candidate.status == "DEMOTED");
    }
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet v5 affine parser accepts exact arithmetic and rejects nonlinear syntax") {
  using statewright::egcf::parse_exact_affine_expression;
  const auto value = parse_exact_affine_expression("(x - 32) * 5 / 9", "x");
  REQUIRE(value);
  REQUIRE(value->slope == mpq_class(5, 9));
  REQUIRE(value->bias == mpq_class(-160, 9));
  REQUIRE(parse_exact_affine_expression("x + 273.15", "x")->bias == mpq_class(5463, 20));
  for (const auto expression : {"x*x", "1/x", "x/0", "system(x)", "x if x > 0", "other + x"})
    REQUIRE_FALSE(parse_exact_affine_expression(expression, "x"));
}

TEST_CASE("internet grounded protocol requires signed independent evidence and records coverage separately") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  auto request = request_for(staged.snapshot_id);
  const auto search = egcf::exact_capability_search(store, staged.candidate);
  REQUIRE(search.at("candidates").empty());
  const auto make_evidence = [&](contracts::Json content) {
    return egcf::register_grounded_evidence(store, staged.candidate, std::move(content),
        "unit-test-only", "unit-test-only", "fixture", request.recorded_at);
  };
  request.baseline_ref = make_evidence({{"kind", "CANONICAL_CATALOG_UNSUPPORTED_BASELINE_V1"},
      {"candidate_id", staged.candidate.object_id()}, {"workspace", root.string()}, {"search", search}});
  request.baseline_saa_ir = contracts::Json::object();
  request.benchmark_policy.minimum_track_scores = request.benchmark_track_scores;
  request.benchmark_policy.minimum_independence_groups = 2;
  egcf::InternetExperimentProtocol protocol;
  protocol.protocol_version = std::string(egcf::grounded_experiment_version);
  protocol.applicable_candidate_statuses = {"VALIDATION_READY"};
  protocol.baseline_ref = request.baseline_ref;
  protocol.baseline_saa_ir = request.baseline_saa_ir;
  protocol.dataset_snapshot_ids = request.dataset_snapshot_ids;
  for (const auto &group : request.trial_groups) protocol.trial_groups.push_back(egcf::to_json(group));
  protocol.minimum_material_effect = "1";
  protocol.minimum_output = "-10";
  protocol.maximum_output = "10";
  for (const auto &[name, score] : request.benchmark_track_scores) protocol.benchmark_track_scores[name] = score;
  protocol.benchmark_policy = saa::to_json(request.benchmark_policy);
  protocol.integrity_policy = saa::to_json(request.integrity_policy);
  for (const auto &snapshot : request.integrity_snapshots) protocol.integrity_snapshots.push_back(saa::to_json(snapshot));
  const auto measured = make_evidence({{"benchmark_track_scores", protocol.benchmark_track_scores},
                                      {"integrity_snapshots", protocol.integrity_snapshots}});
  const auto negative = make_evidence({{"kind", "unit-test-negative-control-fixture"}});
  protocol.valid_from = "2026-09-02T00:00:00Z";
  protocol.valid_until = "2026-09-03T00:00:00Z";
  protocol.source_provenance = {{"grounded", {
      {"adoption_mode", "NEW_CAPABILITY"}, {"author_identity", "fixture-author"},
      {"candidate_id", staged.candidate.object_id()}, {"source_fragment_id", staged.candidate.source_fragment_id},
      {"source_body_sha256", store.get(staged.snapshot_id).payload.at("body_sha256")},
      {"candidate_ir_sha256", contracts::sha256_json(staged.candidate.proposed_saa_ir)},
      {"baseline_rationale", "CANONICAL_CATALOG_LOOKUP_ONLY"},
      {"reference_oracle_ir", staged.candidate.proposed_saa_ir},
      {"measurement_evidence_id", measured},
      {"claim", {{"inputs", staged.candidate.semantic_inputs}, {"outputs", staged.candidate.semantic_outputs},
                 {"units", staged.candidate.units}, {"domain", "test rationals"}, {"exclusions", {"production use"}}}},
      {"review_evidence_ids", contracts::Json::array()}}}};
  protocol = egcf::canonical_internet_experiment_protocol(std::move(protocol));
  const auto binding = egcf::experiment_review_binding(protocol);
  const auto hex = [](const unsigned char *data, std::size_t length) {
    std::string text;
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < length; ++i) { text += digits[data[i] >> 4]; text += digits[data[i] & 15]; }
    return text;
  };
  contracts::Json trust = {{"schema_version", 1}, {"allowed_adoption_modes", {"NEW_CAPABILITY", "REPLACEMENT"}},
                           {"reviewer_public_keys", contracts::Json::object()}};
  for (int i = 0; i < 2; ++i) {
    std::array<unsigned char, 32> private_bytes{};
    private_bytes[0] = static_cast<unsigned char>(i + 1);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, private_bytes.data(), private_bytes.size()), EVP_PKEY_free);
    REQUIRE(key);
    std::array<unsigned char, 32> public_bytes{};
    std::size_t public_size = public_bytes.size();
    REQUIRE(EVP_PKEY_get_raw_public_key(key.get(), public_bytes.data(), &public_size) == 1);
    const std::string reviewer = "fixture-reviewer-" + std::to_string(i);
    trust["reviewer_public_keys"][reviewer] = hex(public_bytes.data(), public_size);
    contracts::Json message = {{"reviewer_id", reviewer}, {"protocol_binding_sha256", binding},
        {"verdict", "APPROVE"}, {"reviewed_at", request.recorded_at},
        {"independence_group", request.trial_groups[static_cast<std::size_t>(i)].independence_group},
        {"method", "fixture-method-" + std::to_string(i)}, {"derivation", "synthetic test only"},
        {"shared_dependencies", {"synthetic fixture"}}, {"negative_control_evidence_ids", {negative}}};
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    REQUIRE(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1);
    std::array<unsigned char, 64> signature{};
    std::size_t signature_size = signature.size();
    const auto bytes_to_sign = contracts::canonical_json(message);
    REQUIRE(EVP_DigestSign(context.get(), signature.data(), &signature_size,
        reinterpret_cast<const unsigned char *>(bytes_to_sign.data()), bytes_to_sign.size()) == 1);
    contracts::Json envelope = {{"message", message}, {"signature_hex", hex(signature.data(), signature_size)}};
    egcf::verify_experiment_review(envelope, trust);
    auto altered = envelope;
    altered["message"]["verdict"] = "REJECT";
    REQUIRE_THROWS(egcf::verify_experiment_review(altered, trust));
    protocol.source_provenance["grounded"]["review_evidence_ids"].push_back(make_evidence(envelope));
  }
  protocol = egcf::canonical_internet_experiment_protocol(std::move(protocol));
  REQUIRE(egcf::experiment_review_binding(protocol) == binding);
  egcf::InternetImprovementStore internet(store);
  request.protocol_id = internet.register_experiment_protocol(protocol);
  REQUIRE_THROWS(egcf::validate_grounded_experiment(store, staged.candidate, request));
  // Trust anchors here are test-only. No production reviewer keys are created.
  std::ofstream(root / ".ourd-agent/egcf/experiment-trust.json") << contracts::canonical_json(trust);
  REQUIRE(egcf::validate_grounded_experiment(store, staged.candidate, request).new_capability);
  auto changed = request;
  changed.trial_groups[0].expected_outputs[0] = 100;
  changed.context_signature = egcf::internet_experiment_context_signature(changed.dataset_snapshot_ids, changed.trial_groups);
  REQUIRE_THROWS(egcf::validate_grounded_experiment(store, staged.candidate, changed));
  egcf::InternetExperimentCoordinator coordinator(store);
  const auto result = coordinator.qualify(staged.candidate, request);
  REQUIRE(result.qualification.experiment_qualified);
  REQUIRE(result.qualification.canonical_baseline_ir.empty());
  REQUIRE(result.qualification.experiment_design.at("adoption_mode") == "NEW_CAPABILITY");
  REQUIRE(result.qualification.experiment_design.at("baseline_scope") == "CANONICAL_CATALOG_LOOKUP_ONLY");
  std::filesystem::remove_all(root);
}
