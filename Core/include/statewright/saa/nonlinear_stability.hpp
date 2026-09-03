#pragma once

#include "statewright/saa/nonlinear_search.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_stability_version =
    "saa-nonlinear-semantic-stability-v1";
inline constexpr std::size_t max_regional_observations = 64U;

using ExactInterval = std::pair<mpq_class, mpq_class>;
using ExactBox = std::vector<ExactInterval>;

struct NonlinearRegionalObservation final {
  CanonicalNonlinearRepresentativeForm local_form;
  std::string transform_family_signature;
  std::string evidence_signature;

  [[nodiscard]] const std::vector<mpq_class> &center() const noexcept;
  [[nodiscard]] const std::vector<mpq_class> &validity_radius() const noexcept;
};

struct SemanticStabilityAssessment final {
  int schema_version = 1;
  std::string stability_version = std::string(nonlinear_stability_version);
  std::string parent_representative_behavior_signature;
  std::string status;
  std::size_t observation_count = 0U;
  bool connected_region = false;
  bool meanings_stable = false;
  bool representation_family_stable = false;
  std::vector<std::string> stable_meanings;
  std::vector<std::size_t> transition_coordinates;
  std::vector<std::vector<std::size_t>> adjacency;
  std::vector<ExactBox> regional_boxes;
  std::vector<std::string> local_behavior_signatures;
  std::vector<std::string> transform_family_signatures;
  std::vector<std::string> evidence_signatures;
  bool regional_semantic_eligible = false;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] NonlinearRegionalObservation make_regional_observation(
    const CanonicalNonlinearRepresentativeForm &local_form,
    const NonlinearRepresentativeSearch *search = nullptr,
    std::string evidence_signature = {});
[[nodiscard]] SemanticStabilityAssessment assess_semantic_stability(
    const std::vector<NonlinearRegionalObservation> &observations);

[[nodiscard]] contracts::Json
to_json(const NonlinearRegionalObservation &value);
[[nodiscard]] contracts::Json to_json(const SemanticStabilityAssessment &value);

} // namespace statewright::saa
