#include "statewright/saa/retrieval_explanation.hpp"

#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[nodiscard]] std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] RetrievalExplanation finalize_explanation(
    std::string decision_signature, std::string status,
    std::vector<std::string> selected, std::vector<std::string> rejected,
    std::vector<CounterfactualFitChange> changes,
    const std::set<std::string> &dimensions, Json source) {
  std::vector<std::string> sorted_dimensions(dimensions.begin(),
                                             dimensions.end());
  Json change_payload = Json::array();
  for (const auto &change : changes) {
    change_payload.push_back(to_json(change));
  }
  const Json payload =
      {{"counterfactual_changes", change_payload},
       {"decision_signature", decision_signature},
       {"fit_gap_dimensions", sorted_dimensions},
       {"rejected_reasons", rejected},
       {"selected_reasons", selected},
       {"source", std::move(source)},
       {"status", status},
       {"version", retrieval_explanation_version}};
  return {.schema_version = 1,
          .explanation_version =
              std::string(retrieval_explanation_version),
          .decision_signature = std::move(decision_signature),
          .status = std::move(status),
          .selected_reasons = std::move(selected),
          .rejected_reasons = std::move(rejected),
          .counterfactual_changes = std::move(changes),
          .fit_gap_dimensions = std::move(sorted_dimensions),
          .explanation_signature = contracts::sha256_json(payload)};
}

[[nodiscard]] std::string mathematical_gap_dimension(std::string gap) {
  gap = lowercase(std::move(gap));
  if (gap.find("input meaning") != std::string::npos) {
    return "MATHEMATICAL_INPUT_SEMANTICS";
  }
  if (gap.find("output count") != std::string::npos) {
    return "MATHEMATICAL_OUTPUT_SHAPE";
  }
  if (gap.find("domain") != std::string::npos) {
    return "MATHEMATICAL_DOMAIN";
  }
  return "MATHEMATICAL_CONTRACT";
}

[[nodiscard]] std::string reasoning_gap_dimension(std::string gap) {
  gap = lowercase(std::move(gap));
  if (gap.find("inputs") != std::string::npos) {
    return "REASONING_INPUT_SEMANTICS";
  }
  if (gap.find("outputs") != std::string::npos) {
    return "REASONING_OUTPUT_SEMANTICS";
  }
  if (gap.find("applicability") != std::string::npos) {
    return "REASONING_APPLICABILITY";
  }
  if (gap.find("invariant") != std::string::npos) {
    return "REASONING_INVARIANTS";
  }
  if (gap.find("evidence") != std::string::npos) {
    return "REASONING_EVIDENCE_CAPABILITY";
  }
  if (gap.find("termination") != std::string::npos ||
      gap.find("budget") != std::string::npos) {
    return "REASONING_TERMINATION_BUDGET";
  }
  return "REASONING_CONTRACT";
}

} // namespace

RetrievalExplanation
explain_algorithm_transfer(const AlgorithmTransferAssessment &assessment) {
  std::vector<std::string> selected;
  std::vector<std::string> rejected;
  std::vector<CounterfactualFitChange> changes;
  std::set<std::string> dimensions;
  std::string status;
  if (assessment.transfer_without_requalification) {
    selected.push_back(
        "source algorithm transfer preserves semantic, boundary, invariant, "
        "dynamics and evidence contracts");
    status = "EXPLAINED_EXACT_TRANSFER";
  } else if (!assessment.blocking_gaps.empty()) {
    rejected = assessment.blocking_gaps;
    dimensions.insert("SEMANTIC_TRANSFER_CONTRACT");
    std::string current;
    for (std::size_t index = 0; index < assessment.blocking_gaps.size();
         ++index) {
      if (index != 0U) {
        current += "; ";
      }
      current += assessment.blocking_gaps[index];
    }
    changes.push_back(
        {.component = "MATHEMATICAL_ALGORITHM",
         .dimension = "SEMANTIC_TRANSFER_CONTRACT",
         .current = std::move(current),
         .required_change =
             "resolve and independently qualify semantic equivalence before "
             "transfer",
         .would_remove_blocker = false});
    status = "EXPLAINED_BLOCKED_TRANSFER";
  } else {
    status = "EXPLAINED_TRANSFER_REQUALIFICATION_DELTA";
  }
  for (const auto &dimension : assessment.adaptation_gaps) {
    dimensions.insert(dimension);
    changes.push_back(
        {.component = "MATHEMATICAL_ALGORITHM",
         .dimension = dimension,
         .current = "source and target " + lowercase(dimension) + " differ",
         .required_change =
             "adapt only this contract dimension and requalify in the target "
             "domain",
         .would_remove_blocker = true});
  }
  return finalize_explanation(
      assessment.assessment_signature, std::move(status), std::move(selected),
      std::move(rejected), std::move(changes), dimensions,
      to_json(assessment));
}

RetrievalExplanation explain_unified_retrieval(
    const UnifiedRetrievalDecision &decision,
    UnifiedProblemRequirements requirements) {
  const auto task = canonical_unified_requirements(std::move(requirements));
  std::vector<std::string> selected;
  std::vector<std::string> rejected;
  std::vector<CounterfactualFitChange> changes;
  std::set<std::string> dimensions;

  if (decision.selected_mathematical_algorithm_id) {
    const auto found = std::ranges::find_if(
        decision.mathematical_candidates, [&](const auto &candidate) {
          return candidate.canonical_algorithm_id ==
                 *decision.selected_mathematical_algorithm_id;
        });
    if (found != decision.mathematical_candidates.end()) {
      selected.push_back("mathematical algorithm " +
                         found->canonical_algorithm_id + " selected with fit " +
                         std::to_string(found->fit_score_bp) + "/10000");
    }
  }
  for (const auto &candidate : decision.mathematical_candidates) {
    if (decision.selected_mathematical_algorithm_id &&
        candidate.canonical_algorithm_id ==
            *decision.selected_mathematical_algorithm_id) {
      continue;
    }
    for (const auto &gap : candidate.blocking_gaps) {
      rejected.push_back("mathematical " + candidate.canonical_algorithm_id +
                         ": " + gap);
      const auto dimension = mathematical_gap_dimension(gap);
      dimensions.insert(dimension);
      changes.push_back({.component = "MATHEMATICAL_ALGORITHM",
                         .dimension = dimension,
                         .current = gap,
                         .required_change = "satisfy this exact contract",
                         .would_remove_blocker = true});
    }
  }

  if (decision.reasoning_result) {
    if (decision.selected_reasoning_id) {
      const auto found = std::ranges::find_if(
          decision.reasoning_result->candidates, [&](const auto &candidate) {
            return candidate.reasoning_id == *decision.selected_reasoning_id;
          });
      if (found != decision.reasoning_result->candidates.end()) {
        selected.push_back("reasoning algorithm " + found->reasoning_id +
                           " selected with fit " +
                           std::to_string(found->fit_score_bp) + "/10000");
      }
    }
    for (const auto &candidate : decision.reasoning_result->candidates) {
      if (decision.selected_reasoning_id &&
          candidate.reasoning_id == *decision.selected_reasoning_id) {
        continue;
      }
      for (const auto &gap : candidate.blocking_gaps) {
        rejected.push_back("reasoning " + candidate.reasoning_id + ": " +
                           gap);
        const auto dimension = reasoning_gap_dimension(gap);
        dimensions.insert(dimension);
        changes.push_back({.component = "REASONING_ALGORITHM",
                           .dimension = dimension,
                           .current = gap,
                           .required_change = "satisfy this exact contract",
                           .would_remove_blocker = true});
      }
    }
  }

  for (const auto &missing : decision.missing_components) {
    const std::string dimension = "MISSING_" + missing;
    dimensions.insert(dimension);
    changes.push_back(
        {.component = missing,
         .dimension = dimension,
         .current = "no eligible qualified candidate",
         .required_change =
             "provide, adapt, or qualify a candidate that satisfies the "
             "explicit problem contract",
         .would_remove_blocker = true});
  }

  std::string status;
  if (decision.required_components_satisfied) {
    status = "EXPLAINED_COMPLETE_KNOWN_SOLUTION";
  } else if (decision.selected_mathematical_algorithm_id ||
             decision.selected_reasoning_id) {
    status = "EXPLAINED_PARTIAL_FIT_WITH_DELTA";
  } else {
    status = "EXPLAINED_CONFIRMED_RETRIEVAL_GAP";
  }
  return finalize_explanation(
      decision.decision_signature, std::move(status), std::move(selected),
      std::move(rejected), std::move(changes), dimensions,
      {{"decision", to_json(decision)}, {"problem", to_json(task)}});
}

Json to_json(const CounterfactualFitChange &value) {
  return {{"component", value.component},
          {"current", value.current},
          {"dimension", value.dimension},
          {"required_change", value.required_change},
          {"would_remove_blocker", value.would_remove_blocker}};
}

Json to_json(const RetrievalExplanation &value) {
  Json changes = Json::array();
  for (const auto &change : value.counterfactual_changes) {
    changes.push_back(to_json(change));
  }
  return {{"counterfactual_changes", changes},
          {"decision_signature", value.decision_signature},
          {"explanation_signature", value.explanation_signature},
          {"explanation_version", value.explanation_version},
          {"fit_gap_dimensions", value.fit_gap_dimensions},
          {"rejected_reasons", value.rejected_reasons},
          {"schema_version", value.schema_version},
          {"selected_reasons", value.selected_reasons},
          {"status", value.status}};
}

} // namespace statewright::saa
