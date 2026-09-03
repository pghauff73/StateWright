#include "statewright/egcf/execution.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <string>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void execution_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract,
                      "EGCF execution record: " + std::move(message));
}

} // namespace

std::string ExecutionPlan::object_id() const {
  return contracts::typed_id("execution-plan", to_json(*this));
}

std::string ApprovalRecord::object_id() const {
  return contracts::typed_id("approval", to_json(*this));
}

std::string ExecutionRecord::object_id() const {
  return contracts::typed_id("execution", to_json(*this));
}

std::string RollbackRecord::object_id() const {
  return contracts::typed_id("rollback", to_json(*this));
}

std::string FailureRecord::object_id() const {
  return contracts::typed_id("failure", to_json(*this));
}

contracts::Json to_json(const ExecutionPlan &plan) {
  return {{"algorithm_digests", plan.algorithm_digests},
          {"approval_policy", plan.approval_policy},
          {"budget", to_json(plan.budget)},
          {"capability_grant_id", plan.capability_grant_id},
          {"compiled_workflow_id", plan.compiled_workflow_id},
          {"created_at", plan.created_at},
          {"eon_action_ids", plan.eon_action_ids},
          {"evidence_ids", plan.evidence_ids},
          {"expires_at", plan.expires_at},
          {"graph_hash", plan.graph_hash},
          {"node_order", plan.node_order},
          {"rollback_graph", plan.rollback_graph},
          {"source_snapshot_hash", plan.source_snapshot_hash}};
}

contracts::Json to_json(const ApprovalRecord &approval) {
  return {{"approver", approval.approver},
          {"authority", approval.authority},
          {"constraints", approval.constraints},
          {"created_at", approval.created_at},
          {"expires_at", approval.expires_at},
          {"human", approval.human},
          {"plan_hash", approval.plan_hash},
          {"plan_id", approval.plan_id},
          {"use_count", approval.use_count},
          {"use_limit", approval.use_limit}};
}

contracts::Json to_json(const ExecutionRecord &execution) {
  return {{"algorithm_id", execution.algorithm_id},
          {"completed_at", execution.completed_at},
          {"evidence_ids", execution.evidence_ids},
          {"executor", execution.executor},
          {"inputs_hash", execution.inputs_hash},
          {"node_id", execution.node_id},
          {"output", execution.output},
          {"plan_id", execution.plan_id},
          {"simulated", execution.simulated},
          {"started_at", execution.started_at},
          {"status", execution.status},
          {"usage", execution.usage}};
}

contracts::Json to_json(const RollbackRecord &rollback) {
  return {{"created_at", rollback.created_at},
          {"execution_ids", rollback.execution_ids},
          {"failures", rollback.failures},
          {"plan_id", rollback.plan_id},
          {"post_state", rollback.post_state},
          {"pre_state", rollback.pre_state},
          {"restored_state", rollback.restored_state},
          {"rollback_class", rollback.rollback_class},
          {"status", rollback.status}};
}

contracts::Json to_json(const FailureRecord &failure) {
  return {{"active_dimension", failure.active_dimension},
          {"created_at", failure.created_at},
          {"evidence_ids", failure.evidence_ids},
          {"expected", failure.expected},
          {"frozen_dimensions", failure.frozen_dimensions},
          {"observed", failure.observed},
          {"retry_count", failure.retry_count},
          {"status", failure.status},
          {"subject_id", failure.subject_id}};
}

ExecutionPlan execution_plan_from_json(const contracts::Json &value) {
  try {
    return {.compiled_workflow_id = value.at("compiled_workflow_id"),
            .graph_hash = value.at("graph_hash"),
            .source_snapshot_hash = value.at("source_snapshot_hash"),
            .node_order = value.at("node_order"),
            .eon_action_ids = value.at("eon_action_ids"),
            .algorithm_digests = value.at("algorithm_digests"),
            .capability_grant_id = value.at("capability_grant_id"),
            .evidence_ids = value.at("evidence_ids"),
            .budget = budget_from_json(value.at("budget")),
            .rollback_graph = value.at("rollback_graph"),
            .approval_policy = value.at("approval_policy"),
            .expires_at = value.at("expires_at"),
            .created_at = value.at("created_at")};
  } catch (const std::exception &exception) {
    execution_error(exception.what());
  }
}

ApprovalRecord approval_from_json(const contracts::Json &value) {
  try {
    return {.plan_id = value.at("plan_id"),
            .plan_hash = value.at("plan_hash"),
            .approver = value.at("approver"),
            .authority = value.at("authority"),
            .constraints = value.at("constraints"),
            .created_at = value.at("created_at"),
            .expires_at = value.at("expires_at"),
            .use_limit = value.at("use_limit"),
            .use_count = value.at("use_count"),
            .human = value.at("human")};
  } catch (const std::exception &exception) {
    execution_error(exception.what());
  }
}

ExecutionRecord execution_from_json(const contracts::Json &value) {
  try {
    return {.plan_id = value.at("plan_id"),
            .node_id = value.at("node_id"),
            .algorithm_id = value.at("algorithm_id"),
            .executor = value.at("executor"),
            .inputs_hash = value.at("inputs_hash"),
            .output = value.at("output"),
            .status = value.at("status"),
            .usage = value.at("usage"),
            .evidence_ids = value.at("evidence_ids"),
            .started_at = value.at("started_at"),
            .completed_at = value.at("completed_at"),
            .simulated = value.at("simulated")};
  } catch (const std::exception &exception) {
    execution_error(exception.what());
  }
}

RollbackRecord rollback_from_json(const contracts::Json &value) {
  try {
    return {.plan_id = value.at("plan_id"),
            .execution_ids = value.at("execution_ids"),
            .rollback_class = value.at("rollback_class"),
            .pre_state = value.at("pre_state"),
            .post_state = value.at("post_state"),
            .restored_state = value.at("restored_state"),
            .failures = value.at("failures"),
            .status = value.at("status"),
            .created_at = value.at("created_at")};
  } catch (const std::exception &exception) {
    execution_error(exception.what());
  }
}

FailureRecord failure_from_json(const contracts::Json &value) {
  try {
    return {.subject_id = value.at("subject_id"),
            .expected = value.at("expected"),
            .observed = value.at("observed"),
            .active_dimension = value.at("active_dimension"),
            .frozen_dimensions = value.at("frozen_dimensions"),
            .evidence_ids = value.at("evidence_ids"),
            .retry_count = value.at("retry_count"),
            .status = value.at("status"),
            .created_at = value.at("created_at")};
  } catch (const std::exception &exception) {
    execution_error(exception.what());
  }
}

} // namespace statewright::egcf
