#include "statewright/egcf/internet_improvement_director.hpp"

#include "statewright/egcf/internet_records.hpp"
#include "statewright/sources/policy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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
  state.internet_records.push_back({.object_id = watch.object_id(),
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
  state.internet_records.push_back({.object_id = watch.object_id(),
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
  state.internet_records = {{.object_id = snapshot.object_id(),
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

TEST_CASE(
    "internet improvement director schedules unscheduled watches fairly") {
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
  const auto first = make_watch("https://a.example/director", "a.example");
  const auto second = make_watch("https://b.example/director", "b.example");
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
  first_receipt = sources::canonical_fetch_receipt(std::move(first_receipt));

  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'a');
  state.planned_at = "2026-09-04T00:00:00Z";
  state.cycle_key = "cycle-two";
  state.active_watch_ids = {first.object_id(), second.object_id()};
  state.internet_records = {{.object_id = first.object_id(),
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

TEST_CASE(
    "internet improvement director reasons about quarantined candidates") {
  using namespace statewright;

  egcf::InternetAlgorithmCandidate candidate;
  candidate.source_fragment_id = "fixture-fragment";
  candidate.snapshot_id = "fixture-snapshot";
  candidate.source_policy_assessment_id = "fixture-assessment";
  candidate.retrieval_receipt_id = "fixture-retrieval";
  candidate.status = "QUARANTINED";
  candidate.unresolved_assumptions = {"source semantics remain incomplete"};
  candidate =
      egcf::canonical_internet_algorithm_candidate(std::move(candidate));

  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'b');
  state.planned_at = "2026-09-04T00:00:00Z";
  state.cycle_key = "quarantined-reasoning";
  state.active_candidate_ids = {candidate.object_id()};
  state.internet_records = {{.object_id = candidate.object_id(),
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

TEST_CASE("internet director gates queued fetches by current watch time and "
          "resources") {
  using namespace statewright;
  const auto source_policy = sources::canonical_source_policy({});
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/queued";
  watch.source_policy_id = source_policy.object_id();
  watch.source_group = "example.com";
  watch.accepted_mime_types = source_policy.accepted_mime_types;
  watch.maximum_response_bytes = 4096U;
  watch = sources::canonical_watch(std::move(watch));
  auto job =
      sources::make_fetch_job(watch, "2026-09-04T00:00:00Z",
                              "2026-09-04T00:00:00Z", "2026-09-04T00:05:00Z");
  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'c');
  state.planned_at = "2026-09-04T00:00:30Z";
  state.cycle_key = "queued-fetch";
  state.active_watch_ids = {watch.object_id()};
  egcf::InternetDirectorPolicy policy;
  policy.action_deadline = "2026-09-04T00:05:00Z";
  policy.enabled_action_kinds = {"EXECUTE_FETCH"};
  std::string expected_reason;
  SECTION("future work") {
    state.planned_at = "2026-09-03T23:59:59Z";
    expected_reason = "NOT_DUE";
  }
  SECTION("deadline is exclusive") {
    state.planned_at = job.deadline;
    expected_reason = "DEADLINE_EXPIRED";
  }
  SECTION("disabled generation") {
    watch.enabled = false;
    watch = sources::canonical_watch(std::move(watch));
    job = sources::make_fetch_job(watch, job.scheduled_interval,
                                  job.earliest_start, job.deadline);
    state.active_watch_ids = {watch.object_id()};
    expected_reason = "WATCH_DISABLED";
  }
  SECTION("superseded watch") {
    state.active_watch_ids.clear();
    expected_reason = "WATCH_NOT_CURRENT";
  }
  SECTION("one byte budget") {
    policy.scheduler_limits.global_response_byte_budget = 1U;
    expected_reason = "GLOBAL_RESPONSE_BYTE_BUDGET";
  }
  SECTION("CPU budget") {
    job.allocated_cpu_units = 2U;
    job = sources::canonical_fetch_job(std::move(job));
    policy.scheduler_limits.global_cpu_unit_budget = 1U;
    expected_reason = "GLOBAL_CPU_BUDGET";
  }
  SECTION("eligible") {}
  state.internet_records = {{.object_id = watch.object_id(),
                             .object_type = "internet-watch",
                             .digest = {},
                             .payload = sources::to_json(watch),
                             .relative_path = {}},
                            {.object_id = job.object_id(),
                             .object_type = "internet-fetch-job",
                             .digest = {},
                             .payload = sources::to_json(job),
                             .relative_path = {}}};
  const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);
  if (expected_reason.empty()) {
    REQUIRE(plan.actions.size() == 1U);
    REQUIRE(plan.actions.front().subject_id == job.object_id());
    REQUIRE(plan.actions.front().deadline == job.deadline);
  } else {
    REQUIRE(plan.actions.empty());
    const auto excluded =
        std::ranges::find_if(plan.deferred_actions, [&](const auto &action) {
          return action.subject_id == job.object_id();
        });
    REQUIRE(excluded != plan.deferred_actions.end());
    REQUIRE(std::ranges::find(excluded->blocked_reasons, expected_reason) !=
            excluded->blocked_reasons.end());
  }
}

TEST_CASE("internet director reserves resources for running jobs from disabled "
          "watches") {
  using namespace statewright;
  const auto policy = sources::canonical_source_policy({});
  egcf::InternetImprovementState state;
  state.planned_at = "2026-09-04T00:00:30Z";
  for (int index = 0; index != 2; ++index) {
    sources::InternetWatch watch;
    watch.canonical_url = "https://example.com/active-" + std::to_string(index);
    watch.source_group = "group-" + std::to_string(index);
    watch.source_policy_id = policy.object_id();
    watch.accepted_mime_types = policy.accepted_mime_types;
    watch.maximum_response_bytes = 4096U;
    watch.enabled = index == 1;
    watch = sources::canonical_watch(std::move(watch));
    state.active_watch_ids.push_back(watch.object_id());
    state.internet_records.push_back({.object_id = watch.object_id(),
                                      .object_type = "internet-watch",
                                      .digest = {},
                                      .payload = sources::to_json(watch),
                                      .relative_path = {}});
    const auto job =
        sources::make_fetch_job(watch, "2026-09-04T00:00:00Z",
                                "2026-09-04T00:00:00Z", "2026-09-04T00:05:00Z");
    state.internet_records.push_back({.object_id = job.object_id(),
                                      .object_type = "internet-fetch-job",
                                      .digest = {},
                                      .payload = sources::to_json(job),
                                      .relative_path = {}});
    if (index == 0) {
      const auto lease = sources::acquire_fetch_lease(job.object_id(), "worker",
                                                      "2026-09-04T00:00:01Z",
                                                      "2026-09-04T00:01:00Z");
      state.internet_records.push_back({.object_id = lease.object_id(),
                                        .object_type = "internet-fetch-lease",
                                        .digest = {},
                                        .payload = sources::to_json(lease),
                                        .relative_path = {}});
    }
  }
  sources::InternetSchedulerLimits limits;
  limits.global_response_byte_budget = 4096U;
  auto selected = egcf::select_eligible_internet_fetch_jobs(state, limits);
  REQUIRE(selected.selected.empty());
  REQUIRE(std::ranges::any_of(selected.excluded, [](const auto &excluded) {
    return excluded.reason == "GLOBAL_RESPONSE_BYTE_BUDGET";
  }));
  limits.global_response_byte_budget = 8192U;
  limits.global_cpu_unit_budget = 1U;
  selected = egcf::select_eligible_internet_fetch_jobs(state, limits);
  REQUIRE(selected.selected.empty());
  REQUIRE(std::ranges::any_of(selected.excluded, [](const auto &excluded) {
    return excluded.reason == "GLOBAL_CPU_BUDGET";
  }));
}

TEST_CASE(
    "internet director retries promotion after a newer source assessment") {
  using namespace statewright;
  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = std::string(64U, 'a');
  state.planned_at = "2026-09-04T00:03:00Z";
  state.cycle_key = "source-revalidated";
  state.active_promotion_policy_ids = {"promotion-policy"};
  const auto source_policy = sources::canonical_source_policy({});
  std::vector<std::string> assessment_ids;
  const auto append = [&](std::string id, std::string type,
                          contracts::Json payload) {
    state.internet_records.push_back({.object_id = std::move(id),
                                      .object_type = std::move(type),
                                      .digest = {},
                                      .payload = std::move(payload),
                                      .relative_path = {}});
  };
  for (int index = 0; index != 2; ++index) {
    const auto lease = sources::acquire_fetch_lease(
        "job-" + std::to_string(index), "worker",
        index == 0 ? "2026-09-04T00:00:00Z" : "2026-09-04T00:02:00Z",
        "2026-09-04T00:04:00Z");
    append(lease.object_id(), "internet-fetch-lease", sources::to_json(lease));
    sources::InternetFetchReceipt receipt;
    receipt.job_id = lease.job_id;
    receipt.lease_id = lease.object_id();
    receipt.requested_url = "https://example.com/revalidated";
    receipt.final_url = receipt.requested_url;
    receipt.http_status = index == 0 ? 200 : 304;
    receipt.provider_identity = "fixture";
    receipt.snapshot_id = "snapshot";
    receipt.status = index == 0 ? "FETCH_SUCCEEDED" : "NOT_MODIFIED";
    receipt = sources::canonical_fetch_receipt(std::move(receipt));
    append(receipt.object_id(), "internet-fetch-receipt",
           sources::to_json(receipt));
    sources::InternetPolicyAssessment assessment;
    assessment.snapshot_id = "snapshot";
    assessment.fetch_receipt_id = receipt.object_id();
    assessment.source_policy_id = source_policy.object_id();
    assessment.license_classification = "CC0-1.0";
    assessment.public_address_valid = assessment.redirects_valid =
        assessment.robots_allowed = assessment.mime_valid =
            assessment.encoding_valid = assessment.credential_free =
                assessment.size_valid = true;
    assessment = sources::canonical_policy_assessment(std::move(assessment));
    assessment_ids.push_back(assessment.object_id());
    append(assessment.object_id(), "internet-policy-assessment",
           sources::to_json(assessment));
  }
  egcf::InternetAlgorithmCandidate candidate;
  candidate.source_fragment_id = "fragment";
  candidate.snapshot_id = "snapshot";
  candidate.source_policy_assessment_id = assessment_ids.front();
  candidate.retrieval_receipt_id = "retrieval";
  candidate.status = "EXPERIMENT_QUALIFIED";
  candidate.promotion_assessment_ids = {"old-promotion"};
  candidate =
      egcf::canonical_internet_algorithm_candidate(std::move(candidate));
  append(candidate.object_id(), "internet-algorithm-candidate",
         egcf::to_json(candidate));
  state.active_candidate_ids = {candidate.object_id()};
  append("old-promotion", "internet-promotion-assessment",
         {{"policy_id", "promotion-policy"},
          {"source_policy_assessment_ref", assessment_ids.front()}});
  egcf::InternetDirectorPolicy policy;
  policy.enable_acquisition = false;
  policy.promotion_policy_id = "promotion-policy";
  policy.action_deadline = "2026-09-04T00:05:00Z";
  SECTION("new same-snapshot evidence permits a retry") {
    const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);
    REQUIRE(plan.actions.size() == 1U);
    REQUIRE(plan.actions.front().kind ==
            egcf::InternetDirectedActionKind::assess_promotion);
    REQUIRE(std::ranges::find(plan.actions.front().input_ids,
                              assessment_ids.back()) !=
            plan.actions.front().input_ids.end());
  }
  SECTION("future revalidation cannot unlock a retry") {
    state.planned_at = "2026-09-04T00:01:00Z";
    const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);
    REQUIRE(plan.actions.empty());
    REQUIRE(plan.deferred_actions.size() == 1U);
    REQUIRE(plan.deferred_actions.front().blocked_reasons ==
            std::vector<std::string>{"POLICY_ASSESSMENT_UNCHANGED"});
  }
}
