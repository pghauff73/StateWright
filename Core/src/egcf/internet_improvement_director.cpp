#include "statewright/egcf/internet_improvement_director.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_records.hpp"
#include "statewright/egcf/knowledge_governance_store.hpp"
#include "statewright/saa/autonomous_promotion_policy.hpp"
#include "statewright/sources/records.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void director_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

void canonical_strings(std::vector<std::string> &values) {
  for (const auto &value : values) {
    if (value.empty()) {
      director_error("internet director policy values must not be empty");
    }
  }
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <typename Value>
std::map<std::string, Value> parsed_records(
    const InternetImprovementState &state, std::string_view object_type,
    Value (*parser)(const Json &)) {
  std::map<std::string, Value> result;
  for (const auto &object : state.internet_records) {
    if (object.object_type == object_type) {
      result.emplace(object.object_id, parser(object.payload));
    }
  }
  return result;
}

std::map<std::string, StoredObject>
records_of_type(const InternetImprovementState &state,
                std::string_view object_type) {
  std::map<std::string, StoredObject> result;
  for (const auto &object : state.internet_records) {
    if (object.object_type == object_type) {
      result.emplace(object.object_id, object);
    }
  }
  return result;
}

bool contains(const std::vector<std::string> &values, std::string_view value) {
  return std::ranges::find(values, value) != values.end();
}

std::set<std::string> primitive_names(const InternetAlgorithmCandidate &candidate) {
  std::set<std::string> result;
  if (!candidate.proposed_saa_ir.contains("nodes") ||
      !candidate.proposed_saa_ir.at("nodes").is_array()) {
    return result;
  }
  for (const auto &node : candidate.proposed_saa_ir.at("nodes")) {
    if (node.is_object() && node.contains("primitive") &&
        node.at("primitive").is_string()) {
      result.insert(node.at("primitive").get<std::string>());
    }
  }
  return result;
}

bool protocol_applies(const InternetExperimentProtocol &protocol,
                      const InternetAlgorithmCandidate &candidate,
                      std::string_view planned_at) {
  if (!contains(protocol.applicable_candidate_statuses, candidate.status) ||
      planned_at < protocol.valid_from ||
      (!protocol.valid_until.empty() && planned_at > protocol.valid_until)) {
    return false;
  }
  const std::string domain =
      candidate.applicability.value("source_group", std::string{});
  if (!protocol.applicable_domains.empty() &&
      !contains(protocol.applicable_domains, domain)) {
    return false;
  }
  const auto primitives = primitive_names(candidate);
  if (!protocol.applicable_primitives.empty()) {
    if (primitives.empty() ||
        !std::ranges::all_of(primitives, [&](const auto &primitive) {
          return contains(protocol.applicable_primitives, primitive);
        })) {
      return false;
    }
  }
  return true;
}

saa::ImprovementOpportunity opportunity_from_json(const Json &value) {
  saa::ImprovementOpportunity result;
  result.opportunity_id = value.at("opportunity_id").get<std::string>();
  result.kind = value.at("kind").get<std::string>();
  result.source_signature = value.at("source_signature").get<std::string>();
  result.objective = value.at("objective").get<std::string>();
  result.evidence_value_bp = value.at("evidence_value_bp").get<int>();
  result.expected_impact_bp = value.at("expected_impact_bp").get<int>();
  result.uncertainty_reduction_bp =
      value.at("uncertainty_reduction_bp").get<int>();
  result.cost_bp = value.at("cost_bp").get<int>();
  result.risk_bp = value.at("risk_bp").get<int>();
  result.priority_bp = value.at("priority_bp").get<int>();
  result.evidence_ids =
      value.at("evidence_ids").get<std::vector<std::string>>();
  result.independence_groups =
      value.at("independence_groups").get<std::vector<std::string>>();
  result.blocked_reasons =
      value.at("blocked_reasons").get<std::vector<std::string>>();
  result.opportunity_signature =
      value.at("opportunity_signature").get<std::string>();
  return result;
}

InternetDirectedAction make_action(
    const InternetImprovementState &state, const InternetDirectorPolicy &policy,
    InternetDirectedActionKind kind, std::string subject_id,
    std::string subject_type, std::string expected_status,
    int expected_generation, std::vector<std::string> input_ids,
    std::vector<std::string> policy_ids,
    std::vector<std::string> protocol_ids, Json parameters, int priority_bp,
    int cost_bp, int risk_bp, std::size_t response_byte_budget,
    std::size_t cpu_unit_budget, int retry_ceiling,
    std::vector<std::string> blocked_reasons = {}) {
  const std::string kind_name(internet_directed_action_kind_name(kind));
  if (!policy.enabled_action_kinds.empty() &&
      !contains(policy.enabled_action_kinds, kind_name)) {
    blocked_reasons.push_back("ACTION_DISABLED_BY_POLICY");
  }
  return canonical_internet_directed_action(
      {.kind = kind,
       .subject_id = std::move(subject_id),
       .subject_type = std::move(subject_type),
       .expected_status = std::move(expected_status),
       .expected_generation = expected_generation,
       .input_ids = std::move(input_ids),
       .policy_ids = std::move(policy_ids),
       .protocol_ids = std::move(protocol_ids),
       .dependency_action_keys = {},
       .parameters = std::move(parameters),
       .not_before = state.planned_at,
       .deadline = policy.action_deadline.empty() ? state.planned_at
                                                  : policy.action_deadline,
       .priority_bp = priority_bp,
       .cost_bp = cost_bp,
       .risk_bp = risk_bp,
       .response_byte_budget = response_byte_budget,
       .cpu_unit_budget = cpu_unit_budget,
       .retry_ceiling = retry_ceiling,
       .blocked_reasons = std::move(blocked_reasons),
       .action_key = {},
       .action_signature = {}});
}

bool action_order(const InternetDirectedAction &left,
                  const InternetDirectedAction &right) {
  if (left.priority_bp != right.priority_bp) {
    return left.priority_bp > right.priority_bp;
  }
  if (left.not_before != right.not_before) {
    return left.not_before < right.not_before;
  }
  if (left.deadline != right.deadline) {
    return left.deadline < right.deadline;
  }
  if (left.risk_bp != right.risk_bp) {
    return left.risk_bp < right.risk_bp;
  }
  if (left.subject_id != right.subject_id) {
    return left.subject_id < right.subject_id;
  }
  return left.action_key < right.action_key;
}

} // namespace

InternetDirectorPolicy
canonical_internet_director_policy(InternetDirectorPolicy policy) {
  if (policy.maximum_actions <= 0 || policy.maximum_provider_calls < 0 ||
      policy.maximum_cost_bp < 0 || policy.maximum_risk_bp < 0 ||
      policy.scheduler_limits.global_concurrency == 0U ||
      policy.scheduler_limits.per_source_group_concurrency == 0U) {
    director_error("internet director policy limits are invalid");
  }
  canonical_strings(policy.enabled_action_kinds);
  for (const auto &kind : policy.enabled_action_kinds) {
    static_cast<void>(internet_directed_action_kind_from_name(kind));
  }
  policy.improvement_policy =
      saa::canonical_improvement_scheduling_policy(policy.improvement_policy);
  auto material = to_json(policy);
  material.erase("policy_signature");
  policy.policy_signature = contracts::sha256_json(material);
  return policy;
}

InternetDirectorPolicy
internet_director_policy_from_json(const Json &value) {
  InternetDirectorPolicy policy;
  policy.maximum_actions = value.value("maximum_actions", policy.maximum_actions);
  policy.maximum_provider_calls =
      value.value("maximum_provider_calls", policy.maximum_provider_calls);
  policy.maximum_response_bytes =
      value.value("maximum_response_bytes", policy.maximum_response_bytes);
  policy.maximum_cpu_units =
      value.value("maximum_cpu_units", policy.maximum_cpu_units);
  policy.maximum_cost_bp =
      value.value("maximum_cost_bp", policy.maximum_cost_bp);
  policy.maximum_risk_bp =
      value.value("maximum_risk_bp", policy.maximum_risk_bp);
  policy.require_reasoning =
      value.value("require_reasoning", policy.require_reasoning);
  policy.enable_acquisition =
      value.value("enable_acquisition", policy.enable_acquisition);
  policy.enable_candidate_advancement = value.value(
      "enable_candidate_advancement", policy.enable_candidate_advancement);
  policy.candidate_scope_id =
      value.value("candidate_scope_id", std::string{});
  policy.action_deadline = value.value("action_deadline", std::string{});
  policy.promotion_policy_id =
      value.value("promotion_policy_id", std::string{});
  policy.probation_query_signature =
      value.value("probation_query_signature", std::string{});
  policy.enabled_action_kinds = value.value(
      "enabled_action_kinds", std::vector<std::string>{});
  if (value.contains("scheduler_limits")) {
    const auto &limits = value.at("scheduler_limits");
    policy.scheduler_limits.global_concurrency = limits.value(
        "global_concurrency", policy.scheduler_limits.global_concurrency);
    policy.scheduler_limits.per_source_group_concurrency = limits.value(
        "per_source_group_concurrency",
        policy.scheduler_limits.per_source_group_concurrency);
    policy.scheduler_limits.global_response_byte_budget = limits.value(
        "global_response_byte_budget",
        policy.scheduler_limits.global_response_byte_budget);
    policy.scheduler_limits.global_cpu_unit_budget = limits.value(
        "global_cpu_unit_budget",
        policy.scheduler_limits.global_cpu_unit_budget);
    policy.scheduler_limits.maximum_clock_jump_seconds = limits.value(
        "maximum_clock_jump_seconds",
        policy.scheduler_limits.maximum_clock_jump_seconds);
  }
  if (value.contains("improvement_policy")) {
    const auto &improvement = value.at("improvement_policy");
    policy.improvement_policy.max_selected = improvement.value(
        "max_selected", policy.improvement_policy.max_selected);
    policy.improvement_policy.total_cost_budget_bp = improvement.value(
        "total_cost_budget_bp",
        policy.improvement_policy.total_cost_budget_bp);
    policy.improvement_policy.maximum_risk_bp = improvement.value(
        "maximum_risk_bp", policy.improvement_policy.maximum_risk_bp);
    policy.improvement_policy.minimum_priority_bp = improvement.value(
        "minimum_priority_bp",
        policy.improvement_policy.minimum_priority_bp);
  }
  return canonical_internet_director_policy(std::move(policy));
}

InternetImprovementStateReader::InternetImprovementStateReader(EgcfStore &store)
    : store_(store) {}

InternetImprovementState InternetImprovementStateReader::read(
    std::string planned_at, std::string cycle_key) {
  if (planned_at.empty() || cycle_key.empty()) {
    director_error("internet improvement state requires time and cycle key");
  }
  store_.validate_projection();
  const auto checkpoint = store_.projection_checkpoint();
  InternetImprovementState state{
      .event_head = store_.event_head().empty() ? std::string("GENESIS")
                                               : store_.event_head(),
      .projection_digest = checkpoint.authoritative_digest,
      .planned_at = std::move(planned_at),
      .cycle_key = std::move(cycle_key),
      .internet_records = {},
      .active_watch_ids = {},
      .active_candidate_ids = {},
      .active_protocol_ids = {},
      .active_promotion_policy_ids = {},
      .improvement_opportunities = {}};
  for (const auto &object : store_.list()) {
    if (object.object_type.starts_with("internet-")) {
      state.internet_records.push_back(object);
    }
  }
  std::ranges::sort(state.internet_records, {}, &StoredObject::object_id);
  state.active_watch_ids = store_.active_ids("internet-watch");
  state.active_candidate_ids =
      store_.active_ids("internet-algorithm-candidate");
  state.active_protocol_ids =
      store_.active_ids("internet-experiment-protocol");
  state.active_promotion_policy_ids =
      store_.active_ids("internet-promotion-policy");
  KnowledgeGovernanceStore governance(store_);
  state.improvement_opportunities = governance.list_objects(
      "saa_improvement_opportunities", "opportunity_ref");
  return state;
}

InternetImprovementPlan InternetImprovementDirector::plan(
    const InternetImprovementState &state,
    const InternetDirectorPolicy &supplied_policy) const {
  if (state.event_head.empty() || state.projection_digest.empty() ||
      state.planned_at.empty() || state.cycle_key.empty()) {
    director_error("internet improvement state is incomplete");
  }
  const auto policy = canonical_internet_director_policy(supplied_policy);
  const auto watches = parsed_records(
      state, "internet-watch", sources::internet_watch_from_json);
  const auto jobs = parsed_records(
      state, "internet-fetch-job", sources::internet_fetch_job_from_json);
  const auto leases = parsed_records(
      state, "internet-fetch-lease", sources::internet_fetch_lease_from_json);
  const auto receipts = parsed_records(
      state, "internet-fetch-receipt",
      sources::internet_fetch_receipt_from_json);
  const auto snapshots = parsed_records(
      state, "internet-source-snapshot",
      sources::internet_source_snapshot_from_json);
  const auto assessments = parsed_records(
      state, "internet-policy-assessment",
      sources::internet_policy_assessment_from_json);
  const auto assessment_inputs = parsed_records(
      state, "internet-source-assessment-input",
      internet_source_assessment_input_from_json);
  const auto extractions = parsed_records(
      state, "internet-extraction-receipt",
      sources::internet_extraction_receipt_from_json);
  const auto retrievals = records_of_type(state, "internet-retrieval-receipt");
  const auto candidates = parsed_records(
      state, "internet-algorithm-candidate",
      internet_algorithm_candidate_from_json);
  const auto protocols = parsed_records(
      state, "internet-experiment-protocol",
      internet_experiment_protocol_from_json);
  const auto observation_inputs = parsed_records(
      state, "internet-probation-observation-input",
      internet_probation_observation_input_from_json);
  const auto promotion_assessments =
      records_of_type(state, "internet-promotion-assessment");
  const auto terminal_receipts = parsed_records(
      state, "internet-improvement-action-receipt",
      internet_improvement_action_receipt_from_json);

  std::set<std::string> completed_action_keys;
  for (const auto &[id, receipt] : terminal_receipts) {
    static_cast<void>(id);
    completed_action_keys.insert(receipt.action_key);
  }

  std::map<std::string, int> opportunity_priority;
  std::vector<saa::ImprovementOpportunity> opportunities;
  for (const auto &value : state.improvement_opportunities) {
    opportunities.push_back(opportunity_from_json(value));
  }
  if (!opportunities.empty()) {
    const auto schedule =
        saa::schedule_improvements(opportunities, policy.improvement_policy);
    for (const auto &entry : schedule.selected) {
      for (const auto &opportunity : opportunities) {
        if (opportunity.opportunity_id == entry.opportunity_id) {
          opportunity_priority[opportunity.source_signature] =
              std::max(opportunity_priority[opportunity.source_signature],
                       entry.priority_bp);
        }
      }
    }
  }

  std::vector<InternetDirectedAction> proposed;
  const auto add_action = [&](InternetDirectedAction action) {
    if (!completed_action_keys.contains(action.action_key)) {
      proposed.push_back(std::move(action));
    }
  };

  if (policy.enable_acquisition) {
    std::vector<sources::InternetWatch> active_watches;
    for (const auto &watch_id : state.active_watch_ids) {
      const auto iterator = watches.find(watch_id);
      if (iterator != watches.end() && iterator->second.enabled) {
        active_watches.push_back(iterator->second);
      }
    }
    std::vector<sources::InternetFetchJob> existing_jobs;
    for (const auto &[id, job] : jobs) {
      static_cast<void>(id);
      existing_jobs.push_back(job);
    }
    const auto scheduled = sources::schedule_fetch_interval(
        active_watches, existing_jobs, state.cycle_key, state.planned_at,
        policy.action_deadline.empty() ? state.planned_at
                                       : policy.action_deadline);
    for (const auto &job : scheduled) {
      add_action(make_action(
          state, policy, InternetDirectedActionKind::schedule_fetch,
          job.watch_id, "internet-watch", {}, job.expected_watch_generation,
          {job.watch_id}, {}, {}, {{"job", sources::to_json(job)}}, 3500, 500,
          500, 0U, 1U, job.retry_ceiling));
    }

    const auto latest_leases = [&]() {
      std::vector<sources::InternetFetchLease> values;
      for (const auto &[id, lease] : leases) {
        static_cast<void>(id);
        values.push_back(lease);
      }
      return sources::latest_fetch_leases(values);
    }();
    std::map<std::string, sources::InternetFetchLease> latest_by_job;
    for (const auto &lease : latest_leases) {
      latest_by_job.emplace(lease.job_id, lease);
    }
    std::set<std::string> completed_jobs;
    for (const auto &[id, receipt] : receipts) {
      static_cast<void>(id);
      if (receipt.successful()) {
        completed_jobs.insert(receipt.job_id);
      }
    }
    for (const auto &[job_id, job] : jobs) {
      if (completed_jobs.contains(job_id)) {
        continue;
      }
      const auto lease_iterator = latest_by_job.find(job_id);
      if (lease_iterator != latest_by_job.end() &&
          lease_iterator->second.active()) {
        if (lease_iterator->second.expires_at <= state.planned_at) {
          add_action(make_action(
              state, policy,
              InternetDirectedActionKind::recover_expired_lease,
              lease_iterator->second.object_id(), "internet-fetch-lease",
              "ACTIVE", 0,
              {job_id, lease_iterator->second.object_id()}, {}, {},
              {{"job_id", job_id}}, 9000, 100, 100, 0U, 1U,
              job.retry_ceiling));
        }
        continue;
      }
      int attempt_count = 0;
      for (const auto &[lease_id, lease] : leases) {
        static_cast<void>(lease_id);
        if (lease.job_id == job_id && lease.state == "ACTIVE") {
          ++attempt_count;
        }
      }
      std::vector<std::string> blocked;
      if (attempt_count >= job.retry_ceiling + 1) {
        blocked.push_back("RETRY_CEILING_REACHED");
      }
      add_action(make_action(
          state, policy, InternetDirectedActionKind::execute_fetch, job_id,
          "internet-fetch-job", {}, job.expected_watch_generation, {job_id},
          {}, {}, {{"attempt_number", attempt_count + 1}}, 5000, 1500, 2000,
          job.allocated_response_bytes, job.allocated_cpu_units,
          job.retry_ceiling, std::move(blocked)));
    }

    for (const auto &[receipt_id, receipt] : receipts) {
      if (!receipt.successful() || receipt.snapshot_id.empty()) {
        continue;
      }
      std::optional<std::pair<std::string, InternetSourceAssessmentInput>> input;
      for (const auto &[input_id, candidate_input] : assessment_inputs) {
        if (candidate_input.snapshot_id == receipt.snapshot_id &&
            candidate_input.fetch_receipt_id == receipt_id) {
          input = {input_id, candidate_input};
          break;
        }
      }
      const bool already_assessed = std::ranges::any_of(
          assessments, [&](const auto &entry) {
            return entry.second.snapshot_id == receipt.snapshot_id &&
                   entry.second.fetch_receipt_id == receipt_id &&
                   (!input || entry.second.source_policy_id ==
                                  input->second.source_policy_id);
          });
      if (already_assessed) {
        continue;
      }
      std::vector<std::string> blocked;
      if (!input) {
        blocked.push_back("MISSING_SOURCE_POLICY_EVIDENCE");
      }
      Json parameters = Json::object();
      std::vector<std::string> inputs = {receipt_id, receipt.snapshot_id};
      std::vector<std::string> policy_ids;
      if (input) {
        inputs.push_back(input->first);
        policy_ids.push_back(input->second.source_policy_id);
        parameters = {{"license_classification",
                       input->second.license_classification},
                      {"robots_allowed", input->second.robots_allowed}};
      }
      add_action(make_action(
          state, policy, InternetDirectedActionKind::assess_source,
          receipt.snapshot_id, "internet-source-snapshot", {}, 0,
          std::move(inputs), std::move(policy_ids), {}, std::move(parameters),
          6500, 500, 500, 0U, 1U, 0, std::move(blocked)));
    }

    for (const auto &[assessment_id, assessment] : assessments) {
      if (!assessment.admissible()) {
        continue;
      }
      const bool already_extracted = std::ranges::any_of(
          extractions, [&](const auto &entry) {
            return entry.second.snapshot_id == assessment.snapshot_id;
          });
      if (!already_extracted) {
        add_action(make_action(
            state, policy, InternetDirectedActionKind::extract_snapshot,
            assessment.snapshot_id, "internet-source-snapshot", {}, 0,
            {assessment_id, assessment.snapshot_id},
            {assessment.source_policy_id}, {}, Json::object(), 6000, 1000, 500,
            0U, 1U, 0));
      }
    }

    for (const auto &[extraction_id, extraction] : extractions) {
      std::size_t consumed = 0U;
      for (const auto &[retrieval_id, retrieval] : retrievals) {
        static_cast<void>(retrieval_id);
        if (contains(extraction.fragment_ids,
                     retrieval.payload.at("source_fragment_id")
                         .get<std::string>())) {
          ++consumed;
        }
      }
      if (consumed == extraction.fragment_ids.size()) {
        continue;
      }
      std::vector<std::string> blocked;
      if (consumed != 0U) {
        blocked.push_back("PARTIAL_EXTRACTION_ALREADY_FED");
      }
      std::optional<std::string> assessment_id;
      for (const auto &[candidate_id, assessment] : assessments) {
        if (assessment.snapshot_id == extraction.snapshot_id &&
            assessment.admissible()) {
          assessment_id = candidate_id;
          break;
        }
      }
      if (!assessment_id) {
        blocked.push_back("MISSING_ADMISSIBLE_SOURCE_ASSESSMENT");
      }
      std::vector<std::string> inputs = {extraction_id};
      if (assessment_id) {
        inputs.push_back(*assessment_id);
      }
      add_action(make_action(
          state, policy, InternetDirectedActionKind::feed_extraction,
          extraction_id, "internet-extraction-receipt", {}, 0,
          std::move(inputs), {}, {},
          {{"source_label", "internet-source"}, {"strict", true}}, 5500,
          1500, 1000, 0U, 1U, 0, std::move(blocked)));
    }
  }

  if (policy.enable_candidate_advancement) {
    for (const auto &candidate_id : state.active_candidate_ids) {
      if (!policy.candidate_scope_id.empty() &&
          policy.candidate_scope_id != candidate_id) {
        continue;
      }
      const auto candidate_iterator = candidates.find(candidate_id);
      if (candidate_iterator == candidates.end()) {
        continue;
      }
      const auto &candidate = candidate_iterator->second;
      const int opportunity_boost =
          opportunity_priority[candidate.candidate_signature];
      if (candidate.status == "VALIDATION_READY") {
        if (policy.require_reasoning && candidate.reasoning_analysis_ids.empty()) {
          add_action(make_action(
              state, policy, InternetDirectedActionKind::reason_candidate,
              candidate_id, "internet-algorithm-candidate", candidate.status, 0,
              {candidate.source_fragment_id}, {}, {}, Json::object(),
              7000 + opportunity_boost, 1500, 1000, 0U, 1U, 0));
          continue;
        }
        std::optional<std::string> protocol_id;
        for (const auto &active_protocol_id : state.active_protocol_ids) {
          const auto iterator = protocols.find(active_protocol_id);
          if (iterator != protocols.end() &&
              protocol_applies(iterator->second, candidate, state.planned_at)) {
            protocol_id = active_protocol_id;
            break;
          }
        }
        std::vector<std::string> blocked;
        if (!protocol_id) {
          blocked.push_back("MISSING_EXPERIMENT_PROTOCOL");
        }
        add_action(make_action(
            state, policy, InternetDirectedActionKind::qualify_candidate,
            candidate_id, "internet-algorithm-candidate", candidate.status, 0,
            {candidate_id}, {}, protocol_id ? std::vector<std::string>{*protocol_id}
                                           : std::vector<std::string>{},
            Json::object(), 6800 + opportunity_boost, 5000, 3000, 0U, 2U, 0,
            std::move(blocked)));
        continue;
      }
      if (candidate.status == "EXPERIMENT_QUALIFIED") {
        std::vector<std::string> blocked;
        if (policy.promotion_policy_id.empty() ||
            !contains(state.active_promotion_policy_ids,
                      policy.promotion_policy_id)) {
          blocked.push_back("MISSING_PROMOTION_POLICY");
        } else {
          for (const auto &assessment_id : candidate.promotion_assessment_ids) {
            const auto iterator = promotion_assessments.find(assessment_id);
            if (iterator != promotion_assessments.end() &&
                iterator->second.payload.value("policy_id", std::string{}) ==
                    policy.promotion_policy_id) {
              blocked.push_back("POLICY_ASSESSMENT_UNCHANGED");
              break;
            }
          }
        }
        add_action(make_action(
            state, policy, InternetDirectedActionKind::assess_promotion,
            candidate_id, "internet-algorithm-candidate", candidate.status, 0,
            {candidate_id},
            policy.promotion_policy_id.empty()
                ? std::vector<std::string>{}
                : std::vector<std::string>{policy.promotion_policy_id},
            {}, Json::object(), 7500 + opportunity_boost, 1000, 1000, 0U, 1U,
            0, std::move(blocked)));
        continue;
      }
      if (candidate.status == "POLICY_QUALIFIED") {
        std::vector<std::string> blocked;
        if (!candidate.probation_admission_ids.empty()) {
          blocked.push_back("PROBATION_ALREADY_ADMITTED");
        }
        add_action(make_action(
            state, policy, InternetDirectedActionKind::admit_probation,
            candidate_id, "internet-algorithm-candidate", candidate.status, 0,
            {candidate_id}, {}, {}, Json::object(), 8000 + opportunity_boost,
            2000, 1500, 0U, 1U, 0, std::move(blocked)));
        continue;
      }
      if (candidate.status == "PROBATIONARY_CANONICAL" ||
          candidate.status == "CANONICAL") {
        bool observation_added = false;
        for (const auto &[input_id, input] : observation_inputs) {
          if (input.candidate_id != candidate_id ||
              !contains(candidate.probation_admission_ids, input.admission_id)) {
            continue;
          }
          add_action(make_action(
              state, policy,
              InternetDirectedActionKind::consume_probation_observation,
              candidate_id, "internet-algorithm-candidate", candidate.status, 0,
              {candidate_id, input_id, input.admission_id}, {}, {},
              {{"observation_input_id", input_id}}, 9500 + opportunity_boost,
              1000, 500, 0U, 1U, 0));
          observation_added = true;
        }
        if (!observation_added &&
            candidate.status == "PROBATIONARY_CANONICAL") {
          std::vector<std::string> blocked;
          if (policy.probation_query_signature.empty()) {
            blocked.push_back("WAITING_FOR_QUERY");
          }
          add_action(make_action(
              state, policy,
              InternetDirectedActionKind::select_probation_candidate,
              candidate_id, "internet-algorithm-candidate", candidate.status, 0,
              {candidate_id}, {}, {},
              {{"query_signature", policy.probation_query_signature}}, 4000,
              100, 100, 0U, 1U, 0, std::move(blocked)));
        }
      }
    }
  }

  std::ranges::sort(proposed, action_order);
  InternetImprovementPlan plan{
      .cycle_key = state.cycle_key,
      .baseline_event_head = state.event_head,
      .projection_digest = state.projection_digest,
      .director_policy = to_json(policy),
      .planned_at = state.planned_at,
      .director_version = std::string(internet_improvement_director_version),
      .actions = {},
      .deferred_actions = {},
      .allocated_response_bytes = 0U,
      .allocated_cpu_units = 0U,
      .allocated_provider_calls = 0,
      .allocated_cost_bp = 0,
      .allocated_risk_bp = 0,
      .plan_signature = {}};
  int provider_calls = 0;
  for (auto action : proposed) {
    const bool provider_action =
        action.kind == InternetDirectedActionKind::reason_candidate;
    const bool fits = action.eligible() &&
                      static_cast<int>(plan.actions.size()) <
                          policy.maximum_actions &&
                      plan.allocated_response_bytes +
                              action.response_byte_budget <=
                          policy.maximum_response_bytes &&
                      plan.allocated_cpu_units + action.cpu_unit_budget <=
                          policy.maximum_cpu_units &&
                      plan.allocated_cost_bp + action.cost_bp <=
                          policy.maximum_cost_bp &&
                      plan.allocated_risk_bp + action.risk_bp <=
                          policy.maximum_risk_bp &&
                      (!provider_action ||
                       provider_calls < policy.maximum_provider_calls);
    if (fits) {
      plan.allocated_response_bytes += action.response_byte_budget;
      plan.allocated_cpu_units += action.cpu_unit_budget;
      plan.allocated_cost_bp += action.cost_bp;
      plan.allocated_risk_bp += action.risk_bp;
      if (provider_action) {
        ++provider_calls;
      }
      plan.actions.push_back(std::move(action));
      continue;
    }
    if (action.eligible()) {
      action.blocked_reasons.push_back("RUN_BUDGET_EXCEEDED");
      action = canonical_internet_directed_action(std::move(action));
    }
    plan.deferred_actions.push_back(std::move(action));
  }
  plan.allocated_provider_calls = provider_calls;
  return canonical_internet_improvement_plan(std::move(plan));
}

Json to_json(const InternetDirectorPolicy &value) {
  return {{"action_deadline", value.action_deadline},
          {"candidate_scope_id", value.candidate_scope_id},
          {"enable_acquisition", value.enable_acquisition},
          {"enable_candidate_advancement",
           value.enable_candidate_advancement},
          {"enabled_action_kinds", value.enabled_action_kinds},
          {"improvement_policy",
           {{"max_selected", value.improvement_policy.max_selected},
            {"maximum_risk_bp", value.improvement_policy.maximum_risk_bp},
            {"minimum_priority_bp",
             value.improvement_policy.minimum_priority_bp},
            {"total_cost_budget_bp",
             value.improvement_policy.total_cost_budget_bp}}},
          {"maximum_actions", value.maximum_actions},
          {"maximum_cost_bp", value.maximum_cost_bp},
          {"maximum_cpu_units", value.maximum_cpu_units},
          {"maximum_provider_calls", value.maximum_provider_calls},
          {"maximum_response_bytes", value.maximum_response_bytes},
          {"maximum_risk_bp", value.maximum_risk_bp},
          {"policy_signature", value.policy_signature},
          {"probation_query_signature", value.probation_query_signature},
          {"promotion_policy_id", value.promotion_policy_id},
          {"require_reasoning", value.require_reasoning},
          {"scheduler_limits",
           {{"global_concurrency",
             value.scheduler_limits.global_concurrency},
            {"global_cpu_unit_budget",
             value.scheduler_limits.global_cpu_unit_budget},
            {"global_response_byte_budget",
             value.scheduler_limits.global_response_byte_budget},
            {"maximum_clock_jump_seconds",
             value.scheduler_limits.maximum_clock_jump_seconds},
            {"per_source_group_concurrency",
             value.scheduler_limits.per_source_group_concurrency}}}};
}

Json to_json(const InternetImprovementState &value) {
  Json records = Json::array();
  for (const auto &record : value.internet_records) {
    records.push_back(to_json(record));
  }
  return {{"active_candidate_ids", value.active_candidate_ids},
          {"active_promotion_policy_ids",
           value.active_promotion_policy_ids},
          {"active_protocol_ids", value.active_protocol_ids},
          {"active_watch_ids", value.active_watch_ids},
          {"cycle_key", value.cycle_key},
          {"event_head", value.event_head},
          {"improvement_opportunities", value.improvement_opportunities},
          {"internet_records", std::move(records)},
          {"planned_at", value.planned_at},
          {"projection_digest", value.projection_digest}};
}

} // namespace statewright::egcf
