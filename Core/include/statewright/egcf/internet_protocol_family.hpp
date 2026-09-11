#pragma once

#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include <exception>
#include <string>

namespace statewright::egcf {

// A scheduling compatibility check, NOT source review or qualification.
// Keep legacy protocol selection unchanged for non-polynomial candidates.
[[nodiscard]] inline bool internet_protocol_family_matches(
    const contracts::Json &candidate_ir, std::string_view candidate_id,
    std::string_view protocol_version, const contracts::Json &provenance) {
  const bool polynomial = candidate_ir.is_object() &&
      candidate_ir.contains("metadata") && candidate_ir.at("metadata").is_object() &&
      candidate_ir.at("metadata").contains("internet_exact_polynomial");
  if (!polynomial) return protocol_version != internet_polynomial_protocol_version;
  if (protocol_version != internet_polynomial_protocol_version || candidate_id.empty())
    return false;
  try {
    const auto program = internet_exact_polynomial_program(candidate_ir);
    if (program.direct_power_sum) return false;
    const auto &ground = provenance.at("grounded");
    return ground.at("candidate_id") == std::string(candidate_id) &&
        ground.at("candidate_ir_sha256") == contracts::sha256_json(candidate_ir) &&
        ground.at("execution_contract_sha256") ==
            contracts::sha256_json(internet_polynomial_contract(program)) &&
        ground.value("review_mode", std::string{}) != "AUTOMATED_CSS_V1";
  } catch (const std::exception &) {
    // Malformed/incomplete protocol material stays deferred. It must not
    // repeatedly consume an execution slot or crash the planning cycle.
    return false;
  }
}

} // namespace statewright::egcf
