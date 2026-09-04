#include "statewright/egcf/internet_source_coordinator.hpp"

#include "statewright/common/error.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"

#include <algorithm>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void source_coordinator_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

} // namespace

InternetSourceCoordinator::InternetSourceCoordinator(EgcfStore &store)
    : store_(store), internet_(store) {}

InternetFetchExecutionResult InternetSourceCoordinator::execute_fetch(
    std::string job_id, std::string lease_id, std::string current_timestamp,
    sources::HttpFetchProvider &provider, std::string prior_snapshot_id) {
  const auto job_record = store_.get(job_id);
  const auto lease_record = store_.get(lease_id);
  if (job_record.object_type != "internet-fetch-job" ||
      lease_record.object_type != "internet-fetch-lease") {
    source_coordinator_error("internet fetch references have invalid types");
  }
  const auto job = sources::internet_fetch_job_from_json(job_record.payload);
  const auto lease = sources::internet_fetch_lease_from_json(lease_record.payload);
  const auto watch_record = store_.get(job.watch_id);
  if (watch_record.object_type != "internet-watch") {
    source_coordinator_error("internet fetch job watch has invalid type");
  }
  const auto watch = sources::internet_watch_from_json(watch_record.payload);
  const auto policy_record = store_.get(watch.source_policy_id);
  if (policy_record.object_type != "internet-source-policy") {
    source_coordinator_error("internet watch policy has invalid type");
  }
  auto policy = sources::source_policy_from_json(policy_record.payload);
  policy.maximum_redirects =
      std::min(policy.maximum_redirects, watch.maximum_redirects);
  policy.maximum_response_bytes =
      std::min(policy.maximum_response_bytes, watch.maximum_response_bytes);
  policy.maximum_decompressed_bytes = std::min(
      policy.maximum_decompressed_bytes, watch.maximum_decompressed_bytes);
  policy.request_timeout_seconds =
      std::min(policy.request_timeout_seconds, watch.request_timeout_seconds);
  policy.accepted_mime_types = watch.accepted_mime_types;
  policy.policy_signature.clear();
  policy = sources::canonical_source_policy(std::move(policy));

  if (lease.job_id != job_id ||
      !sources::latest_lease_is_current(lease, current_timestamp)) {
    source_coordinator_error(
        "internet fetch requires the current active lease");
  }

  try {
    const auto response = provider.fetch(
        {.url = watch.canonical_url,
         .method = "GET",
         .headers = {},
         .policy = policy,
         .cancellation_requested = {}});
    InternetFetchExecutionResult result;
    if (response.http_status == 304) {
      if (prior_snapshot_id.empty()) {
        source_coordinator_error(
            "not-modified fetch requires an explicit prior snapshot");
      }
      result.fetch_receipt_id = internet_.capture_not_modified(
          job_id, lease_id, response, prior_snapshot_id);
      result.snapshot_id = std::move(prior_snapshot_id);
      result.status = "NOT_MODIFIED";
    } else {
      const auto capture = internet_.capture_success(
          job_id, lease_id, response, watch.source_group);
      result.fetch_receipt_id = capture.fetch_receipt_id;
      result.snapshot_id = capture.snapshot_id;
      result.artifact_record_id = capture.artifact_record_id;
      result.artifact_bytes_id = capture.artifact_bytes_id;
      result.status = "FETCH_SUCCEEDED";
    }
    const auto closed = sources::close_fetch_lease(lease, "COMPLETED");
    result.closed_lease_id = internet_.register_fetch_lease(closed);
    return result;
  } catch (const std::exception &error) {
    const std::string receipt_id = internet_.capture_failure(
        job_id, lease_id, watch.canonical_url,
        "http-provider/unavailable-before-response", error.what());
    const auto closed = sources::close_fetch_lease(lease, "ABANDONED");
    static_cast<void>(internet_.register_fetch_lease(closed));
    throw common::Error(common::ErrorCode::internal_failure,
                        "internet fetch failed; receipt " + receipt_id +
                            ": " + error.what());
  }
}

InternetSourceAssessmentResult InternetSourceCoordinator::assess(
    std::string snapshot_id, std::string fetch_receipt_id,
    std::string source_policy_id, bool robots_allowed,
    std::string license_classification) {
  const auto snapshot_record = store_.get(snapshot_id);
  const auto receipt_record = store_.get(fetch_receipt_id);
  const auto policy_record = store_.get(source_policy_id);
  if (snapshot_record.object_type != "internet-source-snapshot" ||
      receipt_record.object_type != "internet-fetch-receipt" ||
      policy_record.object_type != "internet-source-policy") {
    source_coordinator_error("internet source assessment references are invalid");
  }
  const auto assessment = sources::assess_internet_source(
      sources::internet_source_snapshot_from_json(snapshot_record.payload),
      sources::internet_fetch_receipt_from_json(receipt_record.payload),
      sources::source_policy_from_json(policy_record.payload),
      internet_.snapshot_bytes(snapshot_id), robots_allowed,
      std::move(license_classification));
  return {.assessment = assessment,
          .assessment_id = internet_.register_policy_assessment(assessment)};
}

InternetSourceExtractionResult InternetSourceCoordinator::extract(
    std::string snapshot_id, const sources::InternetExtractionLimits &limits) {
  const auto snapshot_record = store_.get(snapshot_id);
  if (snapshot_record.object_type != "internet-source-snapshot") {
    source_coordinator_error("internet extraction snapshot has invalid type");
  }
  const auto snapshot =
      sources::internet_source_snapshot_from_json(snapshot_record.payload);
  const auto extraction = sources::extract_internet_snapshot(
      snapshot_id, snapshot.content_type, internet_.snapshot_bytes(snapshot_id),
      limits);
  return {.extraction = extraction,
          .extraction_receipt_id = internet_.register_extraction(extraction)};
}

contracts::Json to_json(const InternetFetchExecutionResult &value) {
  return {{"artifact_bytes_id", value.artifact_bytes_id},
          {"artifact_record_id", value.artifact_record_id},
          {"closed_lease_id", value.closed_lease_id},
          {"fetch_receipt_id", value.fetch_receipt_id},
          {"snapshot_id", value.snapshot_id},
          {"status", value.status}};
}

contracts::Json to_json(const InternetSourceAssessmentResult &value) {
  return {{"assessment", sources::to_json(value.assessment)},
          {"assessment_id", value.assessment_id}};
}

contracts::Json to_json(const InternetSourceExtractionResult &value) {
  return {{"extraction", sources::to_json(value.extraction)},
          {"extraction_receipt_id", value.extraction_receipt_id}};
}

} // namespace statewright::egcf
