#include "statewright/sources/records.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace statewright::sources {
namespace {

using Json = contracts::Json;

[[noreturn]] void record_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

void require_nonempty(std::string_view value, std::string_view label) {
  if (value.empty()) {
    record_error(std::string(label) + " must not be empty");
  }
}

void canonical_strings(std::vector<std::string> &values,
                       std::string_view label) {
  for (const auto &value : values) {
    require_nonempty(value, label);
  }
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <typename Value>
std::string signature_for(const Value &value, std::string_view signature_key) {
  Json material = to_json(value);
  material.erase(std::string(signature_key));
  return contracts::sha256_json(material);
}

} // namespace

std::string InternetWatch::object_id() const {
  return contracts::typed_id("internet-watch", to_json(*this));
}

std::string InternetFetchJob::object_id() const {
  return contracts::typed_id("internet-fetch-job", to_json(*this));
}

bool InternetFetchLease::active() const noexcept { return state == "ACTIVE"; }

std::string InternetFetchLease::object_id() const {
  return contracts::typed_id("internet-fetch-lease", to_json(*this));
}

bool InternetFetchReceipt::successful() const noexcept {
  return status == "FETCH_SUCCEEDED" || status == "NOT_MODIFIED";
}

std::string InternetFetchReceipt::object_id() const {
  return contracts::typed_id("internet-fetch-receipt", to_json(*this));
}

std::string InternetSourceSnapshot::object_id() const {
  return contracts::typed_id("internet-source-snapshot", to_json(*this));
}

bool InternetPolicyAssessment::admissible() const noexcept {
  return status == "SOURCE_ADMISSIBLE";
}

std::string InternetPolicyAssessment::object_id() const {
  return contracts::typed_id("internet-policy-assessment", to_json(*this));
}

std::string InternetExtractionReceipt::object_id() const {
  return contracts::typed_id("internet-extraction-receipt", to_json(*this));
}

std::string InternetSourceFragment::object_id() const {
  return contracts::typed_id("internet-source-fragment", to_json(*this));
}

InternetWatch canonical_watch(InternetWatch watch) {
  require_nonempty(watch.canonical_url, "watch canonical URL");
  require_nonempty(watch.source_policy_id, "watch source policy ID");
  require_nonempty(watch.source_group, "watch source group");
  canonical_strings(watch.accepted_mime_types, "accepted MIME type");
  if (watch.accepted_mime_types.empty() || watch.polling_interval_seconds <= 0 ||
      watch.deterministic_jitter_seconds < 0 || watch.maximum_redirects < 0 ||
      watch.maximum_response_bytes == 0U ||
      watch.maximum_decompressed_bytes < watch.maximum_response_bytes ||
      watch.request_timeout_seconds <= 0 || watch.schedule_generation <= 0) {
    record_error("watch limits are invalid");
  }
  watch.watch_signature = signature_for(watch, "watch_signature");
  return watch;
}

InternetFetchJob canonical_fetch_job(InternetFetchJob job) {
  require_nonempty(job.watch_id, "fetch job watch ID");
  require_nonempty(job.scheduled_interval, "fetch job interval");
  require_nonempty(job.earliest_start, "fetch job earliest start");
  require_nonempty(job.deadline, "fetch job deadline");
  require_nonempty(job.priority_class, "fetch job priority class");
  require_nonempty(job.source_group, "fetch job source group");
  if (job.expected_watch_generation <= 0 || job.retry_number < 0 ||
      job.retry_ceiling < job.retry_number || job.opportunity_score < 0 ||
      job.allocated_response_bytes == 0U || job.allocated_cpu_units == 0U) {
    record_error("fetch job limits are invalid");
  }
  job.job_signature = signature_for(job, "job_signature");
  return job;
}

InternetFetchLease canonical_fetch_lease(InternetFetchLease lease) {
  require_nonempty(lease.job_id, "fetch lease job ID");
  require_nonempty(lease.worker_id, "fetch lease worker ID");
  require_nonempty(lease.acquired_at, "fetch lease acquired time");
  require_nonempty(lease.expires_at, "fetch lease expiry");
  static const std::set<std::string> states = {"ACTIVE", "COMPLETED", "EXPIRED",
                                               "ABANDONED"};
  if (!states.contains(lease.state)) {
    record_error("fetch lease state is invalid");
  }
  lease.lease_signature = signature_for(lease, "lease_signature");
  return lease;
}

InternetFetchReceipt canonical_fetch_receipt(InternetFetchReceipt receipt) {
  require_nonempty(receipt.job_id, "fetch receipt job ID");
  require_nonempty(receipt.lease_id, "fetch receipt lease ID");
  require_nonempty(receipt.requested_url, "fetch receipt requested URL");
  require_nonempty(receipt.provider_identity, "fetch receipt provider identity");
  static const std::set<std::string> states = {"FETCH_SUCCEEDED", "FETCH_FAILED",
                                               "NOT_MODIFIED"};
  if (!states.contains(receipt.status)) {
    record_error("fetch receipt status is invalid");
  }
  canonical_strings(receipt.resolved_addresses, "resolved address");
  if (receipt.status == "FETCH_SUCCEEDED") {
    require_nonempty(receipt.final_url, "fetch receipt final URL");
    require_nonempty(receipt.snapshot_id, "fetch receipt snapshot ID");
    if (receipt.http_status < 200 || receipt.http_status >= 300 ||
        !receipt.failure_reason.empty()) {
      record_error("successful fetch receipt cannot contain failure reason");
    }
  } else if (receipt.status == "NOT_MODIFIED") {
    require_nonempty(receipt.final_url, "fetch receipt final URL");
    require_nonempty(receipt.snapshot_id, "fetch receipt snapshot ID");
    if (receipt.http_status != 304 || !receipt.failure_reason.empty() ||
        receipt.decompressed_bytes != 0U) {
      record_error("not-modified fetch receipt is invalid");
    }
  } else if (receipt.status == "FETCH_FAILED") {
    require_nonempty(receipt.failure_reason, "fetch receipt failure reason");
  }
  receipt.receipt_signature = signature_for(receipt, "receipt_signature");
  return receipt;
}

InternetSourceSnapshot canonical_source_snapshot(InternetSourceSnapshot snapshot) {
  require_nonempty(snapshot.canonical_url, "snapshot canonical URL");
  require_nonempty(snapshot.final_url, "snapshot final URL");
  require_nonempty(snapshot.body_sha256, "snapshot body hash");
  require_nonempty(snapshot.content_type, "snapshot content type");
  require_nonempty(snapshot.artifact_id, "snapshot artifact ID");
  require_nonempty(snapshot.source_group, "snapshot source group");
  if (snapshot.body_size == 0U) {
    record_error("snapshot body size is invalid");
  }
  snapshot.snapshot_signature = signature_for(snapshot, "snapshot_signature");
  return snapshot;
}

InternetPolicyAssessment
canonical_policy_assessment(InternetPolicyAssessment assessment) {
  require_nonempty(assessment.snapshot_id, "policy snapshot ID");
  require_nonempty(assessment.fetch_receipt_id, "policy fetch receipt ID");
  require_nonempty(assessment.source_policy_id, "policy source policy ID");
  canonical_strings(assessment.blocking_reasons, "policy blocking reason");
  const bool all_required =
      assessment.public_address_valid && assessment.redirects_valid &&
      assessment.robots_allowed && assessment.mime_valid &&
      assessment.encoding_valid && assessment.credential_free &&
      assessment.size_valid;
  assessment.status = all_required && assessment.blocking_reasons.empty()
                          ? "SOURCE_ADMISSIBLE"
                          : "SOURCE_BLOCKED";
  assessment.assessment_signature =
      signature_for(assessment, "assessment_signature");
  return assessment;
}

InternetExtractionReceipt
canonical_extraction_receipt(InternetExtractionReceipt receipt) {
  require_nonempty(receipt.snapshot_id, "extraction snapshot ID");
  require_nonempty(receipt.decoded_text_signature,
                   "extraction decoded text signature");
  canonical_strings(receipt.extractor_versions, "extractor version");
  canonical_strings(receipt.fragment_ids, "fragment ID");
  canonical_strings(receipt.rejected_fragments, "rejected fragment");
  canonical_strings(receipt.diagnostics, "extraction diagnostic");
  if (receipt.extractor_versions.empty()) {
    record_error("extraction receipt requires an extractor version");
  }
  receipt.extraction_signature = signature_for(receipt, "extraction_signature");
  return receipt;
}

InternetSourceFragment canonical_source_fragment(InternetSourceFragment fragment) {
  require_nonempty(fragment.snapshot_id, "source fragment snapshot ID");
  require_nonempty(fragment.fragment_kind, "source fragment kind");
  require_nonempty(fragment.selector, "source fragment selector");
  require_nonempty(fragment.text, "source fragment text");
  static const std::set<std::string> kinds = {
      "ALGORITHM_DESCRIPTION", "CITATION", "CODE_BLOCK", "CORRECTION",
      "HEADING", "MATH_EXPRESSION", "METADATA", "RETRACTION",
      "TABLE_ROW", "TEXT", "VERSION_MARKER"};
  if (!kinds.contains(fragment.fragment_kind) ||
      fragment.byte_end <= fragment.byte_start || !fragment.metadata.is_object()) {
    record_error("source fragment is invalid");
  }
  fragment.fragment_signature =
      signature_for(fragment, "fragment_signature");
  return fragment;
}

Json to_json(const InternetWatch &value) {
  return {{"accepted_mime_types", value.accepted_mime_types},
          {"canonical_url", value.canonical_url},
          {"deterministic_jitter_seconds", value.deterministic_jitter_seconds},
          {"enabled", value.enabled},
          {"maximum_decompressed_bytes", value.maximum_decompressed_bytes},
          {"maximum_redirects", value.maximum_redirects},
          {"maximum_response_bytes", value.maximum_response_bytes},
          {"polling_interval_seconds", value.polling_interval_seconds},
          {"request_timeout_seconds", value.request_timeout_seconds},
          {"schedule_generation", value.schedule_generation},
          {"schema_version", value.schema_version},
          {"source_group", value.source_group},
          {"source_policy_id", value.source_policy_id},
          {"supersedes_watch_id", value.supersedes_watch_id},
          {"watch_signature", value.watch_signature}};
}

Json to_json(const InternetFetchJob &value) {
  return {{"allocated_cpu_units", value.allocated_cpu_units},
          {"allocated_response_bytes", value.allocated_response_bytes},
          {"deadline", value.deadline},
          {"earliest_start", value.earliest_start},
          {"expected_watch_generation", value.expected_watch_generation},
          {"job_signature", value.job_signature},
          {"opportunity_score", value.opportunity_score},
          {"priority_class", value.priority_class},
          {"retry_ceiling", value.retry_ceiling},
          {"retry_number", value.retry_number},
          {"schema_version", value.schema_version},
          {"scheduled_interval", value.scheduled_interval},
          {"source_group", value.source_group},
          {"watch_id", value.watch_id}};
}

Json to_json(const InternetFetchLease &value) {
  return {{"acquired_at", value.acquired_at},
          {"expires_at", value.expires_at},
          {"job_id", value.job_id},
          {"lease_signature", value.lease_signature},
          {"predecessor_lease_id", value.predecessor_lease_id},
          {"schema_version", value.schema_version},
          {"state", value.state},
          {"worker_id", value.worker_id}};
}

Json to_json(const InternetFetchReceipt &value) {
  return {{"compressed_bytes", value.compressed_bytes},
          {"decompressed_bytes", value.decompressed_bytes},
          {"failure_reason", value.failure_reason},
          {"final_url", value.final_url},
          {"http_status", value.http_status},
          {"job_id", value.job_id},
          {"lease_id", value.lease_id},
          {"provider_identity", value.provider_identity},
          {"receipt_signature", value.receipt_signature},
          {"redirect_chain", value.redirect_chain},
          {"requested_url", value.requested_url},
          {"resolved_addresses", value.resolved_addresses},
          {"schema_version", value.schema_version},
          {"selected_headers", value.selected_headers},
          {"snapshot_id", value.snapshot_id},
          {"status", value.status},
          {"tls_verified", value.tls_verified},
          {"total_time_milliseconds", value.total_time_milliseconds}};
}

Json to_json(const InternetSourceSnapshot &value) {
  return {{"artifact_id", value.artifact_id},
          {"body_sha256", value.body_sha256},
          {"body_size", value.body_size},
          {"canonical_url", value.canonical_url},
          {"content_type", value.content_type},
          {"final_url", value.final_url},
          {"schema_version", value.schema_version},
          {"snapshot_signature", value.snapshot_signature},
          {"source_group", value.source_group}};
}

Json to_json(const InternetPolicyAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"blocking_reasons", value.blocking_reasons},
          {"credential_free", value.credential_free},
          {"encoding_valid", value.encoding_valid},
          {"fetch_receipt_id", value.fetch_receipt_id},
          {"license_classification", value.license_classification},
          {"mime_valid", value.mime_valid},
          {"public_address_valid", value.public_address_valid},
          {"redirects_valid", value.redirects_valid},
          {"robots_allowed", value.robots_allowed},
          {"schema_version", value.schema_version},
          {"size_valid", value.size_valid},
          {"snapshot_id", value.snapshot_id},
          {"source_policy_id", value.source_policy_id},
          {"status", value.status}};
}

Json to_json(const InternetExtractionReceipt &value) {
  return {{"decoded_text_signature", value.decoded_text_signature},
          {"diagnostics", value.diagnostics},
          {"extraction_signature", value.extraction_signature},
          {"extractor_versions", value.extractor_versions},
          {"fragment_ids", value.fragment_ids},
          {"rejected_fragments", value.rejected_fragments},
          {"schema_version", value.schema_version},
          {"snapshot_id", value.snapshot_id},
          {"truncated", value.truncated}};
}

Json to_json(const InternetSourceFragment &value) {
  return {{"byte_end", value.byte_end},
          {"byte_start", value.byte_start},
          {"fragment_kind", value.fragment_kind},
          {"fragment_signature", value.fragment_signature},
          {"language", value.language},
          {"metadata", value.metadata},
          {"schema_version", value.schema_version},
          {"selector", value.selector},
          {"snapshot_id", value.snapshot_id},
          {"text", value.text}};
}

} // namespace statewright::sources
