#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/saa/oiec_bench_gate.hpp"

namespace statewright::egcf {

// Presence and range diagnostics only. A reported score is not measured truth.
[[nodiscard]] inline contracts::Json internet_polynomial_measurement_coverage(
    const contracts::Json &content) {
  contracts::Json missing = contracts::Json::array();
  const auto scores = content.value("benchmark_track_scores", contracts::Json::object());
  for (const auto track : saa::oiec_bench_tracks) {
    const auto name = std::string(track);
    if (!scores.is_object() || !scores.contains(name) ||
        !scores.at(name).is_number_integer() || scores.at(name) < 0 ||
        scores.at(name) > 10000) {
      missing.push_back(name);
    }
  }
  const bool integrity_present = content.contains("integrity_snapshots") &&
      content.at("integrity_snapshots").is_array() &&
      !content.at("integrity_snapshots").empty();
  return {{"missing_or_invalid_benchmark_tracks", missing},
      {"integrity_snapshots_present", integrity_present},
      {"status", missing.empty() && integrity_present
          ? "REQUIRES_EVIDENCE_VALIDATION" : "MEASUREMENT_FIELDS_INCOMPLETE"},
      {"qualification_claim", "NONE"}};
}

} // namespace statewright::egcf
