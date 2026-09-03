#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/context.hpp"

#include <string>
#include <vector>

namespace statewright::egcf {

struct ExecutionPlan final {
  std::string compiled_workflow_id;
  std::string graph_hash;
  std::string source_snapshot_hash;
  std::vector<std::string> node_order;
  std::vector<std::string> eon_action_ids;
  std::vector<std::string> algorithm_digests;
  std::string capability_grant_id;
  std::vector<std::string> evidence_ids;
  Budget budget;
  contracts::Json rollback_graph = contracts::Json::object();
  std::string approval_policy;
  std::string expires_at;
  std::string created_at;

  [[nodiscard]] std::string object_id() const;
};

struct ApprovalRecord final {
  std::string plan_id;
  std::string plan_hash;
  std::string approver;
  std::string authority;
  contracts::Json constraints = contracts::Json::object();
  std::string created_at;
  std::string expires_at;
  int use_limit = 1;
  int use_count = 0;
  bool human = true;

  [[nodiscard]] std::string object_id() const;
};

struct ExecutionRecord final {
  std::string plan_id;
  std::string node_id;
  std::string algorithm_id;
  std::string executor;
  std::string inputs_hash;
  contracts::Json output;
  std::string status;
  contracts::Json usage = contracts::Json::object();
  std::vector<std::string> evidence_ids;
  std::string started_at;
  std::string completed_at;
  bool simulated = false;

  [[nodiscard]] std::string object_id() const;
};

struct RollbackRecord final {
  std::string plan_id;
  std::vector<std::string> execution_ids;
  std::string rollback_class;
  contracts::Json pre_state = contracts::Json::object();
  contracts::Json post_state = contracts::Json::object();
  contracts::Json restored_state = contracts::Json::object();
  std::vector<std::string> failures;
  std::string status;
  std::string created_at;

  [[nodiscard]] std::string object_id() const;
};

struct FailureRecord final {
  std::string subject_id;
  std::string expected;
  std::string observed;
  std::string active_dimension;
  std::vector<std::string> frozen_dimensions;
  std::vector<std::string> evidence_ids;
  int retry_count = 0;
  std::string status;
  std::string created_at;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] contracts::Json to_json(const ExecutionPlan &plan);
[[nodiscard]] contracts::Json to_json(const ApprovalRecord &approval);
[[nodiscard]] contracts::Json to_json(const ExecutionRecord &execution);
[[nodiscard]] contracts::Json to_json(const RollbackRecord &rollback);
[[nodiscard]] contracts::Json to_json(const FailureRecord &failure);
[[nodiscard]] ExecutionPlan execution_plan_from_json(const contracts::Json &value);
[[nodiscard]] ApprovalRecord approval_from_json(const contracts::Json &value);
[[nodiscard]] ExecutionRecord execution_from_json(const contracts::Json &value);
[[nodiscard]] RollbackRecord rollback_from_json(const contracts::Json &value);
[[nodiscard]] FailureRecord failure_from_json(const contracts::Json &value);

} // namespace statewright::egcf
