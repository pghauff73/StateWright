#pragma once

#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/reasoning_service.hpp"
#include "statewright/providers/reasoning_provider.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_reasoning_coordinator_version =
    "statewright-internet-reasoning-coordinator-v1";

struct InternetReasoningResult final {
  InternetReasoningAnalysis analysis;
  InternetAlgorithmCandidate updated_candidate;
  std::string analysis_id;
  std::string updated_candidate_id;
  std::string result_signature;
};

class InternetReasoningCoordinator final {
public:
  explicit InternetReasoningCoordinator(EgcfStore &store);

  [[nodiscard]] InternetReasoningResult analyze(
      const InternetAlgorithmCandidate &candidate,
      const std::vector<sources::InternetSourceFragment> &fragments,
      providers::ReasoningProvider *provider = nullptr,
      std::string provider_identity = "deterministic-fallback",
      std::string model_identity = "none");

private:
  EgcfStore &store_;
  InternetImprovementStore internet_;
  EvidenceManager evidence_;
  Ieps ieps_;
  OiecSrProposalService proposals_;
};

[[nodiscard]] contracts::Json to_json(const InternetReasoningResult &value);

} // namespace statewright::egcf
