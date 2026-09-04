#pragma once

#include "statewright/egcf/internet_improvement_director.hpp"
#include "statewright/providers/reasoning_provider.hpp"
#include "statewright/sources/http_provider.hpp"

#include <string>
#include <string_view>

namespace statewright::egcf {

inline constexpr std::string_view internet_improvement_orchestrator_version =
    "statewright-internet-improvement-orchestrator-v1";

struct InternetImprovementRunRequest final {
  std::string cycle_key;
  std::string worker_id;
  std::string current_timestamp;
  std::string action_lease_expires_at;
  std::string fetch_lease_expires_at;
  std::string prior_snapshot_id;
  std::string source_label = "internet-source";
  bool strict_feed = true;
  InternetDirectorPolicy policy;
};

struct InternetImprovementRunResult final {
  InternetImprovementPlan plan;
  std::string plan_id;
  std::string run_id;
  std::string action_key;
  std::string action_lease_id;
  std::string action_receipt_id;
  std::vector<std::string> output_ids;
  std::string status;
  std::string diagnostic;
};

class InternetImprovementOrchestrator final {
public:
  explicit InternetImprovementOrchestrator(
      EgcfStore &store, sources::HttpFetchProvider *fetch_provider = nullptr,
      providers::ReasoningProvider *reasoning_provider = nullptr,
      std::string reasoning_provider_identity = "deterministic-fallback",
      std::string model_identity = "none");

  [[nodiscard]] InternetImprovementPlan
  plan(const InternetImprovementRunRequest &request);
  [[nodiscard]] InternetImprovementRunResult
  run_once(const InternetImprovementRunRequest &request);
  [[nodiscard]] InternetImprovementRunResult
  resume(std::string prior_run_id,
         const InternetImprovementRunRequest &request);
  [[nodiscard]] contracts::Json
  run_status(std::string_view run_id = {}) const;
  [[nodiscard]] contracts::Json
  explain_action(std::string_view action_key) const;

private:
  [[nodiscard]] InternetImprovementRunResult
  run(std::string resume_of_run_id,
      const InternetImprovementRunRequest &request);

  EgcfStore &store_;
  sources::HttpFetchProvider *fetch_provider_;
  providers::ReasoningProvider *reasoning_provider_;
  std::string reasoning_provider_identity_;
  std::string model_identity_;
};

[[nodiscard]] contracts::Json
to_json(const InternetImprovementRunRequest &value);
[[nodiscard]] contracts::Json
to_json(const InternetImprovementRunResult &value);

} // namespace statewright::egcf
