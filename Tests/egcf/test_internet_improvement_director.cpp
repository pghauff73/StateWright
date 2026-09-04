#include "statewright/egcf/internet_improvement_director.hpp"

#include "statewright/egcf/internet_records.hpp"
#include "statewright/sources/policy.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("internet improvement director deterministically schedules watches") {
  using namespace statewright;

  const auto source_policy = sources::canonical_source_policy({});
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/director";
  watch.source_policy_id = source_policy.object_id();
  watch.source_group = "example.com";
  watch.accepted_mime_types = source_policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));

  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'd');
  state.planned_at = "2026-09-04T00:00:00Z";
  state.cycle_key = "2026-09-04T00:00:00Z";
  state.active_watch_ids = {watch.object_id()};
  state.internet_records.push_back(
      {.object_id = watch.object_id(),
       .object_type = "internet-watch",
       .digest = {},
       .payload = sources::to_json(watch),
       .relative_path = {}});

  egcf::InternetDirectorPolicy policy;
  policy.action_deadline = "2026-09-04T00:05:00Z";
  const egcf::InternetImprovementDirector director;
  const auto first = director.plan(state, policy);
  const auto repeated = director.plan(state, policy);

  REQUIRE(egcf::to_json(first) == egcf::to_json(repeated));
  REQUIRE(first.actions.size() == 1U);
  REQUIRE(first.actions.front().kind ==
          egcf::InternetDirectedActionKind::schedule_fetch);
  REQUIRE(first.actions.front().subject_id == watch.object_id());
  REQUIRE(first.actions.front().blocked_reasons.empty());
}

TEST_CASE("internet improvement director exposes disabled work as deferred") {
  using namespace statewright;

  const auto source_policy = sources::canonical_source_policy({});
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/disabled";
  watch.source_policy_id = source_policy.object_id();
  watch.source_group = "example.com";
  watch.accepted_mime_types = source_policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));

  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'e');
  state.planned_at = "2026-09-04T00:00:00Z";
  state.cycle_key = "2026-09-04T00:00:00Z";
  state.active_watch_ids = {watch.object_id()};
  state.internet_records.push_back(
      {.object_id = watch.object_id(),
       .object_type = "internet-watch",
       .digest = {},
       .payload = sources::to_json(watch),
       .relative_path = {}});

  egcf::InternetDirectorPolicy policy;
  policy.action_deadline = "2026-09-04T00:05:00Z";
  policy.enabled_action_kinds = {"VERIFY_INTEGRITY"};
  const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);
  REQUIRE(plan.actions.empty());
  REQUIRE(plan.deferred_actions.size() == 1U);
  REQUIRE(plan.deferred_actions.front().blocked_reasons ==
          std::vector<std::string>{"ACTION_DISABLED_BY_POLICY"});
}

TEST_CASE("internet improvement director matches assessment policy lineage") {
  using namespace statewright;

  const auto source_policy = sources::canonical_source_policy({});
  sources::InternetSourcePolicy other_policy_value;
  other_policy_value.request_timeout_seconds = 31;
  const auto other_policy =
      sources::canonical_source_policy(std::move(other_policy_value));

  sources::InternetSourceSnapshot snapshot;
  snapshot.canonical_url = "https://example.com/assessment-lineage";
  snapshot.final_url = snapshot.canonical_url;
  snapshot.body_sha256 = std::string(64U, 'a');
  snapshot.content_type = "text/plain";
  snapshot.body_size = 1U;
  snapshot.artifact_id = "fixture-artifact";
  snapshot.source_group = "example.com";
  snapshot = sources::canonical_source_snapshot(std::move(snapshot));

  sources::InternetFetchReceipt receipt;
  receipt.job_id = "fixture-job";
  receipt.lease_id = "fixture-lease";
  receipt.requested_url = snapshot.canonical_url;
  receipt.final_url = snapshot.final_url;
  receipt.http_status = 200;
  receipt.provider_identity = "fixture-provider";
  receipt.snapshot_id = snapshot.object_id();
  receipt.status = "FETCH_SUCCEEDED";
  receipt = sources::canonical_fetch_receipt(std::move(receipt));

  egcf::InternetSourceAssessmentInput input;
  input.snapshot_id = snapshot.object_id();
  input.fetch_receipt_id = receipt.object_id();
  input.source_policy_id = source_policy.object_id();
  input.robots_allowed = true;
  input.license_classification = "CC0-1.0";
  input.evidence_ids = {"fixture-policy-evidence"};
  input.producer_identity = "fixture-producer";
  input = egcf::canonical_internet_source_assessment_input(std::move(input));

  sources::InternetPolicyAssessment wrong_assessment;
  wrong_assessment.snapshot_id = snapshot.object_id();
  wrong_assessment.fetch_receipt_id = receipt.object_id();
  wrong_assessment.source_policy_id = other_policy.object_id();
  wrong_assessment.public_address_valid = true;
  wrong_assessment.redirects_valid = true;
  wrong_assessment.robots_allowed = true;
  wrong_assessment.license_classification = "CC0-1.0";
  wrong_assessment.mime_valid = true;
  wrong_assessment.encoding_valid = true;
  wrong_assessment.credential_free = true;
  wrong_assessment.size_valid = true;
  wrong_assessment =
      sources::canonical_policy_assessment(std::move(wrong_assessment));

  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'f');
  state.planned_at = "2026-09-04T00:00:00Z";
  state.cycle_key = "2026-09-04T00:00:00Z";
  state.internet_records = {
      {.object_id = snapshot.object_id(),
       .object_type = "internet-source-snapshot",
       .digest = {},
       .payload = sources::to_json(snapshot),
       .relative_path = {}},
      {.object_id = receipt.object_id(),
       .object_type = "internet-fetch-receipt",
       .digest = {},
       .payload = sources::to_json(receipt),
       .relative_path = {}},
      {.object_id = input.object_id(),
       .object_type = "internet-source-assessment-input",
       .digest = {},
       .payload = egcf::to_json(input),
       .relative_path = {}},
      {.object_id = wrong_assessment.object_id(),
       .object_type = "internet-policy-assessment",
       .digest = {},
       .payload = sources::to_json(wrong_assessment),
       .relative_path = {}}};

  egcf::InternetDirectorPolicy policy;
  policy.action_deadline = "2026-09-04T00:05:00Z";
  const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);
  REQUIRE(plan.actions.size() == 1U);
  REQUIRE(plan.actions.front().kind ==
          egcf::InternetDirectedActionKind::assess_source);
  REQUIRE(plan.actions.front().policy_ids ==
          std::vector<std::string>{source_policy.object_id()});
}

TEST_CASE("internet improvement director schedules unscheduled watches fairly") {
  using namespace statewright;

  const auto source_policy = sources::canonical_source_policy({});
  const auto make_watch = [&](std::string url, std::string group) {
    sources::InternetWatch value;
    value.canonical_url = std::move(url);
    value.source_policy_id = source_policy.object_id();
    value.source_group = std::move(group);
    value.accepted_mime_types = source_policy.accepted_mime_types;
    return sources::canonical_watch(std::move(value));
  };
  const auto first =
      make_watch("https://a.example/director", "a.example");
  const auto second =
      make_watch("https://b.example/director", "b.example");
  const auto first_window =
      sources::polling_window(first, "2026-09-04T00:00:00Z");
  const auto first_job = sources::make_fetch_job(
      first, first_window.scheduled_interval, first_window.earliest_start,
      first_window.deadline);
  sources::InternetFetchReceipt first_receipt;
  first_receipt.job_id = first_job.object_id();
  first_receipt.lease_id = "fixture-lease";
  first_receipt.requested_url = first.canonical_url;
  first_receipt.final_url = first.canonical_url;
  first_receipt.http_status = 200;
  first_receipt.provider_identity = "fixture-provider";
  first_receipt.snapshot_id = "fixture-snapshot";
  first_receipt.status = "FETCH_SUCCEEDED";
  first_receipt =
      sources::canonical_fetch_receipt(std::move(first_receipt));

  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'a');
  state.planned_at = "2026-09-04T00:00:00Z";
  state.cycle_key = "cycle-two";
  state.active_watch_ids = {first.object_id(), second.object_id()};
  state.internet_records = {
      {.object_id = first.object_id(),
       .object_type = "internet-watch",
       .digest = {},
       .payload = sources::to_json(first),
       .relative_path = {}},
      {.object_id = second.object_id(),
       .object_type = "internet-watch",
       .digest = {},
       .payload = sources::to_json(second),
       .relative_path = {}},
      {.object_id = first_job.object_id(),
       .object_type = "internet-fetch-job",
       .digest = {},
       .payload = sources::to_json(first_job),
       .relative_path = {}},
      {.object_id = first_receipt.object_id(),
       .object_type = "internet-fetch-receipt",
       .digest = {},
       .payload = sources::to_json(first_receipt),
       .relative_path = {}}};

  egcf::InternetDirectorPolicy policy;
  policy.action_deadline = "2026-09-04T00:05:00Z";
  const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);

  REQUIRE(plan.actions.size() == 1U);
  REQUIRE(plan.actions.front().kind ==
          egcf::InternetDirectedActionKind::schedule_fetch);
  REQUIRE(plan.actions.front().subject_id == second.object_id());
}

TEST_CASE("internet improvement director reasons about quarantined candidates") {
  using namespace statewright;

  egcf::InternetAlgorithmCandidate candidate;
  candidate.source_fragment_id = "fixture-fragment";
  candidate.snapshot_id = "fixture-snapshot";
  candidate.source_policy_assessment_id = "fixture-assessment";
  candidate.retrieval_receipt_id = "fixture-retrieval";
  candidate.status = "QUARANTINED";
  candidate.unresolved_assumptions = {"source semantics remain incomplete"};
  candidate = egcf::canonical_internet_algorithm_candidate(
      std::move(candidate));

  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'b');
  state.planned_at = "2026-09-04T00:00:00Z";
  state.cycle_key = "quarantined-reasoning";
  state.active_candidate_ids = {candidate.object_id()};
  state.internet_records = {
      {.object_id = candidate.object_id(),
       .object_type = "internet-algorithm-candidate",
       .digest = {},
       .payload = egcf::to_json(candidate),
       .relative_path = {}}};

  egcf::InternetDirectorPolicy policy;
  policy.enable_acquisition = false;
  policy.action_deadline = "2026-09-04T00:05:00Z";
  const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);
  REQUIRE(plan.actions.size() == 1U);
  REQUIRE(plan.actions.front().kind ==
          egcf::InternetDirectedActionKind::reason_candidate);
  REQUIRE(plan.actions.front().subject_id == candidate.object_id());
}
