#pragma once

#include "statewright/saa/algorithm_adaptation.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view adaptation_lineage_version =
    "saa-adaptation-lineage-v1";
inline constexpr std::size_t max_lineage_depth = 64U;

struct AdaptationLineageEdge final {
  int schema_version = 1;
  std::string lineage_version = std::string(adaptation_lineage_version);
  std::string relation;
  std::string parent_ref;
  std::string child_ref;
  std::string base_algorithm_id;
  std::string component;
  std::string changed_dimension;
  std::string step_signature;
  std::string source_explanation_signature;
  std::string candidate_signature;
  std::string parent_candidate_signature;
  std::string edge_signature;
};

struct AdaptationPromotionRecord final {
  int schema_version = 1;
  std::string lineage_version = std::string(adaptation_lineage_version);
  std::string candidate_ref;
  std::string canonical_algorithm_ref;
  std::string qualification_signature;
  std::vector<std::string> evidence_ids;
  std::string promotion_signature;
};

[[nodiscard]] std::string adapted_candidate_ref(std::string signature);
[[nodiscard]] AdaptationLineageEdge make_adaptation_lineage_edge(
    const AdaptedAlgorithmCandidate &candidate, const AdaptationStep &step,
    std::string source_explanation_signature);
[[nodiscard]] AdaptationPromotionRecord make_adaptation_promotion(
    std::string candidate_ref, std::string canonical_algorithm_ref,
    std::string qualification_signature, std::vector<std::string> evidence_ids);

[[nodiscard]] contracts::Json to_json(const AdaptationLineageEdge &value);
[[nodiscard]] contracts::Json to_json(const AdaptationPromotionRecord &value);

} // namespace statewright::saa
