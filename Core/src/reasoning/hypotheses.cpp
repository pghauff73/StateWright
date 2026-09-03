#include "statewright/reasoning/hypotheses.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

namespace statewright::reasoning {
namespace {

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto non_space = [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) == 0;
  };
  const auto first = std::find_if(value.begin(), value.end(), non_space);
  const auto last = std::find_if(value.rbegin(), value.rend(), non_space).base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values) {
  std::set<std::string> unique;
  for (const auto &value : values) {
    if (!value.empty()) {
      unique.insert(value);
    }
  }
  return {unique.begin(), unique.end()};
}

void validate_score(int value, std::string_view label) {
  if (value < 0 || value > score_scale) {
    policy_error(std::string(label) + " must be 0..10000");
  }
}

[[nodiscard]] bool valid_status(std::string_view status) {
  return status == "ACTIVE" || status == "WEAKENED" ||
         status == "SUPPORTED" || status == "FALSIFIED" ||
         status == "UNRESOLVED";
}

void canonicalize_and_validate(Hypothesis &hypothesis) {
  if (hypothesis.hypothesis_id.empty()) {
    policy_error("hypothesis_id must be non-empty");
  }
  if (trim(hypothesis.proposition).empty()) {
    policy_error("hypothesis proposition must be non-empty");
  }
  validate_score(hypothesis.prior_bp, "hypothesis prior");
  validate_score(hypothesis.posterior_bp, "hypothesis posterior");
  if (!valid_status(hypothesis.status)) {
    policy_error("invalid hypothesis status: " + hypothesis.status);
  }
  hypothesis.supporting_evidence =
      canonical_strings(hypothesis.supporting_evidence);
  hypothesis.conflicting_evidence =
      canonical_strings(hypothesis.conflicting_evidence);
  hypothesis.assumptions = canonical_strings(hypothesis.assumptions);
  hypothesis.predictions = canonical_strings(hypothesis.predictions);
  hypothesis.falsifiers = canonical_strings(hypothesis.falsifiers);
}

[[nodiscard]] std::string hypothesis_signature(const Hypothesis &hypothesis) {
  auto material = to_json(hypothesis);
  material.erase("signature");
  return contracts::sha256_json(material);
}

[[nodiscard]] Hypothesis canonical_hypothesis(Hypothesis hypothesis) {
  canonicalize_and_validate(hypothesis);
  hypothesis.signature = hypothesis_signature(hypothesis);
  return hypothesis;
}

[[nodiscard]] std::map<std::string, int>
normalize_values(const std::map<std::string, std::int64_t> &values,
                 const std::vector<std::string> &ordered_ids) {
  if (ordered_ids.empty()) {
    policy_error(
        "mutually exclusive hypothesis state has no surviving hypothesis");
  }
  std::map<std::string, std::int64_t> scores;
  std::int64_t total = 0;
  for (const auto &identity : ordered_ids) {
    const std::int64_t score = std::max<std::int64_t>(0, values.at(identity));
    scores.emplace(identity, score);
    total += score;
  }

  std::map<std::string, int> normalized;
  if (total <= 0) {
    const int quotient = score_scale / static_cast<int>(ordered_ids.size());
    const int remainder = score_scale % static_cast<int>(ordered_ids.size());
    for (std::size_t index = 0; index < ordered_ids.size(); ++index) {
      normalized[ordered_ids[index]] =
          quotient + (index < static_cast<std::size_t>(remainder) ? 1 : 0);
    }
    return normalized;
  }

  std::vector<std::pair<std::int64_t, std::string>> remainders;
  int assigned = 0;
  for (const auto &identity : ordered_ids) {
    const std::int64_t numerator = scores.at(identity) * score_scale;
    const int floor = static_cast<int>(numerator / total);
    normalized[identity] = floor;
    assigned += floor;
    remainders.emplace_back(numerator % total, identity);
  }
  const int missing = score_scale - assigned;
  std::ranges::sort(remainders, [](const auto &left, const auto &right) {
    if (left.first != right.first) {
      return left.first > right.first;
    }
    return left.second < right.second;
  });
  for (int index = 0; index < missing; ++index) {
    ++normalized.at(remainders.at(static_cast<std::size_t>(index)).second);
  }
  return normalized;
}

enum class ScoreField { prior, posterior };

[[nodiscard]] std::map<std::string, int>
normalize_scores(const std::vector<Hypothesis> &hypotheses, ScoreField field) {
  std::vector<std::string> eligible;
  std::map<std::string, std::int64_t> values;
  for (const auto &hypothesis : hypotheses) {
    if (field == ScoreField::prior || hypothesis.status != "FALSIFIED") {
      eligible.push_back(hypothesis.hypothesis_id);
      values[hypothesis.hypothesis_id] =
          field == ScoreField::prior ? hypothesis.prior_bp
                                     : hypothesis.posterior_bp;
    }
  }
  if (eligible.empty() && field == ScoreField::posterior) {
    std::map<std::string, int> result;
    for (const auto &hypothesis : hypotheses) {
      result[hypothesis.hypothesis_id] = 0;
    }
    return result;
  }
  const auto normalized = normalize_values(values, eligible);
  std::map<std::string, int> result;
  for (const auto &hypothesis : hypotheses) {
    const auto iterator = normalized.find(hypothesis.hypothesis_id);
    result[hypothesis.hypothesis_id] =
        iterator == normalized.end() ? 0 : iterator->second;
  }
  return result;
}

[[nodiscard]] int hypothesis_uncertainty(const std::vector<Hypothesis> &values,
                                         bool mutually_exclusive) {
  if (mutually_exclusive) {
    const auto maximum = std::ranges::max_element(
        values, {}, &Hypothesis::posterior_bp);
    return score_scale - maximum->posterior_bp;
  }
  std::int64_t uncertainty = 0;
  for (const auto &hypothesis : values) {
    uncertainty += score_scale -
                   std::abs((2 * hypothesis.posterior_bp) - score_scale);
  }
  return static_cast<int>(uncertainty /
                          static_cast<std::int64_t>(values.size()));
}

void canonicalize_and_validate(HypothesisSet &state,
                               bool verify_signature = true) {
  if (state.schema_version != 1) {
    policy_error("hypothesis set schema_version must be 1");
  }
  if (trim(state.problem_id).empty()) {
    policy_error("hypothesis set problem_id must be non-empty");
  }
  if (state.max_hypotheses < 1) {
    policy_error("hypothesis set maximum must be positive");
  }
  for (auto &hypothesis : state.hypotheses) {
    canonicalize_and_validate(hypothesis);
    const std::string expected = hypothesis_signature(hypothesis);
    if (!hypothesis.signature.empty() && hypothesis.signature != expected) {
      policy_error("hypothesis signature mismatch");
    }
    hypothesis.signature = expected;
  }
  std::ranges::sort(state.hypotheses, {}, &Hypothesis::hypothesis_id);
  if (state.hypotheses.empty()) {
    policy_error("hypothesis set must contain at least one hypothesis");
  }
  if (state.hypotheses.size() >
      static_cast<std::size_t>(state.max_hypotheses)) {
    policy_error("hypothesis set exceeds its maximum");
  }
  std::set<std::string> identities;
  for (const auto &hypothesis : state.hypotheses) {
    if (!identities.insert(hypothesis.hypothesis_id).second) {
      policy_error("hypothesis set IDs must be unique");
    }
  }
  if (state.mutually_exclusive) {
    const bool has_survivor = std::ranges::any_of(
        state.hypotheses,
        [](const Hypothesis &hypothesis) {
          return hypothesis.status != "FALSIFIED";
        });
    int mass = 0;
    for (const auto &hypothesis : state.hypotheses) {
      mass += hypothesis.posterior_bp;
    }
    if (mass != (has_survivor ? score_scale : 0)) {
      policy_error(
          "mutually exclusive hypothesis posteriors must sum to 10000 or zero "
          "when all are falsified");
    }
  }
  validate_score(state.uncertainty_bp, "hypothesis set uncertainty");
  state.evidence_ids = canonical_strings(state.evidence_ids);
  state.update_ids = canonical_strings(state.update_ids);
  auto material = to_json(state);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (verify_signature && !state.signature.empty() && state.signature != expected) {
    policy_error("hypothesis set signature mismatch");
  }
  state.signature = expected;
}

[[nodiscard]] HypothesisSet finish_set(std::vector<Hypothesis> hypotheses,
                                       std::string problem_id,
                                       int max_hypotheses,
                                       bool mutually_exclusive,
                                       std::vector<std::string> update_ids) {
  std::ranges::sort(hypotheses, {}, &Hypothesis::hypothesis_id);
  std::set<std::string> identities;
  for (const auto &hypothesis : hypotheses) {
    if (!identities.insert(hypothesis.hypothesis_id).second) {
      policy_error("hypothesis IDs must be unique");
    }
  }
  if (mutually_exclusive) {
    const auto priors = normalize_scores(hypotheses, ScoreField::prior);
    const auto posteriors = normalize_scores(hypotheses, ScoreField::posterior);
    for (auto &hypothesis : hypotheses) {
      hypothesis.prior_bp = priors.at(hypothesis.hypothesis_id);
      hypothesis.posterior_bp = posteriors.at(hypothesis.hypothesis_id);
      hypothesis.signature = hypothesis_signature(hypothesis);
    }
  }
  std::vector<std::string> evidence_ids;
  for (const auto &hypothesis : hypotheses) {
    evidence_ids.insert(evidence_ids.end(), hypothesis.supporting_evidence.begin(),
                        hypothesis.supporting_evidence.end());
    evidence_ids.insert(evidence_ids.end(), hypothesis.conflicting_evidence.begin(),
                        hypothesis.conflicting_evidence.end());
  }
  HypothesisSet state{.schema_version = 1,
                      .problem_id = std::move(problem_id),
                      .hypotheses = std::move(hypotheses),
                      .max_hypotheses = max_hypotheses,
                      .mutually_exclusive = mutually_exclusive,
                      .uncertainty_bp = 0,
                      .evidence_ids = canonical_strings(evidence_ids),
                      .update_ids = canonical_strings(update_ids),
                      .signature = {}};
  state.uncertainty_bp =
      hypothesis_uncertainty(state.hypotheses, mutually_exclusive);
  canonicalize_and_validate(state, false);
  return state;
}

[[nodiscard]] int independent_posterior(int posterior_bp,
                                        int likelihood_if_true_bp,
                                        int likelihood_if_false_bp) {
  if (likelihood_if_true_bp == likelihood_if_false_bp) {
    return posterior_bp;
  }
  const std::int64_t effective_prior =
      std::clamp<std::int64_t>(posterior_bp, 1, score_scale - 1);
  const std::int64_t true_mass = effective_prior * likelihood_if_true_bp;
  const std::int64_t false_mass =
      (score_scale - effective_prior) * likelihood_if_false_bp;
  const std::int64_t denominator = true_mass + false_mass;
  if (denominator <= 0) {
    return posterior_bp;
  }
  return static_cast<int>(std::clamp<std::int64_t>(
      ((true_mass * score_scale) + (denominator / 2)) / denominator, 0,
      score_scale));
}

[[nodiscard]] std::string status_after_update(const Hypothesis &hypothesis,
                                              int posterior,
                                              std::string_view polarity,
                                              bool new_provenance) {
  if (posterior <= 0) {
    return "FALSIFIED";
  }
  if (hypothesis.status == "FALSIFIED" && posterior > hypothesis.posterior_bp) {
    if (!new_provenance) {
      policy_error("falsified hypothesis recovery requires new evidence");
    }
    return "WEAKENED";
  }
  if ((polarity == "counterexample" || polarity == "conflict") &&
      posterior <= hypothesis.posterior_bp) {
    return "WEAKENED";
  }
  if (posterior > hypothesis.posterior_bp) {
    if (!hypothesis.assumptions.empty()) {
      return "UNRESOLVED";
    }
    if (posterior >= 7'500) {
      return "SUPPORTED";
    }
    return "ACTIVE";
  }
  return hypothesis.status;
}

[[nodiscard]] HypothesisUpdateRecord
finalize_update_record(HypothesisUpdateRecord record) {
  if (record.schema_version != 1) {
    policy_error("hypothesis update schema_version must be 1");
  }
  if (trim(record.problem_id).empty() || trim(record.hypothesis_id).empty() ||
      trim(record.operation).empty() ||
      trim(record.previous_hypothesis_signature).empty() ||
      trim(record.updated_hypothesis_signature).empty()) {
    policy_error("hypothesis update required identity field is empty");
  }
  if (record.polarity != "support" && record.polarity != "counterexample" &&
      record.polarity != "conflict") {
    policy_error("hypothesis update polarity is invalid");
  }
  validate_score(record.likelihood_if_true_bp, "likelihood_if_true_bp");
  validate_score(record.likelihood_if_false_bp, "likelihood_if_false_bp");
  validate_score(record.previous_posterior_bp, "previous_posterior_bp");
  validate_score(record.updated_posterior_bp, "updated_posterior_bp");
  if (!valid_status(record.previous_status) ||
      !valid_status(record.updated_status)) {
    policy_error("hypothesis update status is invalid");
  }
  record.evidence_ids = canonical_strings(record.evidence_ids);
  record.collision_ids = canonical_strings(record.collision_ids);
  auto material = to_json(record);
  material.erase("signature");
  material.erase("update_id");
  const std::string expected_id =
      "hypothesis-update:" + contracts::sha256_json(material);
  if (!record.update_id.empty() && record.update_id != expected_id) {
    policy_error("hypothesis update ID mismatch");
  }
  record.update_id = expected_id;
  material["update_id"] = expected_id;
  const std::string expected_signature = contracts::sha256_json(material);
  if (!record.signature.empty() && record.signature != expected_signature) {
    policy_error("hypothesis update signature mismatch");
  }
  record.signature = expected_signature;
  return record;
}

} // namespace

contracts::Json to_json(const Hypothesis &hypothesis) {
  return {{"assumptions", hypothesis.assumptions},
          {"conflicting_evidence", hypothesis.conflicting_evidence},
          {"falsifiers", hypothesis.falsifiers},
          {"hypothesis_id", hypothesis.hypothesis_id},
          {"posterior_bp", hypothesis.posterior_bp},
          {"predictions", hypothesis.predictions},
          {"prior_bp", hypothesis.prior_bp},
          {"proposition", hypothesis.proposition},
          {"signature", hypothesis.signature},
          {"status", hypothesis.status},
          {"supporting_evidence", hypothesis.supporting_evidence}};
}

contracts::Json to_json(const HypothesisUpdateRecord &record) {
  return {{"collision_ids", record.collision_ids},
          {"evidence_ids", record.evidence_ids},
          {"hypothesis_id", record.hypothesis_id},
          {"likelihood_if_false_bp", record.likelihood_if_false_bp},
          {"likelihood_if_true_bp", record.likelihood_if_true_bp},
          {"operation", record.operation},
          {"polarity", record.polarity},
          {"previous_hypothesis_signature",
           record.previous_hypothesis_signature},
          {"previous_posterior_bp", record.previous_posterior_bp},
          {"previous_status", record.previous_status},
          {"problem_id", record.problem_id},
          {"reason", record.reason},
          {"schema_version", record.schema_version},
          {"signature", record.signature},
          {"update_id", record.update_id},
          {"updated_hypothesis_signature", record.updated_hypothesis_signature},
          {"updated_posterior_bp", record.updated_posterior_bp},
          {"updated_status", record.updated_status}};
}

contracts::Json to_json(const HypothesisSet &state) {
  contracts::Json hypotheses = contracts::Json::array();
  for (const auto &hypothesis : state.hypotheses) {
    hypotheses.push_back(to_json(hypothesis));
  }
  return {{"evidence_ids", state.evidence_ids},
          {"hypotheses", hypotheses},
          {"max_hypotheses", state.max_hypotheses},
          {"mutually_exclusive", state.mutually_exclusive},
          {"problem_id", state.problem_id},
          {"schema_version", state.schema_version},
          {"signature", state.signature},
          {"uncertainty_bp", state.uncertainty_bp},
          {"update_ids", state.update_ids}};
}

HypothesisSet build_hypothesis_set(
    const std::vector<HypothesisProposal> &proposals, std::string problem_id,
    int max_hypotheses, bool mutually_exclusive,
    std::vector<std::string> update_ids) {
  if (proposals.empty()) {
    policy_error("super reasoning requires at least one hypothesis");
  }
  if (max_hypotheses < 1) {
    policy_error("hypothesis budget must be positive");
  }
  if (proposals.size() > static_cast<std::size_t>(max_hypotheses)) {
    policy_error("hypothesis pool exceeds the reasoning budget");
  }
  std::vector<Hypothesis> hypotheses;
  hypotheses.reserve(proposals.size());
  for (const auto &proposal : proposals) {
    const std::string proposition = trim(proposal.proposition);
    std::string identity = trim(proposal.hypothesis_id);
    if (identity.empty()) {
      identity = "hypothesis:" + contracts::sha256_json(
                                     {{"proposition", proposition}});
    }
    Hypothesis hypothesis{.hypothesis_id = std::move(identity),
                          .proposition = proposition,
                          .prior_bp = proposal.prior_bp,
                          .posterior_bp =
                              proposal.posterior_bp.value_or(proposal.prior_bp),
                          .supporting_evidence = proposal.supporting_evidence,
                          .conflicting_evidence = proposal.conflicting_evidence,
                          .assumptions = proposal.assumptions,
                          .predictions = proposal.predictions,
                          .falsifiers = proposal.falsifiers,
                          .status = proposal.status,
                          .signature = {}};
    canonicalize_and_validate(hypothesis);
    hypothesis.signature = hypothesis_signature(hypothesis);
    hypotheses.push_back(std::move(hypothesis));
  }
  return finish_set(std::move(hypotheses), std::move(problem_id),
                    max_hypotheses, mutually_exclusive,
                    std::move(update_ids));
}

HypothesisSet build_hypothesis_set(const std::vector<Hypothesis> &hypotheses,
                                   std::string problem_id,
                                   int max_hypotheses,
                                   bool mutually_exclusive,
                                   std::vector<std::string> update_ids) {
  if (hypotheses.empty()) {
    policy_error("super reasoning requires at least one hypothesis");
  }
  if (max_hypotheses < 1) {
    policy_error("hypothesis budget must be positive");
  }
  if (hypotheses.size() > static_cast<std::size_t>(max_hypotheses)) {
    policy_error("hypothesis pool exceeds the reasoning budget");
  }
  std::vector<Hypothesis> canonical;
  canonical.reserve(hypotheses.size());
  for (const auto &hypothesis : hypotheses) {
    canonical.push_back(canonical_hypothesis(hypothesis));
  }
  return finish_set(std::move(canonical), std::move(problem_id),
                    max_hypotheses, mutually_exclusive,
                    std::move(update_ids));
}

void require_hypothesis_set_integrity(const HypothesisSet &state) {
  auto checked = state;
  canonicalize_and_validate(checked, true);
}

HypothesisStateUpdate update_hypothesis_state(
    const HypothesisSet &state, const LikelihoodMap &likelihoods,
    std::vector<std::string> evidence_ids,
    std::vector<std::string> collision_ids, std::string evidence_polarity,
    std::string operation, std::string reason) {
  require_hypothesis_set_integrity(state);
  if (evidence_polarity != "support" && evidence_polarity != "counterexample" &&
      evidence_polarity != "conflict") {
    policy_error("evidence polarity is invalid");
  }
  std::set<std::string> declared;
  for (const auto &hypothesis : state.hypotheses) {
    declared.insert(hypothesis.hypothesis_id);
  }
  for (const auto &[identity, scores] : likelihoods) {
    static_cast<void>(scores);
    if (!declared.contains(identity)) {
      policy_error("likelihood update references an unknown hypothesis");
    }
  }
  if (state.mutually_exclusive && likelihoods.size() != declared.size()) {
    policy_error(
        "mutually exclusive updates require one likelihood per hypothesis");
  }
  evidence_ids = canonical_strings(evidence_ids);
  collision_ids = canonical_strings(collision_ids);
  if (evidence_ids.empty() && collision_ids.empty()) {
    policy_error("hypothesis update requires evidence or collision provenance");
  }
  for (const auto &[identity, scores] : likelihoods) {
    static_cast<void>(identity);
    validate_score(scores.first, "likelihood if true");
    validate_score(scores.second, "likelihood if false");
  }

  std::map<std::string, int> updated_scores;
  if (state.mutually_exclusive) {
    std::map<std::string, std::int64_t> weighted;
    std::vector<std::string> surviving_ids;
    for (const auto &hypothesis : state.hypotheses) {
      const auto scores = likelihoods.at(hypothesis.hypothesis_id);
      const std::int64_t effective_prior =
          std::clamp<std::int64_t>(hypothesis.posterior_bp, 1,
                                   score_scale - 1);
      weighted[hypothesis.hypothesis_id] = effective_prior * scores.first;
      if (hypothesis.status != "FALSIFIED" && scores.first > 0) {
        surviving_ids.push_back(hypothesis.hypothesis_id);
      }
    }
    const auto normalized = surviving_ids.empty()
                                ? std::map<std::string, int>{}
                                : normalize_values(weighted, surviving_ids);
    for (const auto &hypothesis : state.hypotheses) {
      const auto iterator = normalized.find(hypothesis.hypothesis_id);
      updated_scores[hypothesis.hypothesis_id] =
          iterator == normalized.end() ? 0 : iterator->second;
    }
  } else {
    for (const auto &hypothesis : state.hypotheses) {
      const auto iterator = likelihoods.find(hypothesis.hypothesis_id);
      if (iterator != likelihoods.end()) {
        updated_scores[hypothesis.hypothesis_id] = independent_posterior(
            hypothesis.posterior_bp, iterator->second.first,
            iterator->second.second);
      }
    }
  }

  std::vector<Hypothesis> hypotheses;
  std::vector<HypothesisUpdateRecord> records;
  for (const auto &hypothesis : state.hypotheses) {
    const auto score_iterator = likelihoods.find(hypothesis.hypothesis_id);
    if (score_iterator == likelihoods.end()) {
      hypotheses.push_back(hypothesis);
      continue;
    }
    const auto [likelihood_true, likelihood_false] = score_iterator->second;
    std::string polarity;
    if (evidence_polarity == "conflict") {
      polarity = "conflict";
    } else if (likelihood_true > likelihood_false) {
      polarity = "support";
    } else if (likelihood_true < likelihood_false) {
      polarity = "counterexample";
    } else {
      hypotheses.push_back(hypothesis);
      continue;
    }
    std::set<std::string> existing(hypothesis.supporting_evidence.begin(),
                                   hypothesis.supporting_evidence.end());
    existing.insert(hypothesis.conflicting_evidence.begin(),
                    hypothesis.conflicting_evidence.end());
    const bool new_evidence = std::ranges::any_of(
        evidence_ids,
        [&](const std::string &identity) { return !existing.contains(identity); });
    const bool new_provenance = new_evidence || !collision_ids.empty();
    if (!new_provenance) {
      hypotheses.push_back(hypothesis);
      continue;
    }
    const int posterior = updated_scores.at(hypothesis.hypothesis_id);
    const std::string updated_status =
        status_after_update(hypothesis, posterior, polarity, new_provenance);
    std::set<std::string> supporting(hypothesis.supporting_evidence.begin(),
                                    hypothesis.supporting_evidence.end());
    std::set<std::string> conflicting(hypothesis.conflicting_evidence.begin(),
                                     hypothesis.conflicting_evidence.end());
    if (polarity == "support" || polarity == "conflict") {
      supporting.insert(evidence_ids.begin(), evidence_ids.end());
    }
    if (polarity == "counterexample" || polarity == "conflict") {
      conflicting.insert(evidence_ids.begin(), evidence_ids.end());
    }
    Hypothesis updated = hypothesis;
    updated.posterior_bp = posterior;
    updated.supporting_evidence = {supporting.begin(), supporting.end()};
    updated.conflicting_evidence = {conflicting.begin(), conflicting.end()};
    updated.status = updated_status;
    updated.signature.clear();
    updated = canonical_hypothesis(std::move(updated));

    HypothesisUpdateRecord record{
        .schema_version = 1,
        .update_id = {},
        .problem_id = state.problem_id,
        .hypothesis_id = hypothesis.hypothesis_id,
        .operation = operation,
        .evidence_ids = evidence_ids,
        .collision_ids = collision_ids,
        .polarity = polarity,
        .likelihood_if_true_bp = likelihood_true,
        .likelihood_if_false_bp = likelihood_false,
        .previous_posterior_bp = hypothesis.posterior_bp,
        .updated_posterior_bp = updated.posterior_bp,
        .previous_status = hypothesis.status,
        .updated_status = updated.status,
        .previous_hypothesis_signature = hypothesis.signature,
        .updated_hypothesis_signature = updated.signature,
        .reason = reason,
        .signature = {}};
    hypotheses.push_back(std::move(updated));
    records.push_back(finalize_update_record(std::move(record)));
  }
  if (records.empty()) {
    return {.state = state, .records = {}};
  }
  std::ranges::sort(records, {}, &HypothesisUpdateRecord::update_id);
  auto update_ids = state.update_ids;
  for (const auto &record : records) {
    update_ids.push_back(record.update_id);
  }
  return {.state = build_hypothesis_set(
              hypotheses, state.problem_id, state.max_hypotheses,
              state.mutually_exclusive, std::move(update_ids)),
          .records = std::move(records)};
}

HypothesisStateUpdate apply_collision_update(
    const HypothesisSet &state, const std::vector<std::string> &objects,
    std::string_view falsifier, std::vector<std::string> evidence_ids,
    std::string collision_id, int severity_bp) {
  validate_score(severity_bp, "collision severity");
  std::set<std::string> object_set;
  for (const auto &object : objects) {
    if (!object.empty()) {
      object_set.insert(lowercase(object));
    }
  }
  const std::string falsifier_key = lowercase(trim(std::string(falsifier)));
  LikelihoodMap targeted;
  for (const auto &hypothesis : state.hypotheses) {
    const bool named = object_set.contains(lowercase(hypothesis.hypothesis_id));
    const bool matched_falsifier =
        !falsifier_key.empty() &&
        std::ranges::any_of(hypothesis.falsifiers,
                            [&](const std::string &candidate) {
                              const std::string key = lowercase(candidate);
                              return falsifier_key == key ||
                                     falsifier_key.find(key) !=
                                         std::string::npos ||
                                     key.find(falsifier_key) !=
                                         std::string::npos;
                            });
    if (named || matched_falsifier) {
      targeted[hypothesis.hypothesis_id] = {
          matched_falsifier && severity_bp >= 5'000
              ? 0
              : std::max(1, score_scale - severity_bp),
          score_scale};
    } else if (state.mutually_exclusive) {
      targeted[hypothesis.hypothesis_id] = {score_scale, score_scale};
    }
  }
  if (targeted.empty()) {
    return {.state = state, .records = {}};
  }
  return update_hypothesis_state(
      state, targeted, std::move(evidence_ids), {std::move(collision_id)},
      "counterexample", "CFEL_COLLISION_UPDATE",
      "CFEL collision contradicted a hypothesis prediction or falsifier");
}

} // namespace statewright::reasoning
