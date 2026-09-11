#pragma once

#include "statewright/egcf/internet_polynomial_canonical.hpp"

namespace statewright::egcf {

// A catalogue-presence query, not a current source/probation execution decision.
// Exhaustion fails closed: a truncated lookup cannot prove capability absence.
[[nodiscard]] inline contracts::Json search_internet_polynomial_catalog(
    EgcfStore &store, const InternetAlgorithmCandidate &candidate) {
  const auto form = make_internet_polynomial_form(candidate.proposed_saa_ir,
      candidate.semantic_inputs.at(0), candidate.semantic_outputs.at(0));
  const auto signature = form.at("mathematical_signature").get<std::string>();
  const auto matches = store.search_text("\"" + signature + "\"",
      "internet-polynomial-canonical", 33);
  if (matches.size() > 32U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CATALOG_LOOKUP_BUDGET_EXHAUSTED");
  }
  contracts::Json candidates = contracts::Json::array();
  for (const auto &match : matches) {
    const auto canonical = read_internet_polynomial_canonical(store, match.object_id);
    if (canonical.at("form").at("mathematical_signature") != signature) continue;
    const auto origin = store.get(canonical.at("candidate_id").get<std::string>());
    if (origin.payload.at("units") != candidate.units) continue;
    candidates.push_back({{"canonical_id", match.object_id},
        {"representation_kind", "FULL_EXACT_RATIONAL_POLYNOMIAL"},
        {"mathematical_signature", signature},
        {"lifecycle_claim", "RECORDED_REPRESENTATION_ONLY"}});
  }
  contracts::Json result = {
      {"search_version", "internet-polynomial-capability-search-v1"},
      {"mathematical_signature", signature}, {"units", candidate.units},
      {"candidates", candidates}, {"search_complete", true},
      {"selected_canonical_id", nullptr},
      {"status", candidates.empty() ? "NO_MATCH" : "RECORDED_CAPABILITY_MATCH"}};
  result["result_signature"] = contracts::sha256_json(result);
  return result;
}

} // namespace statewright::egcf
