#include "statewright/sources/snapshot.hpp"

#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <span>
#include <utility>

namespace statewright::sources {
namespace {

[[nodiscard]] std::string normalized_content_type(const FetchResponse &response) {
  std::string value = header_value(response, "content-type");
  const auto separator = value.find(';');
  if (separator != std::string::npos) {
    value.erase(separator);
  }
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value.empty() ? "application/octet-stream" : value;
}

[[nodiscard]] std::string body_hash(std::span<const std::byte> body) {
  return contracts::sha256_bytes(body);
}

} // namespace

std::string normalized_response_content_type(const FetchResponse &response) {
  return normalized_content_type(response);
}

InternetSourceSnapshot make_source_snapshot(
    const FetchResponse &response, std::string artifact_id,
    std::string source_group) {
  const std::string digest = body_hash(response.body);
  InternetSourceSnapshot snapshot;
  snapshot.canonical_url = response.requested_url;
  snapshot.final_url = response.final_url;
  snapshot.body_sha256 = digest;
  snapshot.content_type = normalized_response_content_type(response);
  snapshot.body_size = response.body.size();
  snapshot.artifact_id = std::move(artifact_id);
  snapshot.source_group = std::move(source_group);
  return canonical_source_snapshot(std::move(snapshot));
}

InternetFetchReceipt make_fetch_receipt(std::string job_id,
                                        std::string lease_id,
                                        const FetchResponse &response,
                                        std::string snapshot_id) {
  contracts::Json headers = contracts::Json::object();
  for (const auto &[name, value] : response.headers) {
    headers[name] = value;
  }
  InternetFetchReceipt receipt;
  receipt.job_id = std::move(job_id);
  receipt.lease_id = std::move(lease_id);
  receipt.requested_url = response.requested_url;
  receipt.final_url = response.final_url;
  receipt.resolved_addresses = response.resolved_addresses;
  receipt.redirect_chain = response.redirect_chain;
  receipt.http_status = response.http_status;
  receipt.selected_headers = std::move(headers);
  receipt.tls_verified = response.tls_verified;
  receipt.robots_policy_evaluated = response.robots_policy_evaluated;
  receipt.robots_allowed = response.robots_allowed;
  receipt.robots_evidence = response.robots_evidence;
  receipt.compressed_bytes = response.compressed_bytes;
  receipt.decompressed_bytes = response.decompressed_bytes;
  receipt.total_time_milliseconds = response.total_time_milliseconds;
  receipt.provider_identity = response.provider_identity;
  receipt.snapshot_id = std::move(snapshot_id);
  receipt.status = "FETCH_SUCCEEDED";
  return canonical_fetch_receipt(std::move(receipt));
}

InternetFetchReceipt make_not_modified_fetch_receipt(
    std::string job_id, std::string lease_id, const FetchResponse &response,
    std::string snapshot_id) {
  contracts::Json headers = contracts::Json::object();
  for (const auto &[name, value] : response.headers) {
    headers[name] = value;
  }
  InternetFetchReceipt receipt;
  receipt.job_id = std::move(job_id);
  receipt.lease_id = std::move(lease_id);
  receipt.requested_url = response.requested_url;
  receipt.final_url = response.final_url;
  receipt.resolved_addresses = response.resolved_addresses;
  receipt.redirect_chain = response.redirect_chain;
  receipt.http_status = response.http_status;
  receipt.selected_headers = std::move(headers);
  receipt.tls_verified = response.tls_verified;
  receipt.robots_policy_evaluated = response.robots_policy_evaluated;
  receipt.robots_allowed = response.robots_allowed;
  receipt.robots_evidence = response.robots_evidence;
  receipt.compressed_bytes = response.compressed_bytes;
  receipt.decompressed_bytes = response.decompressed_bytes;
  receipt.total_time_milliseconds = response.total_time_milliseconds;
  receipt.provider_identity = response.provider_identity;
  receipt.snapshot_id = std::move(snapshot_id);
  receipt.status = "NOT_MODIFIED";
  return canonical_fetch_receipt(std::move(receipt));
}

InternetFetchReceipt make_failed_fetch_receipt(
    std::string job_id, std::string lease_id, std::string requested_url,
    std::string provider_identity, std::string reason) {
  InternetFetchReceipt receipt;
  receipt.job_id = std::move(job_id);
  receipt.lease_id = std::move(lease_id);
  receipt.requested_url = std::move(requested_url);
  receipt.provider_identity = std::move(provider_identity);
  receipt.status = "FETCH_FAILED";
  receipt.failure_reason = std::move(reason);
  return canonical_fetch_receipt(std::move(receipt));
}

} // namespace statewright::sources
