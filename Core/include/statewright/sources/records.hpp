#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::sources {

inline constexpr std::string_view internet_source_records_version =
    "statewright-internet-source-records-v1";

struct InternetWatch final {
  int schema_version = 1;
  std::string canonical_url;
  bool enabled = true;
  std::string supersedes_watch_id;
  std::string source_policy_id;
  std::string source_group;
  std::vector<std::string> accepted_mime_types;
  int polling_interval_seconds = 3600;
  int deterministic_jitter_seconds = 0;
  int maximum_redirects = 5;
  std::size_t maximum_response_bytes = 8U * 1024U * 1024U;
  std::size_t maximum_decompressed_bytes = 32U * 1024U * 1024U;
  int request_timeout_seconds = 30;
  int schedule_generation = 1;
  std::string watch_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetFetchJob final {
  int schema_version = 1;
  std::string watch_id;
  std::string scheduled_interval;
  int expected_watch_generation = 1;
  std::string earliest_start;
  std::string deadline;
  int retry_number = 0;
  int retry_ceiling = 3;
  std::string priority_class = "NORMAL";
  int opportunity_score = 0;
  std::string source_group;
  std::size_t allocated_response_bytes = 0;
  std::size_t allocated_cpu_units = 1;
  std::string job_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetFetchLease final {
  int schema_version = 1;
  std::string job_id;
  std::string worker_id;
  std::string acquired_at;
  std::string expires_at;
  std::string predecessor_lease_id;
  std::string state;
  std::string lease_signature;

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] std::string object_id() const;
};

struct InternetFetchReceipt final {
  int schema_version = 1;
  std::string job_id;
  std::string lease_id;
  std::string requested_url;
  std::string final_url;
  std::vector<std::string> resolved_addresses;
  std::vector<std::string> redirect_chain;
  int http_status = 0;
  contracts::Json selected_headers = contracts::Json::object();
  bool tls_verified = false;
  bool robots_policy_evaluated = false;
  bool robots_allowed = false;
  contracts::Json robots_evidence = contracts::Json::array();
  std::size_t compressed_bytes = 0;
  std::size_t decompressed_bytes = 0;
  long long total_time_milliseconds = 0;
  std::string provider_identity;
  std::string snapshot_id;
  std::string status;
  std::string failure_reason;
  std::string receipt_signature;

  [[nodiscard]] bool successful() const noexcept;
  [[nodiscard]] std::string object_id() const;
};

struct InternetSourceSnapshot final {
  int schema_version = 1;
  std::string canonical_url;
  std::string final_url;
  std::string body_sha256;
  std::string content_type;
  std::size_t body_size = 0;
  std::string artifact_id;
  std::string source_group;
  std::string snapshot_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetPolicyAssessment final {
  int schema_version = 1;
  std::string snapshot_id;
  std::string fetch_receipt_id;
  std::string source_policy_id;
  bool public_address_valid = false;
  bool redirects_valid = false;
  bool robots_allowed = false;
  std::string license_classification;
  bool mime_valid = false;
  bool encoding_valid = false;
  bool credential_free = true;
  bool size_valid = false;
  std::string status;
  std::vector<std::string> blocking_reasons;
  std::string assessment_signature;

  [[nodiscard]] bool admissible() const noexcept;
  [[nodiscard]] std::string object_id() const;
};

struct InternetExtractionReceipt final {
  int schema_version = 1;
  std::string snapshot_id;
  std::vector<std::string> extractor_versions;
  std::string decoded_text_signature;
  std::vector<std::string> fragment_ids;
  std::vector<std::string> rejected_fragments;
  std::vector<std::string> diagnostics;
  bool truncated = false;
  std::string extraction_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetSourceFragment final {
  int schema_version = 1;
  std::string snapshot_id;
  std::string fragment_kind;
  std::size_t byte_start = 0;
  std::size_t byte_end = 0;
  std::string selector;
  std::string text;
  std::string language;
  contracts::Json metadata = contracts::Json::object();
  std::string fragment_signature;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] InternetWatch canonical_watch(InternetWatch watch);
[[nodiscard]] InternetFetchJob canonical_fetch_job(InternetFetchJob job);
[[nodiscard]] InternetFetchLease canonical_fetch_lease(InternetFetchLease lease);
[[nodiscard]] InternetFetchReceipt
canonical_fetch_receipt(InternetFetchReceipt receipt);
[[nodiscard]] InternetSourceSnapshot
canonical_source_snapshot(InternetSourceSnapshot snapshot);
[[nodiscard]] InternetPolicyAssessment
canonical_policy_assessment(InternetPolicyAssessment assessment);
[[nodiscard]] InternetExtractionReceipt
canonical_extraction_receipt(InternetExtractionReceipt receipt);
[[nodiscard]] InternetSourceFragment
canonical_source_fragment(InternetSourceFragment fragment);

[[nodiscard]] InternetWatch
internet_watch_from_json(const contracts::Json &value);
[[nodiscard]] InternetFetchJob
internet_fetch_job_from_json(const contracts::Json &value);
[[nodiscard]] InternetFetchLease
internet_fetch_lease_from_json(const contracts::Json &value);
[[nodiscard]] InternetFetchReceipt
internet_fetch_receipt_from_json(const contracts::Json &value);
[[nodiscard]] InternetSourceSnapshot
internet_source_snapshot_from_json(const contracts::Json &value);
[[nodiscard]] InternetPolicyAssessment
internet_policy_assessment_from_json(const contracts::Json &value);
[[nodiscard]] InternetExtractionReceipt
internet_extraction_receipt_from_json(const contracts::Json &value);
[[nodiscard]] InternetSourceFragment
internet_source_fragment_from_json(const contracts::Json &value);

[[nodiscard]] contracts::Json to_json(const InternetWatch &value);
[[nodiscard]] contracts::Json to_json(const InternetFetchJob &value);
[[nodiscard]] contracts::Json to_json(const InternetFetchLease &value);
[[nodiscard]] contracts::Json to_json(const InternetFetchReceipt &value);
[[nodiscard]] contracts::Json to_json(const InternetSourceSnapshot &value);
[[nodiscard]] contracts::Json to_json(const InternetPolicyAssessment &value);
[[nodiscard]] contracts::Json to_json(const InternetExtractionReceipt &value);
[[nodiscard]] contracts::Json to_json(const InternetSourceFragment &value);

} // namespace statewright::sources
