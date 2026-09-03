#pragma once

#include "statewright/egcf/store.hpp"
#include "statewright/saa/reasoning_fit.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view canonical_reasoning_store_version =
    "saa-canonical-reasoning-store-v1";
inline constexpr int canonical_reasoning_store_schema_version = 1;

struct ReasoningStoreLookup final {
  std::string status;
  std::vector<std::string> exact_ids;
  std::vector<std::string> topology_match_ids;
  std::vector<std::string> semantic_match_ids;

  [[nodiscard]] bool unique() const noexcept { return exact_ids.empty(); }
};

struct ReasoningStoreAdmission final {
  std::string status;
  std::string reasoning_id;
  std::string qualification_id;
  int store_generation = 0;
  ReasoningStoreLookup lookup;

  [[nodiscard]] bool admitted_new() const noexcept {
    return status == "ADMITTED_NEW_CANONICAL_REASONING";
  }
};

class CanonicalReasoningStore final : public saa::ReasoningAlgorithmCatalog {
public:
  explicit CanonicalReasoningStore(EgcfStore &egcf_store);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] saa::ReasoningEvidenceResolver evidence_resolver() const;
  [[nodiscard]] int current_generation();
  [[nodiscard]] ReasoningStoreLookup
  lookup(const saa::CanonicalReasoningAlgorithm &algorithm);
  [[nodiscard]] ReasoningStoreAdmission admit(
      const saa::CanonicalReasoningAlgorithm &algorithm,
      const saa::ReasoningOutcomeQualification &qualification);
  [[nodiscard]] contracts::Json get(std::string_view reasoning_id) const;
  [[nodiscard]] saa::CanonicalReasoningAlgorithm
  load_algorithm(std::string_view reasoning_id) const;
  [[nodiscard]] std::vector<std::string>
  list_reasoning_ids() const override;
  [[nodiscard]] saa::CanonicalReasoningAlgorithm
  load_reasoning_algorithm(std::string_view reasoning_id) const override;
  [[nodiscard]] std::vector<contracts::Json> list();
  [[nodiscard]] std::vector<contracts::Json>
  qualifications(std::optional<std::string> reasoning_id = std::nullopt);

  void rebuild_projection();

private:
  void ensure_projection();
  void verify_algorithm(
      const saa::CanonicalReasoningAlgorithm &algorithm) const;
  void verify_qualification(
      const saa::CanonicalReasoningAlgorithm &algorithm,
      const saa::ReasoningOutcomeQualification &qualification) const;
  [[nodiscard]] std::filesystem::path
  algorithm_path(std::string_view reasoning_id) const;
  [[nodiscard]] std::filesystem::path
  qualification_path(std::string_view qualification_id) const;
  [[nodiscard]] std::string persist_qualification(
      std::string_view reasoning_id,
      const saa::ReasoningOutcomeQualification &qualification);
  [[nodiscard]] int generation_for(std::string_view reasoning_id);

  EgcfStore &egcf_store_;
  std::filesystem::path state_root_;
  std::filesystem::path root_;
  std::filesystem::path algorithm_root_;
  std::filesystem::path qualification_root_;
  std::filesystem::path projection_path_;
};

[[nodiscard]] contracts::Json to_json(const ReasoningStoreLookup &value);
[[nodiscard]] contracts::Json to_json(const ReasoningStoreAdmission &value);

} // namespace statewright::egcf
