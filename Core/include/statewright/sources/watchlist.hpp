#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/sources/http_provider.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/records.hpp"

#include <string>
#include <string_view>

namespace statewright::sources {

inline constexpr std::string_view internet_watchlist_version =
    "statewright-internet-watchlist-v1";

[[nodiscard]] contracts::Json create_watchlist_manifest(
    const contracts::Json &request, const contracts::Json &source_registry);

void validate_watchlist_manifest(const contracts::Json &manifest,
                                 const contracts::Json &source_registry);

[[nodiscard]] contracts::Json preflight_watchlist_manifest(
    const contracts::Json &manifest, const contracts::Json &source_registry,
    const InternetSourcePolicy &base_policy, HttpFetchProvider &provider,
    std::string checked_at);

[[nodiscard]] InternetSourcePolicy watchlist_source_policy(
    const contracts::Json &entry, InternetSourcePolicy base_policy);

[[nodiscard]] InternetWatch watchlist_watch(const contracts::Json &entry,
                                            std::string source_policy_id,
                                            bool eligible,
                                            bool enable_eligible);

[[nodiscard]] contracts::Json make_watchlist_registration(
    const contracts::Json &manifest, const contracts::Json &entry,
    std::string watch_id, std::string source_policy_id,
    std::string preflight_report_sha256, std::string eligibility_status);

} // namespace statewright::sources
