#include "statewright/egcf/internet_improvement_director.hpp"

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
