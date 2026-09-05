#include "statewright/egcf/internet_source_coordinator.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"
#include "statewright/sources/watchlist.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void source_coordinator_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

struct WatchRegistrationEvidence final {
  std::string license_classification;
  std::vector<std::string> license_evidence_urls;
  std::vector<std::string> registration_ids;
  contracts::Json source_review = contracts::Json::object();
};

[[nodiscard]] std::optional<WatchRegistrationEvidence>
watch_registration(EgcfStore &store, std::string_view watch_id) {
  std::optional<WatchRegistrationEvidence> result;
  for (const auto &object : store.list("internet-watch-registration")) {
    if (object.payload.value("watch_id", std::string{}) != watch_id ||
        object.payload.value("license_status", std::string{}) != "verified") {
      continue;
    }
    const std::string classification =
        object.payload.at("license_classification").get<std::string>();
    const auto evidence_urls = object.payload.at("license_evidence_urls")
                                   .get<std::vector<std::string>>();
    const auto source_review =
        object.payload.value("source_review", contracts::Json::object());
    if (result && result->source_review != source_review)
      source_coordinator_error(
          "internet watch has conflicting mathematical source reviews");
    if (result && result->license_classification != classification) {
      source_coordinator_error(
          "internet watch has conflicting verified license provenance");
    }
    if (!result) {
      result =
          WatchRegistrationEvidence{.license_classification = classification,
                                    .license_evidence_urls = {},
                                    .registration_ids = {},
                                    .source_review = source_review};
    }
    result->registration_ids.push_back(object.object_id);
    result->license_evidence_urls.insert(result->license_evidence_urls.end(),
                                         evidence_urls.begin(),
                                         evidence_urls.end());
  }
  if (result) {
    std::ranges::sort(result->registration_ids);
    result->registration_ids.erase(std::unique(result->registration_ids.begin(),
                                               result->registration_ids.end()),
                                   result->registration_ids.end());
    std::ranges::sort(result->license_evidence_urls);
    result->license_evidence_urls.erase(
        std::unique(result->license_evidence_urls.begin(),
                    result->license_evidence_urls.end()),
        result->license_evidence_urls.end());
  }
  return result;
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
  const auto lease =
      sources::internet_fetch_lease_from_json(lease_record.payload);
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
      std::min({policy.maximum_response_bytes, watch.maximum_response_bytes,
                job.allocated_response_bytes});
  policy.maximum_decompressed_bytes = std::min(
      policy.maximum_decompressed_bytes, watch.maximum_decompressed_bytes);
  policy.request_timeout_seconds =
      std::min(policy.request_timeout_seconds, watch.request_timeout_seconds);
  policy.accepted_mime_types = watch.accepted_mime_types;
  policy.policy_signature.clear();
  policy = sources::canonical_source_policy(std::move(policy));

  const auto active_watches = internet_.active_watch_ids();
  const auto latest_lease = internet_.latest_lease(job_id);
  if (!watch.enabled ||
      std::ranges::find(active_watches, job.watch_id) == active_watches.end() ||
      watch.schedule_generation != job.expected_watch_generation ||
      job.source_group != watch.source_group ||
      current_timestamp < job.earliest_start ||
      current_timestamp >= job.deadline) {
    source_coordinator_error(
        "internet fetch watch or polling window is no longer eligible");
  }
  if (lease.job_id != job_id || !latest_lease ||
      latest_lease->object_id() != lease_id ||
      !sources::latest_lease_is_current(lease, current_timestamp)) {
    source_coordinator_error(
        "internet fetch requires the current active lease");
  }

  std::map<std::string, std::string> conditional_headers;
  std::string previous_final_url;
  std::string selected_snapshot_id;
  std::vector<std::pair<std::string, sources::InternetFetchReceipt>>
      previous_receipts;
  // Only validators captured from this source under the same policy may be
  // reused. A scheduling-only generation change preserves that binding.
  for (const auto &record : store_.list("internet-fetch-receipt")) {
    const auto receipt =
        sources::internet_fetch_receipt_from_json(record.payload);
    if (!receipt.successful()) {
      continue;
    }
    if (receipt.job_id == job_id) {
      source_coordinator_error(
          "internet fetch job already has a successful receipt");
    }
    if (receipt.requested_url != watch.canonical_url ||
        receipt.snapshot_id.empty()) {
      continue;
    }
    if (!prior_snapshot_id.empty() &&
        receipt.snapshot_id != prior_snapshot_id) {
      continue;
    }
    const auto prior_job = sources::internet_fetch_job_from_json(
        store_.get(receipt.job_id).payload);
    const auto prior_watch = sources::internet_watch_from_json(
        store_.get(prior_job.watch_id).payload);
    const auto prior_lease = sources::internet_fetch_lease_from_json(
        store_.get(receipt.lease_id).payload);
    if (prior_watch.source_group != watch.source_group ||
        prior_watch.source_policy_id != watch.source_policy_id ||
        prior_lease.acquired_at > current_timestamp) {
      continue;
    }
    previous_receipts.emplace_back(prior_lease.acquired_at, receipt);
  }
  std::ranges::sort(previous_receipts, [](const auto &left, const auto &right) {
    return std::pair{left.first, left.second.object_id()} >
           std::pair{right.first, right.second.object_id()};
  });
  for (const auto &[checked_at, receipt] : previous_receipts) {
    if (selected_snapshot_id.empty()) {
      selected_snapshot_id = receipt.snapshot_id;
      previous_final_url = receipt.final_url;
    }
    if (receipt.snapshot_id != selected_snapshot_id ||
        receipt.final_url != previous_final_url) {
      continue;
    }
    for (const auto &[header, request_header] :
         std::vector<std::pair<std::string, std::string>>{
             {"etag", "If-None-Match"},
             {"last-modified", "If-Modified-Since"}}) {
      const auto value = receipt.selected_headers.value(header, std::string{});
      if (!value.empty() && value.size() <= 8192U &&
          value.find_first_of("\r\n") == std::string::npos) {
        conditional_headers.try_emplace(request_header, value);
      }
    }
  }
  if (!conditional_headers.empty()) {
    prior_snapshot_id = selected_snapshot_id;
  }

  try {
    const auto response = provider.fetch({.url = watch.canonical_url,
                                          .method = "GET",
                                          .headers = conditional_headers,
                                          .policy = policy,
                                          .cancellation_requested = {}});
    const auto registration = watch_registration(store_, job.watch_id);
    if (registration && !registration->source_review.empty()) {
      const auto &review = registration->source_review;
      const auto digest =
          response.http_status == 304 && !prior_snapshot_id.empty()
              ? store_.get(prior_snapshot_id)
                    .payload.at("body_sha256")
                    .get<std::string>()
              : contracts::sha256_bytes(response.body);
      if (!sources::valid_mathematical_source_review(review) ||
          review.at("body_sha256").get<std::string>() != digest)
        source_coordinator_error("PINNED_SOURCE_REVIEW_OR_HASH_INVALID");
    }
    InternetFetchExecutionResult result;
    if (response.http_status == 304) {
      if (prior_snapshot_id.empty() || conditional_headers.empty() ||
          response.final_url != previous_final_url) {
        source_coordinator_error("not-modified fetch requires matching "
                                 "conditional request evidence");
      }
      result.fetch_receipt_id = internet_.capture_not_modified(
          job_id, lease_id, response, prior_snapshot_id);
      result.snapshot_id = std::move(prior_snapshot_id);
      result.status = "NOT_MODIFIED";
    } else {
      const auto capture = internet_.capture_success(job_id, lease_id, response,
                                                     watch.source_group);
      result.fetch_receipt_id = capture.fetch_receipt_id;
      result.snapshot_id = capture.snapshot_id;
      result.artifact_record_id = capture.artifact_record_id;
      result.artifact_bytes_id = capture.artifact_bytes_id;
      result.status = "FETCH_SUCCEEDED";
    }
    const bool robots_authorized =
        !policy.require_robots_permission ||
        (response.robots_policy_evaluated && response.robots_allowed);
    std::string license_classification;
    std::vector<std::string> assessment_evidence = {result.fetch_receipt_id,
                                                    watch.source_policy_id};
    contracts::Json license_provenance = contracts::Json::object();
    if (registration) {
      license_classification = registration->license_classification;
      assessment_evidence.insert(assessment_evidence.end(),
                                 registration->registration_ids.begin(),
                                 registration->registration_ids.end());
      license_provenance = {
          {"registration_ids", registration->registration_ids},
          {"license_evidence_urls", registration->license_evidence_urls}};
    } else if (!policy.require_known_license) {
      license_classification = "UNKNOWN";
    }
    if (!license_classification.empty() && robots_authorized) {
      InternetSourceAssessmentInput input;
      input.snapshot_id = result.snapshot_id;
      input.fetch_receipt_id = result.fetch_receipt_id;
      input.source_policy_id = watch.source_policy_id;
      input.robots_allowed = robots_authorized;
      input.license_classification = std::move(license_classification);
      input.evidence_ids = std::move(assessment_evidence);
      input.producer_identity =
          std::string(internet_source_coordinator_version);
      input.provenance = {
          {"automatic", true},
          {"license", std::move(license_provenance)},
          {"provider_identity", response.provider_identity},
          {"robots_evidence", response.robots_evidence},
          {"robots_policy_evaluated", response.robots_policy_evaluated}};
      result.source_assessment_input_id =
          internet_.register_source_assessment_input(input);
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
                        "internet fetch failed; receipt " + receipt_id + ": " +
                            error.what());
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
    source_coordinator_error(
        "internet source assessment references are invalid");
  }
  auto assessment = sources::assess_internet_source(
      sources::internet_source_snapshot_from_json(snapshot_record.payload),
      sources::internet_fetch_receipt_from_json(receipt_record.payload),
      sources::source_policy_from_json(policy_record.payload),
      internet_.snapshot_bytes(snapshot_id), robots_allowed,
      std::move(license_classification));
  const auto job =
      store_.get(receipt_record.payload.at("job_id").get<std::string>());
  const auto watch_id = job.payload.at("watch_id").get<std::string>();
  for (const auto &registration : store_.list("internet-watch-registration")) {
    if (registration.payload.value("watch_id", std::string{}) != watch_id ||
        !registration.payload.contains("source_review"))
      continue;
    const auto &review = registration.payload.at("source_review");
    if (!sources::valid_mathematical_source_review(review) ||
        review.at("body_sha256") != snapshot_record.payload.at("body_sha256")) {
      assessment.blocking_reasons.push_back(
          "PINNED_SOURCE_REVIEW_OR_HASH_INVALID");
      assessment.status = "QUARANTINED";
      assessment = sources::canonical_policy_assessment(std::move(assessment));
    }
  }
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
  std::string strategy;
  contracts::Json source_review = contracts::Json::object();
  const auto registrations = store_.list("internet-watch-registration");
  for (const auto &record : store_.list("internet-fetch-receipt")) {
    if (record.payload.value("snapshot_id", std::string{}) != snapshot_id)
      continue;
    const auto job = store_.get(record.payload.at("job_id").get<std::string>());
    const auto watch_id = job.payload.at("watch_id").get<std::string>();
    for (const auto &registration : registrations) {
      if (registration.payload.value("watch_id", std::string{}) != watch_id)
        continue;
      const auto declared =
          registration.payload.value("extraction_strategy", std::string{});
      if (declared.empty())
        continue;
      if (!strategy.empty() && declared != strategy) {
        source_coordinator_error(
            "snapshot has conflicting extraction strategies");
      }
      strategy = declared;
      if (registration.payload.contains("source_review")) {
        const auto &review = registration.payload.at("source_review");
        if (!source_review.empty() && source_review != review)
          source_coordinator_error(
              "snapshot has conflicting mathematical source reviews");
        source_review = review;
      }
    }
  }
  const auto extraction = sources::extract_internet_snapshot(
      snapshot_id, snapshot.content_type, internet_.snapshot_bytes(snapshot_id),
      limits, strategy, source_review);
  contracts::Json proposals = contracts::Json::array();
  for (const auto &fragment : extraction.fragments) {
    if (fragment.metadata.contains("discovery_watch_proposal")) {
      auto proposal = fragment.metadata.at("discovery_watch_proposal");
      proposal["source_fragment_id"] = fragment.object_id();
      proposal["discovery_source_group"] = snapshot.source_group;
      proposal["blocking_reasons"] = {"SOURCE_GROUP_ASSIGNMENT_REQUIRED",
                                      "LICENSE_REVIEW_REQUIRED",
                                      "PREFLIGHT_REQUIRED"};
      proposals.push_back(std::move(proposal));
    }
  }
  return {.extraction = extraction,
          .extraction_receipt_id = internet_.register_extraction(extraction),
          .discovery_watch_proposals = std::move(proposals)};
}

contracts::Json to_json(const InternetFetchExecutionResult &value) {
  return {{"artifact_bytes_id", value.artifact_bytes_id},
          {"artifact_record_id", value.artifact_record_id},
          {"closed_lease_id", value.closed_lease_id},
          {"fetch_receipt_id", value.fetch_receipt_id},
          {"snapshot_id", value.snapshot_id},
          {"source_assessment_input_id", value.source_assessment_input_id},
          {"status", value.status}};
}

contracts::Json to_json(const InternetSourceAssessmentResult &value) {
  return {{"assessment", sources::to_json(value.assessment)},
          {"assessment_id", value.assessment_id}};
}

contracts::Json to_json(const InternetSourceExtractionResult &value) {
  return {{"extraction", sources::to_json(value.extraction)},
          {"extraction_receipt_id", value.extraction_receipt_id},
          {"discovery_watch_proposals", value.discovery_watch_proposals}};
}

} // namespace statewright::egcf
