#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {

inline constexpr int score_scale = 10'000;

struct Hypothesis final {
  std::string hypothesis_id;
  std::string proposition;
  int prior_bp = 0;
  int posterior_bp = 0;
  std::vector<std::string> supporting_evidence;
  std::vector<std::string> conflicting_evidence;
  std::vector<std::string> assumptions;
  std::vector<std::string> predictions;
  std::vector<std::string> falsifiers;
  std::string status = "ACTIVE";
  std::string signature;
};

struct HypothesisUpdateRecord final {
  int schema_version = 1;
  std::string update_id;
  std::string problem_id;
  std::string hypothesis_id;
  std::string operation = "BAYES_EVIDENCE_UPDATE";
  std::vector<std::string> evidence_ids;
  std::vector<std::string> collision_ids;
  std::string polarity = "support";
  int likelihood_if_true_bp = 0;
  int likelihood_if_false_bp = 0;
  int previous_posterior_bp = 0;
  int updated_posterior_bp = 0;
  std::string previous_status = "ACTIVE";
  std::string updated_status = "ACTIVE";
  std::string previous_hypothesis_signature;
  std::string updated_hypothesis_signature;
  std::string reason;
  std::string signature;
};

struct HypothesisSet final {
  int schema_version = 1;
  std::string problem_id;
  std::vector<Hypothesis> hypotheses;
  int max_hypotheses = 16;
  bool mutually_exclusive = false;
  int uncertainty_bp = 0;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> update_ids;
  std::string signature;
};

struct HypothesisProposal final {
  std::string hypothesis_id;
  std::string proposition;
  int prior_bp = 0;
  std::optional<int> posterior_bp;
  std::vector<std::string> supporting_evidence;
  std::vector<std::string> conflicting_evidence;
  std::vector<std::string> assumptions;
  std::vector<std::string> predictions;
  std::vector<std::string> falsifiers;
  std::string status = "ACTIVE";
};

struct HypothesisStateUpdate final {
  HypothesisSet state;
  std::vector<HypothesisUpdateRecord> records;
};

using LikelihoodMap = std::map<std::string, std::pair<int, int>>;

[[nodiscard]] contracts::Json to_json(const Hypothesis &hypothesis);
[[nodiscard]] contracts::Json to_json(const HypothesisUpdateRecord &record);
[[nodiscard]] contracts::Json to_json(const HypothesisSet &state);

[[nodiscard]] HypothesisSet build_hypothesis_set(
    const std::vector<HypothesisProposal> &proposals, std::string problem_id,
    int max_hypotheses, bool mutually_exclusive = false,
    std::vector<std::string> update_ids = {});

[[nodiscard]] HypothesisSet build_hypothesis_set(
    const std::vector<Hypothesis> &hypotheses, std::string problem_id,
    int max_hypotheses, bool mutually_exclusive = false,
    std::vector<std::string> update_ids = {});

void require_hypothesis_set_integrity(const HypothesisSet &state);

[[nodiscard]] HypothesisStateUpdate update_hypothesis_state(
    const HypothesisSet &state, const LikelihoodMap &likelihoods,
    std::vector<std::string> evidence_ids = {},
    std::vector<std::string> collision_ids = {},
    std::string evidence_polarity = "support",
    std::string operation = "BAYES_EVIDENCE_UPDATE", std::string reason = {});

[[nodiscard]] HypothesisStateUpdate apply_collision_update(
    const HypothesisSet &state, const std::vector<std::string> &objects,
    std::string_view falsifier, std::vector<std::string> evidence_ids,
    std::string collision_id, int severity_bp);

} // namespace statewright::reasoning
