#include "statewright/reasoning/ablation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

constexpr std::string_view pipeline_prefix =
    "super_reasoning_kernel_grounded_topology_v2_task_isolated";

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

} // namespace

const std::vector<std::string> &required_ablation_ids() {
  static const std::vector<std::string> values{
      "one_path_only",
      "without_hypothesis_state",
      "without_verifier",
      "without_falsifier",
      "without_diversity_filter",
      "without_synthesis_verification",
      "without_adaptive_compute",
      "full_sr"};
  return values;
}

contracts::Json to_json(const AblationConfiguration &value) {
  return {{"schema_version", value.schema_version},
          {"ablation_id", value.ablation_id},
          {"path_count", value.path_count},
          {"proposer_batch_size", value.proposer_batch_size},
          {"verifier_batch_size", value.verifier_batch_size},
          {"falsifier_batch_size", value.falsifier_batch_size},
          {"hypothesis_state_enabled", value.hypothesis_state_enabled},
          {"verifier_enabled", value.verifier_enabled},
          {"falsifier_enabled", value.falsifier_enabled},
          {"diversity_filter_enabled", value.diversity_filter_enabled},
          {"synthesis_verification_enabled",
           value.synthesis_verification_enabled},
          {"adaptive_compute_enabled", value.adaptive_compute_enabled},
          {"signature", value.signature}};
}

AblationConfiguration
make_ablation_configuration(AblationConfiguration value) {
  if (value.schema_version != 1) {
    policy_error("ablation configuration schema_version must be 1");
  }
  if (std::ranges::find(required_ablation_ids(), value.ablation_id) ==
      required_ablation_ids().end()) {
    policy_error("invalid qualification ablation: " + value.ablation_id);
  }
  if (value.path_count < 1 || value.path_count > 16) {
    policy_error("ablation path count must be 1..16");
  }
  const std::array<std::pair<std::string_view, int>, 3> batch_sizes{
      {{"proposer batch size", value.proposer_batch_size},
       {"verifier batch size", value.verifier_batch_size},
       {"falsifier batch size", value.falsifier_batch_size}}};
  for (const auto &[name, batch_size] : batch_sizes) {
    if (batch_size < 1 || batch_size > 16) {
      policy_error(std::string(name) + " must be 1..16");
    }
  }
  const std::string supplied = value.signature;
  value.signature.clear();
  auto material = to_json(value);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (!supplied.empty() && supplied != expected) {
    policy_error("ablation configuration signature mismatch");
  }
  value.signature = expected;
  return value;
}

std::vector<AblationConfiguration> standard_ablation_configurations() {
  std::vector<AblationConfiguration> configurations;

  AblationConfiguration one_path;
  one_path.ablation_id = "one_path_only";
  one_path.path_count = 1;
  configurations.push_back(make_ablation_configuration(std::move(one_path)));

  AblationConfiguration no_hypotheses;
  no_hypotheses.ablation_id = "without_hypothesis_state";
  no_hypotheses.hypothesis_state_enabled = false;
  configurations.push_back(
      make_ablation_configuration(std::move(no_hypotheses)));

  AblationConfiguration no_verifier;
  no_verifier.ablation_id = "without_verifier";
  no_verifier.verifier_enabled = false;
  configurations.push_back(
      make_ablation_configuration(std::move(no_verifier)));

  AblationConfiguration no_falsifier;
  no_falsifier.ablation_id = "without_falsifier";
  no_falsifier.falsifier_enabled = false;
  configurations.push_back(
      make_ablation_configuration(std::move(no_falsifier)));

  AblationConfiguration no_diversity;
  no_diversity.ablation_id = "without_diversity_filter";
  no_diversity.diversity_filter_enabled = false;
  configurations.push_back(
      make_ablation_configuration(std::move(no_diversity)));

  AblationConfiguration no_synthesis_verification;
  no_synthesis_verification.ablation_id = "without_synthesis_verification";
  no_synthesis_verification.synthesis_verification_enabled = false;
  configurations.push_back(
      make_ablation_configuration(std::move(no_synthesis_verification)));

  AblationConfiguration no_adaptive_compute;
  no_adaptive_compute.ablation_id = "without_adaptive_compute";
  no_adaptive_compute.adaptive_compute_enabled = false;
  configurations.push_back(
      make_ablation_configuration(std::move(no_adaptive_compute)));

  configurations.push_back(make_ablation_configuration());
  return configurations;
}

std::string ablation_pipeline(const AblationConfiguration &configuration) {
  const auto checked = make_ablation_configuration(configuration);
  return std::string(pipeline_prefix) + ":" + checked.ablation_id + ":" +
         checked.signature;
}

void require_ablation_integrity(const AblationConfiguration &configuration) {
  AblationConfiguration rebuilt = configuration;
  rebuilt.signature.clear();
  if (to_json(make_ablation_configuration(std::move(rebuilt))) !=
      to_json(configuration)) {
    policy_error("ablation configuration integrity check failed");
  }
}

} // namespace statewright::reasoning
