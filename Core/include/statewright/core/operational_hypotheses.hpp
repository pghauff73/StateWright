#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::core {

inline constexpr int score_scale = 10'000;
inline constexpr std::size_t max_evidence_links_per_hypothesis = 64U;
inline constexpr int falsifier_quality_threshold_bp = 5'000;
inline constexpr int hypothesis_status_margin_bp = 1'000;
inline constexpr std::string_view unverified_proposition =
    "UNVERIFIED_PROPOSITION";
inline constexpr std::string_view model_proposed_evidence_relation =
    "MODEL_PROPOSED_RELATION_TO_VERIFIED_EVIDENCE";

struct EvidenceArtifact final {
  std::string artifact_id;
  std::string kind;
  std::string description;
  std::string sha256;
  std::string action_id;
  std::string source_snapshot_hash;
  std::string source_event_id;
  std::string path;
  std::string command_capability;
  std::optional<bool> success;
  std::vector<std::string> requirement_ids;
  int quality_bp = score_scale;
  std::string polarity = "support";
};

struct HypothesisEvidenceLink final {
  std::string evidence_id;
  std::string evidence_fingerprint;
  std::string relation;
  int quality_bp = 0;
  std::string source_snapshot_hash;
  std::string relation_epistemic_status =
      std::string(model_proposed_evidence_relation);
  std::string signature;
};

struct OperationalHypothesis final {
  std::string hypothesis_id;
  std::string proposition;
  int model_prior_bp = 5'000;
  std::vector<std::string> assumptions;
  std::vector<std::string> predictions;
  std::vector<std::string> falsifiers;
  std::vector<HypothesisEvidenceLink> evidence_links;
  int evidence_support_bp = 0;
  int evidence_conflict_bp = 0;
  int evidence_balance_bp = 0;
  std::string status = "ACTIVE";
  std::string verification_status = std::string(unverified_proposition);
  std::string signature;
};

struct OperationalHypothesisSet final {
  int max_hypotheses = 16;
  std::vector<OperationalHypothesis> hypotheses;
  std::string signature;
};

struct HypothesisProposal final {
  std::string proposition;
  int model_prior_bp = 5'000;
  std::vector<std::string> assumptions;
  std::vector<std::string> predictions;
  std::vector<std::string> falsifiers;
};

struct HypothesisSetUpdate final {
  OperationalHypothesisSet state;
  std::vector<std::string> added_hypothesis_ids;
};

struct HypothesisEvidenceUpdate final {
  OperationalHypothesisSet state;
  bool changed = false;
};

using EvidenceRegistry = std::map<std::string, EvidenceArtifact>;

[[nodiscard]] contracts::Json to_json(const EvidenceArtifact &artifact);
[[nodiscard]] contracts::Json to_json(const HypothesisEvidenceLink &link);
[[nodiscard]] contracts::Json to_json(const OperationalHypothesis &hypothesis);
[[nodiscard]] contracts::Json to_json(const OperationalHypothesisSet &state);

[[nodiscard]] std::string evidence_fingerprint(const EvidenceArtifact &artifact);

[[nodiscard]] OperationalHypothesis
make_operational_hypothesis(const HypothesisProposal &proposal);

[[nodiscard]] HypothesisSetUpdate bounded_operational_hypothesis_set(
    const std::optional<OperationalHypothesisSet> &current,
    const std::vector<HypothesisProposal> &proposals, int max_hypotheses);

[[nodiscard]] HypothesisEvidenceUpdate link_operational_hypothesis_evidence(
    const OperationalHypothesisSet &current,
    const EvidenceRegistry &evidence_registry, std::string_view hypothesis_id,
    std::string_view evidence_id, std::string_view relation);

[[nodiscard]] contracts::Json public_operational_hypothesis_projection(
    const std::optional<OperationalHypothesisSet> &state);

} // namespace statewright::core
