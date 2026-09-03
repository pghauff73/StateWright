#pragma once

#include "statewright/sources/policy.hpp"
#include "statewright/sources/records.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace statewright::sources {

inline constexpr std::string_view internet_extractor_version =
    "statewright-internet-extractor-v1";

struct InternetExtractionLimits final {
  std::size_t maximum_input_bytes = 8U * 1024U * 1024U;
  std::size_t maximum_fragments = 4096U;
  std::size_t maximum_fragment_bytes = 256U * 1024U;
  std::size_t maximum_nesting_depth = 64U;
};

struct InternetExtractionResult final {
  std::vector<InternetSourceFragment> fragments;
  InternetExtractionReceipt receipt;
};

[[nodiscard]] InternetPolicyAssessment assess_internet_source(
    const InternetSourceSnapshot &snapshot,
    const InternetFetchReceipt &fetch_receipt,
    const InternetSourcePolicy &source_policy, std::span<const std::byte> bytes,
    bool robots_allowed, std::string license_classification);

[[nodiscard]] InternetExtractionResult extract_internet_snapshot(
    std::string snapshot_id, std::string content_type,
    std::span<const std::byte> bytes,
    const InternetExtractionLimits &limits = {});

[[nodiscard]] contracts::Json to_json(const InternetExtractionResult &value);

} // namespace statewright::sources
