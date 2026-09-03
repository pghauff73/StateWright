#pragma once

#include "statewright/sources/fetch.hpp"
#include "statewright/sources/records.hpp"

#include <span>
#include <string>

namespace statewright::sources {

[[nodiscard]] std::string
normalized_response_content_type(const FetchResponse &response);
[[nodiscard]] InternetSourceSnapshot make_source_snapshot(
    const FetchResponse &response, std::string artifact_id,
    std::string source_group);
[[nodiscard]] InternetFetchReceipt make_fetch_receipt(
    std::string job_id, std::string lease_id, const FetchResponse &response,
    std::string snapshot_id);
[[nodiscard]] InternetFetchReceipt make_not_modified_fetch_receipt(
    std::string job_id, std::string lease_id, const FetchResponse &response,
    std::string snapshot_id);
[[nodiscard]] InternetFetchReceipt make_failed_fetch_receipt(
    std::string job_id, std::string lease_id, std::string requested_url,
    std::string provider_identity, std::string reason);

} // namespace statewright::sources
