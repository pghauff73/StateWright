#include "statewright/egcf/autonomous_promotion.hpp"
#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_probation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-internet-experiment-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
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
  return {{"entry_nodes", {"constant"}},
          {"inputs", {{{"name", "x"}, {"position", 0}}}},
          {"name", "constant-baseline"},
          {"nodes",
           {{{"id", "constant"},
             {"operands", {{{"constant", value}}}},
             {"primitive", "CONST"}}}},
          {"outputs",
           {{{"name", "y"},
             {"position", 0},
             {"source", {{"node", "constant"}}}}}}};
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

StagedCandidate stage_identity_candidate(statewright::egcf::EgcfStore &store) {
  using namespace statewright;
  egcf::InternetImprovementStore internet(store);
  const auto policy = sources::canonical_source_policy({});
  const std::string policy_id = internet.register_source_policy(policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/identity-experiment";
  watch.source_policy_id = policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));
  const auto job = sources::make_fetch_job(
      watch, "2026-09-02T01:00:00Z", "2026-09-02T01:00:00Z",
      "2026-09-02T01:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "worker-a", "2026-09-02T01:00:01Z",
      "2026-09-02T01:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  sources::FetchResponse response;
  response.requested_url = watch.canonical_url;
  response.final_url = watch.canonical_url;
  response.resolved_addresses = {"93.184.216.34"};
  response.http_status = 200;
  response.headers["content-type"] = "text/plain";
  response.body = bytes(
      "Identity algorithm; inputs: x; outputs: y; procedure: return the input; "
      "source code example: system(\"touch /tmp/must-not-exist\")\n");
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
  REQUIRE(feed.candidates.size() == 1U);
  REQUIRE(feed.candidates.front().status == "VALIDATION_READY");
  return {.candidate = feed.candidates.front(),
          .snapshot_id = capture.snapshot_id};
}

statewright::egcf::InternetExperimentRequest request_for(
    std::string snapshot_id, int input = 3, int expected = 3) {
  using namespace statewright;
  egcf::InternetExperimentRequest request;
  request.baseline_ref =
      "canonical-algorithm:sha256:" + std::string(64U, 'a');
  request.baseline_saa_ir = constant_ir(0);
  request.dataset_snapshot_ids = {std::move(snapshot_id)};
  request.trial_groups = {
      {.independence_group = "fixture-a",
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
      statewright::contracts::parse_json(
          statewright::core::read_text(path)));
}

struct PolicyQualifiedCandidate final {
  statewright::egcf::InternetAlgorithmCandidate candidate;
  std::vector<std::string> evidence_ids;
};

PolicyQualifiedCandidate policy_qualified_candidate(
    statewright::egcf::EgcfStore &store) {
  using namespace statewright;
  const auto staged = stage_identity_candidate(store);
  egcf::InternetExperimentCoordinator experiments(store);
  const auto experiment =
      experiments.qualify(staged.candidate, request_for(staged.snapshot_id));
  egcf::InternetImprovementStore internet(store);
  const std::string policy_id =
      internet.register_promotion_policy(packaged_promotion_policy());
  egcf::AutonomousPromotionController promotions(store);
  const auto promotion =
      promotions.assess(experiment.updated_candidate, policy_id);
  return {.candidate = promotion.updated_candidate,
          .evidence_ids = experiment.qualification.evidence_ids};
}

std::string probation_query(const statewright::saa::ProbationPlan &plan,
                            int start, bool selected) {
  for (int index = start; index < start + 100000; ++index) {
    const auto signature = statewright::contracts::sha256_json(
        {{"probation-query", index}});
    if (statewright::saa::probation_canary_selected(plan, signature) ==
        selected) {
      return signature;
    }
  }
  FAIL("could not find deterministic probation query bucket");
  return {};
}

statewright::egcf::InternetProbationObservationRequest probation_observation(
    const statewright::saa::ProbationPlan &plan,
    const std::vector<std::string> &evidence_ids, int observation_index,
    int window_index, bool candidate_correct = true,
    bool baseline_correct = true) {
  statewright::egcf::InternetProbationObservationRequest request;
  request.query_signature =
      probation_query(plan, observation_index * 100000, true);
  request.context_signature = statewright::contracts::sha256_json(
      {{"probation-context", observation_index}});
  request.observed_at = "2026-09-03T00:00:0" +
                        std::to_string(observation_index) + "Z";
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

TEST_CASE("internet experiment prevalidates integrity before durable evidence") {
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
  REQUIRE(governance
              .list_objects("saa_integrity_snapshots", "snapshot_ref")
              .empty());
  std::filesystem::remove_all(root);
}

TEST_CASE("internet experiment blocks invariant regression despite score gain") {
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

TEST_CASE("internet experiment blocks known failure before candidate execution") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto staged = stage_identity_candidate(store);
  egcf::InternetImprovementStore internet(store);
  auto candidate = staged.candidate;
  candidate.failure_match_ids = {
      "failure:sha256:" + std::string(64U, 'f')};
  candidate = egcf::canonical_internet_algorithm_candidate(std::move(candidate));
  static_cast<void>(internet.supersede_algorithm_candidate(
      staged.candidate.object_id(), candidate, "attach known failure"));

  egcf::InternetExperimentCoordinator coordinator(store);
  const auto result = coordinator.qualify(
      candidate, request_for(staged.snapshot_id));
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
  candidate = egcf::canonical_internet_algorithm_candidate(std::move(candidate));
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
  const auto result =
      promotions.assess(experiment.updated_candidate, policy_id);
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
  const auto result =
      promotions.assess(experiment.updated_candidate, policy_id);
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
  const auto result =
      promotions.assess(experiment.updated_candidate, policy_id);
  REQUIRE_FALSE(result.assessment.promotion_allowed);
  REQUIRE(result.assessment.blocking_reasons ==
          std::vector<std::string>{"SOURCE_INDEPENDENCE"});
  REQUIRE(result.updated_candidate.status == "EXPERIMENT_QUALIFIED");
  REQUIRE_FALSE(result.assessment.human_approval_required);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet probation automatically promotes successful canary windows") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto qualified = policy_qualified_candidate(store);
  egcf::InternetProbationController probation(store);
  const std::string previous =
      "canonical-algorithm:sha256:" + std::string(64U, '7');
  const auto admission = probation.admit(qualified.candidate, previous);
  REQUIRE(admission.updated_candidate.status == "PROBATIONARY_CANONICAL");
  REQUIRE(admission.updated_candidate.probation_admission_ids ==
          std::vector<std::string>{admission.admission_id});
  REQUIRE(admission.updated_candidate.canonical_algorithm_ids ==
          std::vector<std::string>{
              admission.canonical_admission.canonical_id});
  REQUIRE(store.list("approval").empty());

  const auto selected = probation.select(
      admission.updated_candidate,
      probation_query(admission.plan, 0, true));
  REQUIRE(selected.candidate_selected);
  REQUIRE(selected.selected_canonical_ref ==
          admission.canonical_admission.canonical_id);
  const auto baseline = probation.select(
      admission.updated_candidate,
      probation_query(admission.plan, 100000, false));
  REQUIRE_FALSE(baseline.candidate_selected);
  REQUIRE(baseline.selected_canonical_ref == previous);

  auto candidate = admission.updated_candidate;
  egcf::InternetProbationObservationResult final;
  for (int index = 0; index < 4; ++index) {
    final = probation.observe(
        candidate,
        probation_observation(admission.plan, qualified.evidence_ids, index,
                              index % 2));
    candidate = final.updated_candidate;
  }
  REQUIRE(final.assessment.status == "PROBATION_PROMOTION_READY");
  REQUIRE(final.promotion_decision.has_value());
  REQUIRE_FALSE(final.promotion_decision->human_approval_required);
  REQUIRE(final.updated_candidate.status == "CANONICAL");
  REQUIRE(final.updated_candidate.promotion_decision_ids ==
          std::vector<std::string>{final.promotion_decision_id});
  const auto preferred = probation.select(
      final.updated_candidate,
      probation_query(admission.plan, 200000, false));
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
  const auto admission = probation.admit(qualified.candidate, previous);
  const auto result = probation.observe(
      admission.updated_candidate,
      probation_observation(admission.plan, qualified.evidence_ids, 0, 0,
                            false, true));
  REQUIRE(result.assessment.status == "PROBATION_DEMOTION_REQUIRED");
  REQUIRE(result.demotion_decision.has_value());
  REQUIRE_FALSE(result.demotion_decision->human_approval_required);
  REQUIRE(result.updated_candidate.status == "DEMOTED");
  REQUIRE_FALSE(result.failure_observation_ref.empty());
  REQUIRE_FALSE(result.reevaluation_schedule_ref.empty());
  REQUIRE(store.list("internet-demotion-decision").size() == 1U);
  REQUIRE(store.list("approval").empty());

  const auto restored = probation.select(
      result.updated_candidate,
      probation_query(admission.plan, 100000, true));
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
          contracts::Json::array(
              {"automatically_demoted_internet_candidate"}));
  std::filesystem::remove_all(root);
}
