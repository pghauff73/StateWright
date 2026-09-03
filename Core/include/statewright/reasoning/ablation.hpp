#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <string>
#include <vector>

namespace statewright::reasoning {

struct AblationConfiguration final {
  int schema_version = 1;
  std::string ablation_id = "full_sr";
  int path_count = 4;
  int proposer_batch_size = 1;
  int verifier_batch_size = 2;
  int falsifier_batch_size = 1;
  bool hypothesis_state_enabled = true;
  bool verifier_enabled = true;
  bool falsifier_enabled = true;
  bool diversity_filter_enabled = true;
  bool synthesis_verification_enabled = true;
  bool adaptive_compute_enabled = true;
  std::string signature;
};

[[nodiscard]] const std::vector<std::string> &required_ablation_ids();
[[nodiscard]] contracts::Json to_json(const AblationConfiguration &value);
[[nodiscard]] AblationConfiguration
make_ablation_configuration(AblationConfiguration value = {});
[[nodiscard]] std::vector<AblationConfiguration>
standard_ablation_configurations();
[[nodiscard]] std::string
ablation_pipeline(const AblationConfiguration &configuration);
void require_ablation_integrity(const AblationConfiguration &configuration);

} // namespace statewright::reasoning
