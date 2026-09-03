#pragma once

#include "statewright/egcf/store.hpp"
#include "statewright/saa/adaptation_lineage.hpp"
#include "statewright/saa/algorithm_experiment.hpp"
#include "statewright/saa/multistep_evolution.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view adaptation_lineage_store_version =
    "saa-adaptation-lineage-store-v1";
inline constexpr int adaptation_lineage_store_schema_version = 1;

class AdaptationLineageStore final : public saa::AdaptationLineageCatalog {
public:
  explicit AdaptationLineageStore(EgcfStore &egcf_store);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] saa::ReasoningEvidenceResolver evidence_resolver() const;
  [[nodiscard]] std::pair<std::string, std::string> register_candidate(
      const saa::AdaptedAlgorithmCandidate &candidate,
      const saa::AdaptationStep &step,
      std::string source_explanation_signature);
  [[nodiscard]] contracts::Json
  get_candidate(std::string_view candidate_ref) const override;
  [[nodiscard]] std::vector<contracts::Json> candidates();
  [[nodiscard]] std::vector<contracts::Json>
  lineage_edges() const override;
  [[nodiscard]] std::optional<std::string>
  parent(std::string_view candidate_ref) const;
  [[nodiscard]] std::vector<std::string>
  children(std::string_view reference) const;
  [[nodiscard]] std::vector<std::string>
  ancestors(std::string_view candidate_ref) const override;
  [[nodiscard]] bool descends_from(std::string_view candidate_ref,
                                  std::string_view ancestor_ref) const;
  [[nodiscard]] std::string register_promotion(
      const saa::AdaptationPromotionRecord &promotion);
  [[nodiscard]] std::vector<contracts::Json>
  promotions(std::optional<std::string> candidate_ref = std::nullopt) const;
  [[nodiscard]] std::string register_experiment_design(
      const saa::AlgorithmABExperimentDesign &design);
  [[nodiscard]] std::string register_experiment_result(
      const saa::AlgorithmABExperimentResult &result);
  [[nodiscard]] std::vector<contracts::Json> experiments() const;
  [[nodiscard]] std::vector<contracts::Json> experiment_results(
      std::optional<std::string> design_signature = std::nullopt) const;

  void rebuild_projection();

private:
  [[nodiscard]] bool candidate_exists(std::string_view candidate_ref) const;
  void verify_evidence(const std::vector<std::string> &evidence_ids,
                       std::string_view purpose) const;

  EgcfStore &egcf_store_;
  std::filesystem::path state_root_;
  std::filesystem::path root_;
  std::filesystem::path candidate_root_;
  std::filesystem::path edge_root_;
  std::filesystem::path promotion_root_;
  std::filesystem::path experiment_root_;
  std::filesystem::path result_root_;
  std::filesystem::path projection_path_;
};

} // namespace statewright::egcf
