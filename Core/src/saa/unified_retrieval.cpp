#include "statewright/saa/unified_retrieval.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void retrieval_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string canonical_text(std::string value) {
  std::string result;
  bool pending_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      result.push_back(' ');
      pending_space = false;
    }
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

[[nodiscard]] std::vector<std::string>
canonical_texts(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = canonical_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] bool concept_matches_meaning(
    const SemanticConcept &semantic_concept, std::string_view meaning,
    SemanticMeaningEquivalence *ontology) {
  const std::string target = canonical_text(std::string(meaning));
  std::vector<std::string> direct = {semantic_concept.canonical_name,
                                     semantic_concept.meaning};
  direct.insert(direct.end(), semantic_concept.aliases.begin(),
                semantic_concept.aliases.end());
  for (const auto &candidate : direct) {
    if (canonical_text(candidate) == target) {
      return true;
    }
  }
  if (ontology == nullptr) {
    return false;
  }
  for (const auto &candidate : direct) {
    try {
      if (ontology->meanings_equivalent(candidate, target)) {
        return true;
      }
    } catch (...) {
    }
  }
  return false;
}

[[nodiscard]] std::string joined(const std::vector<std::string> &values) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      output << ", ";
    }
    output << values[index];
  }
  return output.str();
}

} // namespace

UnifiedProblemRequirements canonical_unified_requirements(
    UnifiedProblemRequirements requirements) {
  requirements.problem_id = trimmed(std::move(requirements.problem_id));
  if (requirements.problem_id.empty()) {
    retrieval_error("SAA-10 unified retrieval requires problem_id");
  }
  if (requirements.desired_mathematical_output_count < 0) {
    retrieval_error("desired mathematical output count cannot be negative");
  }
  if (requirements.max_reasoning_steps < 1 ||
      requirements.max_reasoning_steps > max_reasoning_steps) {
    retrieval_error(
        "unified reasoning step budget outside supported range");
  }
  for (const auto &semantic_concept : requirements.input_concepts) {
    if (!semantic_concept.canonical_eligible) {
      retrieval_error(
          "SAA-10 requires canonically resolved input concepts");
    }
  }
  std::ranges::sort(requirements.input_concepts, {},
                    &SemanticConcept::concept_signature);
  requirements.mathematical_domain =
      canonical_text(std::move(requirements.mathematical_domain));
  requirements.reasoning_desired_outputs =
      canonical_texts(std::move(requirements.reasoning_desired_outputs));
  requirements.reasoning_applicability =
      canonical_texts(std::move(requirements.reasoning_applicability));
  requirements.required_invariants =
      canonical_texts(std::move(requirements.required_invariants));
  requirements.available_evidence_requirements = canonical_texts(
      std::move(requirements.available_evidence_requirements));
  return requirements;
}

MathematicalFitAssessment evaluate_mathematical_fit(
    const Json &item, UnifiedProblemRequirements requirements,
    SemanticMeaningEquivalence *ontology) {
  const auto task = canonical_unified_requirements(std::move(requirements));
  const std::string canonical_id = item.value("canonical_id", "");
  const Json payload = item.value("payload", Json::object());
  std::vector<std::string> algorithm_meanings;
  if (payload.contains("inputs") && payload.at("inputs").is_array()) {
    for (const auto &row : payload.at("inputs")) {
      if (!row.is_object()) {
        continue;
      }
      const std::string meaning =
          canonical_text(row.value("canonical_meaning", ""));
      if (!meaning.empty()) {
        algorithm_meanings.push_back(meaning);
      }
    }
  }
  std::vector<std::string> matched;
  std::vector<std::string> unmatched;
  for (const auto &meaning : algorithm_meanings) {
    const bool found =
        std::ranges::any_of(task.input_concepts,
                            [&](const auto &semantic_concept) {
          return concept_matches_meaning(semantic_concept, meaning, ontology);
        });
    (found ? matched : unmatched).push_back(meaning);
  }
  std::ranges::sort(matched);
  std::ranges::sort(unmatched);
  const int semantic_fit =
      algorithm_meanings.empty()
          ? 10000
          : (10000 * static_cast<int>(matched.size())) /
                static_cast<int>(algorithm_meanings.size());
  const int output_count = payload.value("output_count", 0);
  const int output_fit =
      task.desired_mathematical_output_count == 0 ||
              output_count == task.desired_mathematical_output_count
          ? 10000
          : 0;
  const std::string candidate_domain =
      canonical_text(payload.value("domain", ""));
  const int domain_fit =
      task.mathematical_domain.empty() ||
              candidate_domain == task.mathematical_domain
          ? 10000
          : 0;
  std::vector<std::string> blockers;
  if (!unmatched.empty()) {
    blockers.push_back("unmatched representative input meanings: " +
                       joined(unmatched));
  }
  if (task.desired_mathematical_output_count != 0 &&
      output_count != task.desired_mathematical_output_count) {
    blockers.push_back("mathematical output count " +
                       std::to_string(output_count) + " != required " +
                       std::to_string(task.desired_mathematical_output_count));
  }
  if (!task.mathematical_domain.empty() &&
      candidate_domain != task.mathematical_domain) {
    blockers.push_back("mathematical domain " +
                       (candidate_domain.empty() ? "<none>"
                                                 : candidate_domain) +
                       " != required " + task.mathematical_domain);
  }
  const int score =
      (60 * semantic_fit + 20 * output_fit + 20 * domain_fit) / 100;
  std::string status;
  if (!blockers.empty()) {
    status = "INELIGIBLE_MATHEMATICAL_FIT";
  } else if (score >= 9000) {
    status = "GOOD_MATHEMATICAL_FIT";
  } else if (score >= 6500) {
    status = "PARTIAL_MATHEMATICAL_FIT";
  } else {
    status = "POOR_MATHEMATICAL_FIT";
  }
  const Json fit_payload =
      {{"algorithm_meanings", algorithm_meanings},
       {"blocking_gaps", blockers},
       {"canonical_algorithm_id", canonical_id},
       {"domain_fit", domain_fit},
       {"matched", matched},
       {"output_fit", output_fit},
       {"problem", to_json(task)},
       {"score", score},
       {"semantic_fit", semantic_fit},
       {"unmatched", unmatched},
       {"version", unified_retrieval_version}};
  return {.canonical_algorithm_id = canonical_id,
          .status = std::move(status),
          .fit_score_bp = score,
          .semantic_input_fit_bp = semantic_fit,
          .output_shape_fit_bp = output_fit,
          .domain_fit_bp = domain_fit,
          .matched_input_meanings = std::move(matched),
          .unmatched_input_meanings = std::move(unmatched),
          .blocking_gaps = std::move(blockers),
          .fit_signature = contracts::sha256_json(fit_payload)};
}

UnifiedRetrievalDecision retrieve_unified_solution(
    MathematicalAlgorithmCatalog *mathematical_catalog,
    const ReasoningAlgorithmCatalog *reasoning_catalog,
    UnifiedProblemRequirements requirements,
    SemanticMeaningEquivalence *ontology, std::size_t mathematical_limit,
    std::size_t reasoning_limit) {
  const auto task = canonical_unified_requirements(std::move(requirements));
  if (mathematical_limit < 1U ||
      mathematical_limit > max_unified_mathematical_results) {
    retrieval_error("mathematical retrieval limit outside supported range");
  }
  const std::string problem_signature = contracts::sha256_json(
      {{"problem", to_json(task)}, {"version", unified_retrieval_version}});

  std::vector<MathematicalFitAssessment> math_assessments;
  if (mathematical_catalog != nullptr) {
    for (const auto &item :
         mathematical_catalog->list_mathematical_algorithms()) {
      math_assessments.push_back(
          evaluate_mathematical_fit(item, task, ontology));
    }
  }
  std::ranges::sort(math_assessments, [](const auto &left, const auto &right) {
    return std::tuple(left.eligible() ? 0 : 1, -left.fit_score_bp,
                      left.canonical_algorithm_id) <
           std::tuple(right.eligible() ? 0 : 1, -right.fit_score_bp,
                      right.canonical_algorithm_id);
  });
  if (math_assessments.size() > mathematical_limit) {
    math_assessments.resize(mathematical_limit);
  }
  std::optional<std::string> selected_math;
  for (const auto &assessment : math_assessments) {
    if (assessment.eligible()) {
      selected_math = assessment.canonical_algorithm_id;
      break;
    }
  }

  std::optional<ReasoningRetrievalResult> reasoning_result;
  std::optional<std::string> selected_reasoning;
  if (reasoning_catalog != nullptr) {
    std::set<std::string> reasoning_inputs;
    for (const auto &semantic_concept : task.input_concepts) {
      reasoning_inputs.insert(semantic_concept.meaning);
    }
    reasoning_result = retrieve_reasoning_algorithms(
        *reasoning_catalog,
        {.available_inputs =
             {reasoning_inputs.begin(), reasoning_inputs.end()},
         .desired_outputs = task.reasoning_desired_outputs,
         .required_applicability = task.reasoning_applicability,
         .required_invariants = task.required_invariants,
         .available_evidence_requirements =
             task.available_evidence_requirements,
         .max_steps = task.max_reasoning_steps},
        reasoning_limit, true);
    selected_reasoning = reasoning_result->selected_reasoning_id;
  }

  std::vector<std::string> missing;
  if (task.require_mathematical_algorithm && !selected_math) {
    missing.push_back("MATHEMATICAL_ALGORITHM");
  }
  if (task.require_reasoning_algorithm && !selected_reasoning) {
    missing.push_back("REASONING_ALGORITHM");
  }
  const bool satisfied = missing.empty();
  std::string status;
  if (satisfied) {
    status = "QUALIFIED_KNOWN_SOLUTION_PAIR_FOUND";
  } else if (selected_math || selected_reasoning) {
    status = "PARTIAL_QUALIFIED_KNOWN_SOLUTION_FOUND";
  } else {
    status = "NO_QUALIFIED_KNOWN_SOLUTION_FIT";
  }
  std::vector<std::string> math_signatures;
  for (const auto &assessment : math_assessments) {
    math_signatures.push_back(assessment.fit_signature);
  }
  const Json decision_payload =
      {{"mathematical_fit_signatures", math_signatures},
       {"missing_components", missing},
       {"problem_signature", problem_signature},
       {"reasoning_result_signature",
        reasoning_result ? Json(reasoning_result->result_signature)
                         : Json(nullptr)},
       {"required_components_satisfied", satisfied},
       {"selected_mathematical_algorithm_id",
        selected_math ? Json(*selected_math) : Json(nullptr)},
       {"selected_reasoning_id",
        selected_reasoning ? Json(*selected_reasoning) : Json(nullptr)},
       {"status", status},
       {"version", unified_retrieval_version}};
  return {.schema_version = 1,
          .retrieval_version = std::string(unified_retrieval_version),
          .problem_signature = problem_signature,
          .mathematical_candidates = std::move(math_assessments),
          .selected_mathematical_algorithm_id = std::move(selected_math),
          .reasoning_result = std::move(reasoning_result),
          .selected_reasoning_id = std::move(selected_reasoning),
          .required_components_satisfied = satisfied,
          .missing_components = std::move(missing),
          .status = std::move(status),
          .decision_signature = contracts::sha256_json(decision_payload)};
}

Json to_json(const UnifiedProblemRequirements &value) {
  std::vector<std::string> concept_signatures;
  for (const auto &semantic_concept : value.input_concepts) {
    concept_signatures.push_back(semantic_concept.concept_signature);
  }
  return {{"available_evidence_requirements",
           value.available_evidence_requirements},
          {"desired_mathematical_output_count",
           value.desired_mathematical_output_count},
          {"input_concept_signatures", concept_signatures},
          {"mathematical_domain", value.mathematical_domain},
          {"max_reasoning_steps", value.max_reasoning_steps},
          {"problem_id", value.problem_id},
          {"reasoning_applicability", value.reasoning_applicability},
          {"reasoning_desired_outputs", value.reasoning_desired_outputs},
          {"require_mathematical_algorithm",
           value.require_mathematical_algorithm},
          {"require_reasoning_algorithm", value.require_reasoning_algorithm},
          {"required_invariants", value.required_invariants}};
}

Json to_json(const MathematicalFitAssessment &value) {
  return {{"blocking_gaps", value.blocking_gaps},
          {"canonical_algorithm_id", value.canonical_algorithm_id},
          {"domain_fit_bp", value.domain_fit_bp},
          {"eligible", value.eligible()},
          {"fit_score_bp", value.fit_score_bp},
          {"fit_signature", value.fit_signature},
          {"matched_input_meanings", value.matched_input_meanings},
          {"output_shape_fit_bp", value.output_shape_fit_bp},
          {"semantic_input_fit_bp", value.semantic_input_fit_bp},
          {"status", value.status},
          {"unmatched_input_meanings", value.unmatched_input_meanings}};
}

Json to_json(const UnifiedRetrievalDecision &value) {
  Json mathematical = Json::array();
  for (const auto &candidate : value.mathematical_candidates) {
    mathematical.push_back(to_json(candidate));
  }
  return {{"decision_signature", value.decision_signature},
          {"mathematical_candidates", mathematical},
          {"missing_components", value.missing_components},
          {"problem_signature", value.problem_signature},
          {"reasoning_result",
           value.reasoning_result ? to_json(*value.reasoning_result)
                                  : Json(nullptr)},
          {"required_components_satisfied",
           value.required_components_satisfied},
          {"retrieval_version", value.retrieval_version},
          {"schema_version", value.schema_version},
          {"selected_mathematical_algorithm_id",
           value.selected_mathematical_algorithm_id
               ? Json(*value.selected_mathematical_algorithm_id)
               : Json(nullptr)},
          {"selected_reasoning_id",
           value.selected_reasoning_id ? Json(*value.selected_reasoning_id)
                                       : Json(nullptr)},
          {"status", value.status}};
}

} // namespace statewright::saa
