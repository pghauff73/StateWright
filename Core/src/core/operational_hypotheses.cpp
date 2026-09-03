#include "statewright/core/operational_hypotheses.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace statewright::core {
namespace {

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto is_space = [](unsigned char character) {
    return std::isspace(character) != 0;
  };
  const auto first =
      std::find_if_not(value.begin(), value.end(), [&](char character) {
        return is_space(static_cast<unsigned char>(character));
      });
  const auto last =
      std::find_if_not(value.rbegin(), value.rend(), [&](char character) {
        return is_space(static_cast<unsigned char>(character));
      }).base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::string collapse_whitespace(std::string_view input) {
  std::string output;
  bool pending_space = false;
  for (const char character : input) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !output.empty();
      continue;
    }
    if (pending_space) {
      output.push_back(' ');
      pending_space = false;
    }
    output.push_back(character);
  }
  return output;
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values) {
  std::set<std::string> unique;
  for (const auto &value : values) {
    auto canonical = trim(value);
    if (!canonical.empty()) {
      unique.insert(std::move(canonical));
    }
  }
  return {unique.begin(), unique.end()};
}

void validate_score(int value, std::string_view description) {
  if (value < 0 || value > score_scale) {
    policy_error(std::string(description) + " must be 0..10000");
  }
}

void validate_evidence(const EvidenceArtifact &artifact) {
  validate_score(artifact.quality_bp, "evidence quality");
  if (artifact.polarity != "support" && artifact.polarity != "counterexample" &&
      artifact.polarity != "conflict") {
    policy_error(
        "evidence polarity must be support, counterexample, or conflict");
  }
}

void validate_link(const HypothesisEvidenceLink &link) {
  if (link.evidence_id.empty() || link.evidence_fingerprint.empty()) {
    policy_error(
        "hypothesis evidence link requires evidence identity and fingerprint");
  }
  if (link.relation != "supports" && link.relation != "conflicts" &&
      link.relation != "falsifies") {
    policy_error("unsupported hypothesis evidence relation: " + link.relation);
  }
  validate_score(link.quality_bp, "hypothesis evidence link quality");
  if (link.relation_epistemic_status != model_proposed_evidence_relation) {
    policy_error(
        "hypothesis evidence relation must remain explicitly model-proposed");
  }
}

void canonicalize_and_validate(OperationalHypothesis &hypothesis) {
  hypothesis.proposition = collapse_whitespace(hypothesis.proposition);
  if (hypothesis.hypothesis_id.empty() || hypothesis.proposition.empty()) {
    policy_error("hypothesis requires non-empty identity and proposition");
  }
  validate_score(hypothesis.model_prior_bp, "model hypothesis prior");
  hypothesis.assumptions = canonical_strings(hypothesis.assumptions);
  hypothesis.predictions = canonical_strings(hypothesis.predictions);
  hypothesis.falsifiers = canonical_strings(hypothesis.falsifiers);
  for (const auto &link : hypothesis.evidence_links) {
    validate_link(link);
  }
  std::ranges::sort(hypothesis.evidence_links, {},
                    [](const HypothesisEvidenceLink &link) {
                      return link.signature.empty() ? link.evidence_fingerprint
                                                    : link.signature;
                    });
  std::set<std::pair<std::string, std::string>> link_keys;
  for (const auto &link : hypothesis.evidence_links) {
    if (!link_keys.emplace(link.evidence_fingerprint, link.relation).second) {
      policy_error(
          "hypothesis evidence links must be unique by evidence content and "
          "relation");
    }
  }
  validate_score(hypothesis.evidence_support_bp, "hypothesis support score");
  validate_score(hypothesis.evidence_conflict_bp, "hypothesis conflict score");
  if (hypothesis.evidence_balance_bp < -score_scale ||
      hypothesis.evidence_balance_bp > score_scale) {
    policy_error("hypothesis evidence balance must be -10000..10000");
  }
  static const std::set<std::string> statuses = {
      "ACTIVE", "SUPPORTED_BY_LINKED_EVIDENCE",
      "WEAKENED_BY_LINKED_EVIDENCE", "FALSIFIED_BY_LINKED_EVIDENCE",
      "UNRESOLVED"};
  if (!statuses.contains(hypothesis.status)) {
    policy_error("unsupported hypothesis status: " + hypothesis.status);
  }
  if (hypothesis.verification_status != unverified_proposition) {
    policy_error("hypothesis proposition cannot be promoted by model bookkeeping");
  }
}

void canonicalize_and_validate(OperationalHypothesisSet &state) {
  if (state.max_hypotheses < 1) {
    policy_error("hypothesis bound must be positive");
  }
  for (auto &hypothesis : state.hypotheses) {
    canonicalize_and_validate(hypothesis);
  }
  std::ranges::sort(state.hypotheses, {},
                    &OperationalHypothesis::hypothesis_id);
  if (state.hypotheses.size() >
      static_cast<std::size_t>(state.max_hypotheses)) {
    policy_error("hypothesis set exceeds configured bound");
  }
  std::set<std::string> identities;
  for (const auto &hypothesis : state.hypotheses) {
    if (!identities.insert(hypothesis.hypothesis_id).second) {
      policy_error("hypothesis IDs must be unique");
    }
  }
}

[[nodiscard]] std::string
hypothesis_signature(const OperationalHypothesis &hypothesis) {
  auto payload = to_json(hypothesis);
  payload.erase("signature");
  return contracts::sha256_json(payload);
}

[[nodiscard]] std::string
set_signature(int max_hypotheses,
              const std::vector<OperationalHypothesis> &hypotheses) {
  contracts::Json entries = contracts::Json::array();
  for (const auto &hypothesis : hypotheses) {
    entries.push_back({{"hypothesis_id", hypothesis.hypothesis_id},
                       {"signature", hypothesis.signature}});
  }
  return contracts::sha256_json(
      {{"max_hypotheses", max_hypotheses}, {"hypotheses", entries}});
}

struct DerivedScores final {
  int support = 0;
  int conflict = 0;
  int balance = 0;
  std::string status;
};

[[nodiscard]] DerivedScores
derive_scores(const std::vector<HypothesisEvidenceLink> &links) {
  DerivedScores scores;
  bool has_material_falsifier = false;
  for (const auto &link : links) {
    if (link.relation == "supports") {
      scores.support = std::min(score_scale, scores.support + link.quality_bp);
    } else if (link.relation == "conflicts" || link.relation == "falsifies") {
      scores.conflict = std::min(score_scale, scores.conflict + link.quality_bp);
    }
    if (link.relation == "falsifies" &&
        link.quality_bp >= falsifier_quality_threshold_bp) {
      has_material_falsifier = true;
    }
  }
  scores.balance =
      std::clamp(scores.support - scores.conflict, -score_scale, score_scale);
  if (has_material_falsifier) {
    scores.status = "FALSIFIED_BY_LINKED_EVIDENCE";
  } else if (scores.balance >= hypothesis_status_margin_bp) {
    scores.status = "SUPPORTED_BY_LINKED_EVIDENCE";
  } else if (scores.balance <= -hypothesis_status_margin_bp) {
    scores.status = "WEAKENED_BY_LINKED_EVIDENCE";
  } else {
    scores.status = "UNRESOLVED";
  }
  return scores;
}

} // namespace

contracts::Json to_json(const EvidenceArtifact &artifact) {
  return {{"action_id", artifact.action_id},
          {"artifact_id", artifact.artifact_id},
          {"command_capability", artifact.command_capability},
          {"description", artifact.description},
          {"kind", artifact.kind},
          {"path", artifact.path},
          {"polarity", artifact.polarity},
          {"quality_bp", artifact.quality_bp},
          {"requirement_ids", artifact.requirement_ids},
          {"sha256", artifact.sha256},
          {"source_event_id", artifact.source_event_id},
          {"source_snapshot_hash", artifact.source_snapshot_hash},
          {"success", artifact.success ? contracts::Json(*artifact.success)
                                       : contracts::Json(nullptr)}};
}

contracts::Json to_json(const HypothesisEvidenceLink &link) {
  return {{"evidence_fingerprint", link.evidence_fingerprint},
          {"evidence_id", link.evidence_id},
          {"quality_bp", link.quality_bp},
          {"relation", link.relation},
          {"relation_epistemic_status", link.relation_epistemic_status},
          {"signature", link.signature},
          {"source_snapshot_hash", link.source_snapshot_hash}};
}

contracts::Json to_json(const OperationalHypothesis &hypothesis) {
  contracts::Json links = contracts::Json::array();
  for (const auto &link : hypothesis.evidence_links) {
    links.push_back(to_json(link));
  }
  return {{"assumptions", hypothesis.assumptions},
          {"evidence_balance_bp", hypothesis.evidence_balance_bp},
          {"evidence_conflict_bp", hypothesis.evidence_conflict_bp},
          {"evidence_links", links},
          {"evidence_support_bp", hypothesis.evidence_support_bp},
          {"falsifiers", hypothesis.falsifiers},
          {"hypothesis_id", hypothesis.hypothesis_id},
          {"model_prior_bp", hypothesis.model_prior_bp},
          {"predictions", hypothesis.predictions},
          {"proposition", hypothesis.proposition},
          {"signature", hypothesis.signature},
          {"status", hypothesis.status},
          {"verification_status", hypothesis.verification_status}};
}

contracts::Json to_json(const OperationalHypothesisSet &state) {
  contracts::Json hypotheses = contracts::Json::array();
  for (const auto &hypothesis : state.hypotheses) {
    hypotheses.push_back(to_json(hypothesis));
  }
  return {{"hypotheses", hypotheses},
          {"max_hypotheses", state.max_hypotheses},
          {"signature", state.signature}};
}

std::string evidence_fingerprint(const EvidenceArtifact &artifact) {
  validate_evidence(artifact);
  auto requirement_ids = artifact.requirement_ids;
  std::ranges::sort(requirement_ids);
  requirement_ids.erase(
      std::unique(requirement_ids.begin(), requirement_ids.end()),
      requirement_ids.end());
  return contracts::sha256_json(
      {{"command_capability", artifact.command_capability},
       {"description", artifact.description},
       {"kind", artifact.kind},
       {"path", artifact.path},
       {"polarity", artifact.polarity},
       {"quality_bp", artifact.quality_bp},
       {"requirement_ids", requirement_ids},
       {"sha256", artifact.sha256},
       {"source_snapshot_hash", artifact.source_snapshot_hash},
       {"success", artifact.success ? contracts::Json(*artifact.success)
                                    : contracts::Json(nullptr)}});
}

OperationalHypothesis
make_operational_hypothesis(const HypothesisProposal &proposal) {
  OperationalHypothesis hypothesis;
  hypothesis.proposition = collapse_whitespace(proposal.proposition);
  if (hypothesis.proposition.empty()) {
    policy_error("hypothesis proposition must be non-empty");
  }
  validate_score(proposal.model_prior_bp, "hypothesis model_prior_bp");
  hypothesis.model_prior_bp = proposal.model_prior_bp;
  hypothesis.assumptions = canonical_strings(proposal.assumptions);
  hypothesis.predictions = canonical_strings(proposal.predictions);
  hypothesis.falsifiers = canonical_strings(proposal.falsifiers);
  const contracts::Json identity = {
      {"assumptions", hypothesis.assumptions},
      {"falsifiers", hypothesis.falsifiers},
      {"predictions", hypothesis.predictions},
      {"proposition", hypothesis.proposition}};
  hypothesis.hypothesis_id = "hypothesis:" + contracts::sha256_json(identity);
  canonicalize_and_validate(hypothesis);
  hypothesis.signature = hypothesis_signature(hypothesis);
  return hypothesis;
}

HypothesisSetUpdate bounded_operational_hypothesis_set(
    const std::optional<OperationalHypothesisSet> &current,
    const std::vector<HypothesisProposal> &proposals, int max_hypotheses) {
  const int bound = std::max(1, max_hypotheses);
  std::map<std::string, OperationalHypothesis> existing;
  if (current) {
    auto canonical = *current;
    canonicalize_and_validate(canonical);
    for (const auto &hypothesis : canonical.hypotheses) {
      existing.emplace(hypothesis.hypothesis_id, hypothesis);
    }
  }
  if (existing.size() > static_cast<std::size_t>(bound)) {
    policy_error("existing hypothesis state (" +
                 std::to_string(existing.size()) +
                 ") exceeds current active bound (" + std::to_string(bound) +
                 ")");
  }

  HypothesisSetUpdate update;
  for (const auto &proposal : proposals) {
    auto hypothesis = make_operational_hypothesis(proposal);
    if (existing.contains(hypothesis.hypothesis_id)) {
      continue;
    }
    if (existing.size() >= static_cast<std::size_t>(bound)) {
      policy_error("hypothesis set exceeds active bound (" +
                   std::to_string(bound) + ")");
    }
    update.added_hypothesis_ids.push_back(hypothesis.hypothesis_id);
    existing.emplace(hypothesis.hypothesis_id, std::move(hypothesis));
  }
  update.state.max_hypotheses = bound;
  for (auto &[identity, hypothesis] : existing) {
    static_cast<void>(identity);
    update.state.hypotheses.push_back(std::move(hypothesis));
  }
  canonicalize_and_validate(update.state);
  update.state.signature =
      set_signature(update.state.max_hypotheses, update.state.hypotheses);
  return update;
}

HypothesisEvidenceUpdate link_operational_hypothesis_evidence(
    const OperationalHypothesisSet &current,
    const EvidenceRegistry &evidence_registry, std::string_view hypothesis_id,
    std::string_view evidence_id, std::string_view relation) {
  if (relation != "supports" && relation != "conflicts" &&
      relation != "falsifies") {
    policy_error(
        "hypothesis evidence relation must be supports, conflicts, or falsifies");
  }
  const auto artifact_iterator = evidence_registry.find(std::string(evidence_id));
  if (artifact_iterator == evidence_registry.end()) {
    policy_error(
        "hypothesis evidence link references unknown grounded evidence");
  }
  const auto &artifact = artifact_iterator->second;
  validate_evidence(artifact);
  if (artifact.quality_bp <= 0) {
    policy_error(
        "zero-quality evidence cannot create hypothesis-resolution progress");
  }

  auto state = current;
  canonicalize_and_validate(state);
  const auto hypothesis_iterator = std::ranges::find(
      state.hypotheses, hypothesis_id, &OperationalHypothesis::hypothesis_id);
  if (hypothesis_iterator == state.hypotheses.end()) {
    policy_error("unknown hypothesis_id");
  }

  const std::string fingerprint = evidence_fingerprint(artifact);
  if (std::ranges::any_of(
          hypothesis_iterator->evidence_links,
          [&](const HypothesisEvidenceLink &link) {
            return link.evidence_fingerprint == fingerprint;
          })) {
    return {.state = current, .changed = false};
  }
  if (hypothesis_iterator->evidence_links.size() >=
      max_evidence_links_per_hypothesis) {
    policy_error("hypothesis evidence-link bound exceeded");
  }

  HypothesisEvidenceLink link;
  link.evidence_id = std::string(evidence_id);
  link.evidence_fingerprint = fingerprint;
  link.relation = std::string(relation);
  link.quality_bp = artifact.quality_bp;
  link.source_snapshot_hash = artifact.source_snapshot_hash;
  link.signature = contracts::sha256_json(
      {{"evidence_fingerprint", link.evidence_fingerprint},
       {"hypothesis_id", std::string(hypothesis_id)},
       {"quality_bp", link.quality_bp},
       {"relation", link.relation},
       {"relation_epistemic_status", link.relation_epistemic_status},
       {"source_snapshot_hash", link.source_snapshot_hash}});
  hypothesis_iterator->evidence_links.push_back(std::move(link));
  const auto scores = derive_scores(hypothesis_iterator->evidence_links);
  hypothesis_iterator->evidence_support_bp = scores.support;
  hypothesis_iterator->evidence_conflict_bp = scores.conflict;
  hypothesis_iterator->evidence_balance_bp = scores.balance;
  hypothesis_iterator->status = scores.status;
  hypothesis_iterator->verification_status = std::string(unverified_proposition);
  hypothesis_iterator->signature.clear();
  canonicalize_and_validate(*hypothesis_iterator);
  hypothesis_iterator->signature = hypothesis_signature(*hypothesis_iterator);
  canonicalize_and_validate(state);
  state.signature = set_signature(state.max_hypotheses, state.hypotheses);
  return {.state = std::move(state), .changed = true};
}

contracts::Json public_operational_hypothesis_projection(
    const std::optional<OperationalHypothesisSet> &state) {
  if (!state) {
    return {{"hypotheses", contracts::Json::array()},
            {"max_hypotheses", 0},
            {"signature", ""}};
  }
  contracts::Json hypotheses = contracts::Json::array();
  for (const auto &hypothesis : state->hypotheses) {
    contracts::Json links = contracts::Json::array();
    for (const auto &link : hypothesis.evidence_links) {
      links.push_back(
          {{"evidence_id", link.evidence_id},
           {"quality_bp", link.quality_bp},
           {"relation", link.relation},
           {"relation_epistemic_status", link.relation_epistemic_status}});
    }
    hypotheses.push_back(
        {{"assumptions", hypothesis.assumptions},
         {"evidence_balance_bp", hypothesis.evidence_balance_bp},
         {"evidence_conflict_bp", hypothesis.evidence_conflict_bp},
         {"evidence_links", links},
         {"evidence_support_bp", hypothesis.evidence_support_bp},
         {"falsifiers", hypothesis.falsifiers},
         {"hypothesis_id", hypothesis.hypothesis_id},
         {"model_prior_bp", hypothesis.model_prior_bp},
         {"predictions", hypothesis.predictions},
         {"proposition", hypothesis.proposition},
         {"status", hypothesis.status},
         {"verification_status", hypothesis.verification_status}});
  }
  return {{"hypotheses", hypotheses},
          {"max_hypotheses", state->max_hypotheses},
          {"signature", state->signature}};
}

} // namespace statewright::core
