#include "statewright/egcf/internet_chebyshev_comparison_store.hpp"
#include "statewright/egcf/internet_improvement_orchestrator.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <chrono>
#include <filesystem>

using namespace statewright;
using namespace statewright::egcf;

TEST_CASE("internet Chebyshev comparison resumes native receipts without new events") {
  const auto workspace = std::filesystem::temp_directory_path() /
      ("statewright-chebyshev-resume-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto resources = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources";
  std::string proposal_id, design_id, result_id, head;
  const auto request_for = [&](const std::string &cycle) {
    InternetImprovementRunRequest request;
    request.cycle_key = cycle;
    request.worker_id = "synthetic-reference-test-worker";
    request.current_timestamp = "2026-09-08T00:00:30Z";
    request.action_lease_expires_at = "2026-09-08T00:02:00Z";
    request.fetch_lease_expires_at = "2026-09-08T00:02:00Z";
    request.policy.action_deadline = "2026-09-08T00:01:00Z";
    request.policy.enable_acquisition = false;
    request.policy.require_reasoning = false;
    request.policy.enabled_action_kinds = {"COMPARE_POLYNOMIAL_REFERENCE"};
    request.policy.polynomial_reference_design_ids = {design_id};
    request.policy.polynomial_proposal_scope_id = proposal_id;
    return request;
  };
  {
    EgcfStore store(workspace, resources);
    const auto ir = internet_exact_polynomial_ir({-1, 0, 2});
    const contracts::Json proposal = {{"source_context", {{"operation", "ChebyshevT"}, {"degree", 2}}},
        {"proposed_saa_ir", ir}, {"candidate_ir_sha256", contracts::sha256_json(ir)}};
    EvidenceInput input;
    input.subject_id = "synthetic-test-proposal";
    input.category = "POLYNOMIAL_TRANSLATION_PROPOSAL";
    input.producer = "synthetic-chebyshev-resume-test";
    input.method = "SYNTHETIC_TEST_FIXTURE";
    input.source_snapshot_hash = std::string(64U, 'a');
    input.content = {{"kind", "POLYNOMIAL_SOURCE_TRANSLATION_PROPOSAL_V1"}, {"proposal", proposal},
        {"fragment_id", "synthetic-fragment"}, {"snapshot_id", "synthetic-snapshot"}};
    input.limitations = {"Synthetic test fixture, not production acceptance evidence"};
    proposal_id = EvidenceManager(store).collect(input);
    const auto evidence = evidence_artifact_from_json(store.get(proposal_id).payload);
    const contracts::Json groups = contracts::Json::array({
        {{"name", "anchors"}, {"cases", contracts::Json::array({{{"numerator", 0}, {"denominator", 1}}})}},
        {{"name", "fractions"}, {"cases", contracts::Json::array({{{"numerator", 1}, {"denominator", 2}}})}}});
    input.subject_id = proposal_id;
    input.category = "POLYNOMIAL_REFERENCE_DESIGN";
    input.content = build_chebyshev_reference_design(proposal_id, evidence.sha256, proposal, groups);
    design_id = EvidenceManager(store).collect(input);
    {
      // Native evidence envelopes around deliberately synthetic protocol data.
      // This tests linkage, not source review or full protocol qualification.
      auto separate = input;
      separate.content["groups"][0]["name"] = "protocol_anchors";
      const auto separate_id = EvidenceManager(store).collect(separate);
      const auto frozen_design = evidence_artifact_from_json(store.get(separate_id).payload);
      contracts::Json protocol_groups = contracts::Json::array();
      for (const auto &group : frozen_design.content.at("groups")) {
        protocol_groups.push_back({{"independence_group", group.at("name")},
            {"inputs", group.at("inputs")}, {"expected_outputs", group.at("expected_outputs")}});
      }
      const contracts::Json protocol_design = {
          {"protocol_version", internet_polynomial_protocol_version},
          {"trial_groups", protocol_groups},
          {"source_provenance", {{"grounded", {
              {"reference_design_evidence_id", separate_id},
              {"candidate_ir_sha256", proposal.at("candidate_ir_sha256")},
              {"source_fragment_id", "synthetic-fragment"},
              {"source_body_sha256", input.source_snapshot_hash},
              {"reference_oracle_ir", chebyshev_reference_oracle_descriptor(2)}}}}}};
      REQUIRE_NOTHROW(verify_chebyshev_protocol_reference_design(
          protocol_design, separate_id, frozen_design, evidence));
      auto altered = protocol_design;
      altered["trial_groups"][0]["expected_outputs"][0] = "999";
      REQUIRE_THROWS(verify_chebyshev_protocol_reference_design(
          altered, separate_id, frozen_design, evidence));
      auto protocol_input = input;
      protocol_input.subject_id = "synthetic-protocol-candidate";
      protocol_input.category = "POLYNOMIAL_PROTOCOL_DESIGN";
      protocol_input.content = {{"kind", "POLYNOMIAL_PROTOCOL_DESIGN_FREEZE_V1"},
          {"snapshot_id", "synthetic-snapshot"}, {"protocol_design", protocol_design}};
      const auto freeze_id = EvidenceManager(store).collect(protocol_input);
      auto bound_request = request_for("protocol-bound-reference-execution");
      bound_request.policy.polynomial_reference_design_ids = {separate_id};
      bound_request.policy.polynomial_reference_protocol_freeze_ids = {{separate_id, freeze_id}};
      InternetImprovementOrchestrator bound_orchestrator(store, nullptr, nullptr, "none", "none",
          [](std::string_view timestamp) { return std::string(timestamp); });
      const auto bound_plan = bound_orchestrator.plan(bound_request);
      REQUIRE(bound_plan.actions.size() == 1U);
      CHECK(bound_plan.actions.front().parameters.at("polynomial_protocol_freeze_id") == freeze_id);
      const auto bound_run = bound_orchestrator.run_once(bound_request);
      REQUIRE(bound_run.status == "COMPLETED");
      REQUIRE(bound_run.output_ids.size() == 1U);
      CHECK_FALSE(bound_run.action_lease_id.empty());
      const auto comparison_id = bound_run.output_ids.front();
      const auto comparison = evidence_artifact_from_json(store.get(comparison_id).payload);
      CHECK(comparison.content.at("polynomial_protocol_freeze_id") == freeze_id);
      const auto start = evidence_artifact_from_json(store.get(
          comparison.content.at("start_evidence_id").get<std::string>()).payload);
      CHECK(start.content.at("polynomial_protocol_freeze_id") == freeze_id);
      auto ground = protocol_design.at("source_provenance").at("grounded");
      ground["freeze_evidence_id"] = freeze_id;
      ground["reference_comparison_evidence_id"] = comparison_id;
      const auto linked_head = store.event_head();
      REQUIRE_NOTHROW(verify_chebyshev_qualification_comparison(store, ground, comparison.created_at));
      REQUIRE_THROWS(verify_chebyshev_qualification_comparison(store, ground, "2000-01-01T00:00:00Z"));
      CHECK(store.event_head() == linked_head);
      // Results from a different freeze cannot be retrospectively relabelled.
      protocol_input.producer = "different-protocol-freeze-fixture";
      const auto later_freeze = EvidenceManager(store).collect(protocol_input);
      const auto before_reject = store.event_head();
      REQUIRE_THROWS(chebyshev_comparison_work(store, separate_id, false, proposal_id, later_freeze));
      CHECK(store.event_head() == before_reject);
    }
    const auto before = store.event_head();
    CHECK(chebyshev_comparison_work(store, design_id, true).empty());
    CHECK(store.event_head() == before);
    REQUIRE_THROWS(chebyshev_comparison_work(store, design_id, false, "wrong-proposal"));
    CHECK(store.event_head() == before);
    // Persist an unfinished attempt, then resume. No observation is invented.
    input.subject_id = design_id;
    input.category = "POLYNOMIAL_REFERENCE_COMPARISON_START";
    input.content = {{"design_id", design_id}, {"design_sha256", contracts::sha256_json(
        evidence_artifact_from_json(store.get(design_id).payload).content)}, {"qualification_claim", "NONE"}};
    static_cast<void>(EvidenceManager(store).collect(input));
    InternetImprovementOrchestrator orchestrator(store, nullptr, nullptr, "none", "none",
        [](std::string_view timestamp) { return std::string(timestamp); });
    const auto scheduled = orchestrator.run_once(request_for("reference-scheduled-execution"));
    REQUIRE(scheduled.status == "COMPLETED");
    REQUIRE(scheduled.output_ids.size() == 1U);
    CHECK_FALSE(scheduled.action_lease_id.empty());
    CHECK_FALSE(scheduled.action_receipt_id.empty());
    result_id = scheduled.output_ids.front();
    const auto result = evidence_artifact_from_json(store.get(result_id).payload);
    CHECK(result.content.at("comparison").at("mismatch_count") == 0);
    CHECK(result.content.contains("start_evidence_id"));
    CHECK_FALSE(result.success.has_value());
    head = store.event_head();
    CHECK(chebyshev_comparison_work(store, design_id, false) == result_id);
    CHECK(store.event_head() == head);
  }
  {
    EgcfStore store(workspace, resources);
    CHECK(chebyshev_comparison_work(store, design_id, true) == result_id);
    CHECK(chebyshev_comparison_work(store, design_id, false, proposal_id) == result_id);
    CHECK(store.event_head() == head);
    InternetImprovementStore internet(store);
    InternetImprovementOrchestrator orchestrator(store, nullptr, nullptr, "none", "none",
        [](std::string_view timestamp) { return std::string(timestamp); });
    CHECK(orchestrator.plan(request_for("completed-reference-not-rescheduled")).actions.empty());
    for (const auto &[expired, protocol_bound] : {
        std::pair{false, false}, std::pair{true, false},
        std::pair{false, true}, std::pair{true, true}}) {
      const auto original_design = evidence_artifact_from_json(store.get(design_id).payload);
      EvidenceInput independent_attempt;
      independent_attempt.subject_id = original_design.subject_id;
      independent_attempt.category = original_design.category;
      independent_attempt.producer = "synthetic-reference-recovery-test";
      independent_attempt.method = "SYNTHETIC_DISTINCT_INTERRUPTED_DESIGN";
      independent_attempt.source_snapshot_hash = original_design.source_snapshot_hash;
      independent_attempt.content = original_design.content;
      const auto recovery_name = std::string(expired ? "expired_" : "active_") +
          (protocol_bound ? "protocol_anchors" : "anchors");
      independent_attempt.content["groups"][0]["name"] = recovery_name;
      independent_attempt.limitations = {"Synthetic fixture; not independent experiment evidence"};
      const auto interrupted_design_id = EvidenceManager(store).collect(independent_attempt);
      auto request = request_for("reference-recovery-" + recovery_name);
      request.policy.polynomial_reference_design_ids = {interrupted_design_id};
      std::string recovery_freeze_id;
      if (protocol_bound) {
        const auto proposal_record = evidence_artifact_from_json(store.get(proposal_id).payload);
        contracts::Json trial_groups = contracts::Json::array();
        for (const auto &group : independent_attempt.content.at("groups")) {
          trial_groups.push_back({{"independence_group", group.at("name")},
              {"inputs", group.at("inputs")}, {"expected_outputs", group.at("expected_outputs")}});
        }
        EvidenceInput protocol_input;
        protocol_input.subject_id = "synthetic-recovery-protocol-candidate";
        protocol_input.category = "POLYNOMIAL_PROTOCOL_DESIGN";
        protocol_input.producer = "synthetic-protocol-recovery-test";
        protocol_input.method = "SYNTHETIC_PROTOCOL_LINKAGE_FIXTURE";
        protocol_input.source_snapshot_hash = original_design.source_snapshot_hash;
        protocol_input.limitations = {"Synthetic fixture, not approved qualification evidence"};
        protocol_input.content = {{"kind", "POLYNOMIAL_PROTOCOL_DESIGN_FREEZE_V1"},
            {"snapshot_id", proposal_record.content.at("snapshot_id")},
            {"protocol_design", {
                {"protocol_version", internet_polynomial_protocol_version},
                {"trial_groups", trial_groups},
                {"source_provenance", {{"grounded", {
                    {"reference_design_evidence_id", interrupted_design_id},
                    {"candidate_ir_sha256", independent_attempt.content.at("candidate_ir_sha256")},
                    {"source_fragment_id", proposal_record.content.at("fragment_id")},
                    {"source_body_sha256", proposal_record.source_snapshot_hash},
                    {"reference_oracle_ir", chebyshev_reference_oracle_descriptor(2)}}}}}}}};
        recovery_freeze_id = EvidenceManager(store).collect(protocol_input);
        request.policy.polynomial_reference_protocol_freeze_ids = {{interrupted_design_id, recovery_freeze_id}};
      }
      const auto plan = orchestrator.plan(request);
      REQUIRE(plan.actions.size() == 1U);
      InternetImprovementRun interrupted;
      interrupted.plan_id = internet.register_improvement_plan(plan);
      interrupted.worker_id = request.worker_id;
      interrupted.started_at = "2026-09-08T00:00:20Z";
      interrupted.requested_budgets = to_json(request.policy);
      interrupted = canonical_internet_improvement_run(interrupted);
      const auto run_id = internet.register_improvement_run(interrupted);
      InternetImprovementActionLease lease;
      lease.action_key = plan.actions.front().action_key;
      lease.run_id = run_id;
      lease.worker_id = request.worker_id;
      lease.acquired_at = interrupted.started_at;
      lease.expires_at = expired ? "2026-09-08T00:00:25Z" : "2026-09-08T00:00:40Z";
      lease = canonical_internet_improvement_action_lease(lease);
      static_cast<void>(internet.register_improvement_action_lease(lease));
      // Execute after acquiring the fixture lease, but deliberately omit the
      // action receipt to model interruption after durable output collection.
      const auto interrupted_result_id = chebyshev_comparison_work(store, interrupted_design_id,
          false, proposal_id, recovery_freeze_id);
      const auto evidence_count = store.list("egcf-evidence").size();
      auto wrong_plan = plan;
      wrong_plan.actions.front().parameters["proposal_scope_id"] = "wrong-proposal";
      wrong_plan.actions.front() = canonical_internet_directed_action(wrong_plan.actions.front());
      wrong_plan = canonical_internet_improvement_plan(wrong_plan);
      auto wrong_run = interrupted;
      wrong_run.plan_id = internet.register_improvement_plan(wrong_plan);
      wrong_run = canonical_internet_improvement_run(wrong_run);
      const auto wrong_id = internet.register_improvement_run(wrong_run);
      const auto before_rejection = store.event_head();
      REQUIRE_THROWS_WITH(orchestrator.resume(wrong_id, request),
          "CHEBYSHEV_COMPARISON_PROPOSAL_SCOPE_MISMATCH");
      CHECK(store.event_head() == before_rejection);
      const auto recovered = orchestrator.resume(run_id, request);
      CHECK(recovered.output_ids == std::vector<std::string>{interrupted_result_id});
      CHECK_FALSE(recovered.action_receipt_id.empty());
      CHECK(store.list("egcf-evidence").size() == evidence_count);
      CHECK(internet.terminal_action_receipt(lease.action_key).has_value());
      if (protocol_bound) {
        const auto retained = evidence_artifact_from_json(store.get(recovered.output_ids.front()).payload);
        CHECK(retained.content.at("polynomial_protocol_freeze_id") == recovery_freeze_id);
        const auto start = evidence_artifact_from_json(store.get(
            retained.content.at("start_evidence_id").get<std::string>()).payload);
        CHECK(start.content.at("polynomial_protocol_freeze_id") == recovery_freeze_id);
      }
    }
    const auto original = evidence_artifact_from_json(store.get(result_id).payload);
    EvidenceInput duplicate;
    duplicate.subject_id = design_id;
    duplicate.category = original.category;
    duplicate.source_snapshot_hash = original.source_snapshot_hash;
    duplicate.content = original.content;
    duplicate.producer = "deliberate-ambiguity-test";
    duplicate.method = original.method;
    static_cast<void>(EvidenceManager(store).collect(duplicate));
    const auto duplicate_head = store.event_head();
    REQUIRE_THROWS(chebyshev_comparison_work(store, design_id, false));
    CHECK(store.event_head() == duplicate_head);
  }
  std::filesystem::remove_all(workspace);
}

TEST_CASE("internet polynomial reference queue is opt in bounded and scope aware") {
  CHECK_FALSE(to_json(InternetDirectorPolicy{}).contains("polynomial_reference_design_ids"));
  CHECK_FALSE(to_json(InternetDirectorPolicy{}).contains("polynomial_proposal_scope_id"));
  CHECK_FALSE(to_json(InternetDirectorPolicy{}).contains("polynomial_reference_protocol_freeze_ids"));
  InternetDirectorPolicy policy;
  policy.polynomial_reference_design_ids = {"design-b", "design-a", "design-a"};
  policy.polynomial_proposal_scope_id = "proposal";
  policy.enabled_action_kinds = {"COMPARE_POLYNOMIAL_REFERENCE"};
  const auto canonical = canonical_internet_director_policy(policy);
  CHECK(canonical.polynomial_reference_design_ids == std::vector<std::string>{"design-a", "design-b"});
  CHECK(to_json(internet_director_policy_from_json(to_json(canonical))) == to_json(canonical));
  policy.polynomial_reference_protocol_freeze_ids = {{"design-a", "freeze-a"}};
  const auto bound = canonical_internet_director_policy(policy);
  CHECK(to_json(internet_director_policy_from_json(to_json(bound))) == to_json(bound));
  policy.polynomial_reference_protocol_freeze_ids = {{"not-queued", "freeze-a"}};
  REQUIRE_THROWS(canonical_internet_director_policy(policy));
  policy.polynomial_reference_protocol_freeze_ids = {{"design-a", ""}};
  REQUIRE_THROWS(canonical_internet_director_policy(policy));
  policy.polynomial_reference_protocol_freeze_ids.clear();
  policy.polynomial_reference_design_ids.resize(33U, "design");
  REQUIRE_THROWS(canonical_internet_director_policy(policy));
}
