#pragma once

#include "statewright/egcf/internet_improvement_store.hpp"

#include <string>
#include <string_view>

namespace statewright::egcf {

inline constexpr std::string_view autonomous_promotion_controller_version =
    "statewright-autonomous-promotion-controller-v1";

struct AutonomousPromotionResult final {
  saa::AutonomousPromotionAssessment assessment;
  InternetAlgorithmCandidate updated_candidate;
  std::string policy_id;
  std::string assessment_id;
  std::string updated_candidate_id;
  std::string result_signature;
};

class AutonomousPromotionController final {
public:
  explicit AutonomousPromotionController(EgcfStore &store);

  [[nodiscard]] AutonomousPromotionResult assess(
      const InternetAlgorithmCandidate &candidate, std::string policy_id);

private:
  EgcfStore &store_;
  InternetImprovementStore internet_;
};

[[nodiscard]] contracts::Json to_json(const AutonomousPromotionResult &value);

} // namespace statewright::egcf
