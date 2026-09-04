#pragma once

#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/http_provider.hpp"

#include <string>
#include <string_view>

namespace statewright::egcf {

inline constexpr std::string_view internet_source_coordinator_version =
    "statewright-internet-source-coordinator-v1";

struct InternetFetchExecutionResult final {
  std::string fetch_receipt_id;
  std::string snapshot_id;
  std::string artifact_record_id;
  std::string artifact_bytes_id;
  std::string closed_lease_id;
  std::string status;
};

struct InternetSourceAssessmentResult final {
  sources::InternetPolicyAssessment assessment;
  std::string assessment_id;
};

struct InternetSourceExtractionResult final {
  sources::InternetExtractionResult extraction;
  std::string extraction_receipt_id;
};

class InternetSourceCoordinator final {
public:
  explicit InternetSourceCoordinator(EgcfStore &store);

  [[nodiscard]] InternetFetchExecutionResult execute_fetch(
      std::string job_id, std::string lease_id,
      std::string current_timestamp, sources::HttpFetchProvider &provider,
      std::string prior_snapshot_id = {});
  [[nodiscard]] InternetSourceAssessmentResult assess(
      std::string snapshot_id, std::string fetch_receipt_id,
      std::string source_policy_id, bool robots_allowed,
      std::string license_classification);
  [[nodiscard]] InternetSourceExtractionResult extract(
      std::string snapshot_id,
      const sources::InternetExtractionLimits &limits = {});

private:
  EgcfStore &store_;
  InternetImprovementStore internet_;
};

[[nodiscard]] contracts::Json
to_json(const InternetFetchExecutionResult &value);
[[nodiscard]] contracts::Json
to_json(const InternetSourceAssessmentResult &value);
[[nodiscard]] contracts::Json
to_json(const InternetSourceExtractionResult &value);

} // namespace statewright::egcf
