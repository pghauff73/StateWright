#pragma once

#include "statewright/egcf/store.hpp"
#include "statewright/saa/nonlinear_evidence.hpp"
#include "statewright/saa/nonlinear_stability.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view nonlinear_store_version =
    "saa-local-nonlinear-store-v1";
inline constexpr int nonlinear_store_schema_version = 1;

struct NonlinearLocalAdmission final {
  std::string status;
  std::string local_id;
  std::string local_behavior_signature;
  int generation = 0;
  std::string evidence_signature;
};

struct NonlinearRegionalAdmission final {
  std::string status;
  std::string assessment_signature;
  int generation = 0;
};

class NonlinearCanonicalStore final {
public:
  explicit NonlinearCanonicalStore(EgcfStore &egcf_store);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] NonlinearLocalAdmission admit_local(
      const saa::CanonicalNonlinearRepresentativeForm &form,
      const saa::GovernedJetEvidence &evidence);
  [[nodiscard]] NonlinearRegionalAdmission admit_regional_stability(
      const saa::SemanticStabilityAssessment &assessment);
  [[nodiscard]] std::vector<std::string> local_signatures(
      std::optional<std::string> parent_signature = std::nullopt);
  [[nodiscard]] std::vector<contracts::Json> list_local(
      std::optional<std::string> parent_signature = std::nullopt);
  [[nodiscard]] std::vector<std::string> evidence_for_local(
      std::string_view local_behavior_signature);
  [[nodiscard]] std::vector<contracts::Json> list_regional(
      std::optional<std::string> parent_signature = std::nullopt);

  void rebuild_projection();

private:
  void ensure_projection();
  void validate_local_admission(
      const saa::CanonicalNonlinearRepresentativeForm &form,
      const saa::GovernedJetEvidence &evidence) const;
  [[nodiscard]] int generation_for(std::string_view table,
                                   std::string_view identity_column,
                                   std::string_view identity);
  [[nodiscard]] int next_generation(std::string_view table);
  [[nodiscard]] std::filesystem::path local_path(
      std::string_view signature) const;
  [[nodiscard]] std::filesystem::path evidence_path(
      std::string_view signature) const;
  [[nodiscard]] std::filesystem::path regional_path(
      std::string_view signature) const;

  EgcfStore &egcf_store_;
  std::filesystem::path root_;
  std::filesystem::path local_root_;
  std::filesystem::path evidence_root_;
  std::filesystem::path regional_root_;
  std::filesystem::path projection_path_;
};

[[nodiscard]] contracts::Json to_json(const NonlinearLocalAdmission &value);
[[nodiscard]] contracts::Json to_json(const NonlinearRegionalAdmission &value);

} // namespace statewright::egcf
