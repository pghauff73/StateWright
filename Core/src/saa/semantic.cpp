#include "statewright/saa/semantic.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

const std::set<std::string> semantic_issue_statuses = {
    "CANDIDATE_REPRESENTATIVE_SEMANTICS", "DECLARED_SEMANTICS",
    "EVIDENCE_SUPPORTED_SEMANTICS",       "SEMANTICALLY_CONTRADICTED",
    "SEMANTICALLY_RESOLVED",              "SEMANTIC_MISREPRESENTATION",
    "UNRESOLVED_SEMANTICS"};
const std::set<std::string> falsifier_outcomes = {"SURVIVED", "TRIGGERED",
                                                  "UNTESTED"};
const std::vector<std::string> propagation_subsystems = {
    "EON", "OURD", "IURM", "CFEL", "BD_DL", "HYPOTHESIS_STATE",
    "ALGORITHM_STORE"};

[[noreturn]] void semantic_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string normalize_text(std::string value) {
  std::istringstream input(value);
  std::ostringstream output;
  std::string word;
  bool first = true;
  while (input >> word) {
    if (!first) {
      output << ' ';
    }
    output << word;
    first = false;
  }
  return output.str();
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = normalize_text(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] SemanticMap normalized_semantic_map(const SemanticMap &values) {
  SemanticMap result;
  for (const auto &[position, raw_text] : values) {
    if (position < 0) {
      semantic_error("semantic positions cannot be negative");
    }
    std::string text = normalize_text(raw_text);
    if (text.empty()) {
      semantic_error("semantic descriptions must be non-empty");
    }
    result.emplace(position, std::move(text));
  }
  return result;
}

[[nodiscard]] std::vector<std::size_t> affected_outputs(
    const CanonicalMIMOCoupling &mimo, std::size_t input_index) {
  std::vector<std::size_t> result;
  for (std::size_t output = 0; output < mimo.output_count; ++output) {
    if (mimo.nonzero_pattern[output][input_index]) {
      result.push_back(output);
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::size_t> affected_outputs(
    const RepresentativeInputCandidate &candidate,
    std::size_t input_index) {
  std::vector<std::size_t> result;
  for (std::size_t output = 0;
       output < candidate.representative_channels.size(); ++output) {
    if (!candidate.representative_channels[output][input_index].zero()) {
      result.push_back(output);
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::string> question_set(
    std::string_view coordinate_label,
    const std::vector<std::size_t> &affected_output_indices,
    const std::optional<std::string> &declared_meaning,
    std::string_view issue_kind) {
  std::ostringstream outputs;
  for (std::size_t index = 0; index < affected_output_indices.size(); ++index) {
    if (index != 0U) {
      outputs << ", ";
    }
    outputs << 'y' << affected_output_indices[index];
  }
  const std::string output_text = affected_output_indices.empty()
                                      ? "no observed outputs"
                                      : outputs.str();
  std::vector<std::string> questions = {
      "What independent quantity does " + std::string(coordinate_label) +
          " actually represent?",
      "Why does " + std::string(coordinate_label) + " affect " + output_text +
          ", and is that output footprint intended?",
      "Which outputs should change when only the meaning of " +
          std::string(coordinate_label) + " changes?",
      "Which outputs must remain invariant if " +
          std::string(coordinate_label) + " is semantically independent?",
      "What observation would falsify the proposed meaning of " +
          std::string(coordinate_label) + "?"};
  if (declared_meaning) {
    questions.insert(questions.begin() + 1,
                     "Does the declared meaning '" + *declared_meaning +
                         "' fully explain the observed effects of " +
                         std::string(coordinate_label) + "?");
  }
  if (issue_kind == "COUPLED_INPUT") {
    questions.push_back("Is " + std::string(coordinate_label) +
                        " a mixture of multiple latent inputs that should be separated?");
  } else if (issue_kind == "REDUNDANT_INPUT") {
    questions.push_back("Is " + std::string(coordinate_label) +
                        " merely a duplicate or linear combination of other inputs?");
  } else if (issue_kind == "REPRESENTATIVE_COORDINATE") {
    questions.push_back(
        "What domain concept names the newly discovered representative coordinate " +
        std::string(coordinate_label) + "?");
  }
  return questions;
}

[[nodiscard]] SemanticRepresentationIssue make_issue(
    std::string issue_kind, std::string coordinate_kind, int coordinate_index,
    std::string coordinate_label,
    std::vector<std::size_t> source_input_indices,
    RationalPolynomial source_coefficients,
    std::optional<std::string> declared_meaning,
    std::vector<std::size_t> affected_output_indices,
    const SemanticMap &output_semantics, std::string status,
    std::string source_representation_signature) {
  if (!semantic_issue_statuses.contains(status)) {
    semantic_error("unsupported semantic issue status: " + status);
  }
  std::sort(affected_output_indices.begin(), affected_output_indices.end());
  affected_output_indices.erase(
      std::unique(affected_output_indices.begin(),
                  affected_output_indices.end()),
      affected_output_indices.end());
  std::vector<std::string> output_meanings;
  for (const std::size_t output : affected_output_indices) {
    const auto found = output_semantics.find(static_cast<int>(output));
    output_meanings.push_back(found == output_semantics.end()
                                  ? "y" + std::to_string(output)
                                  : found->second);
  }
  auto questions = question_set(coordinate_label, affected_output_indices,
                                declared_meaning, issue_kind);
  const Json material =
      {{"affected_output_indices", affected_output_indices},
       {"coordinate_index", coordinate_index},
       {"coordinate_kind", coordinate_kind},
       {"declared_meaning",
        declared_meaning ? Json(*declared_meaning) : Json(nullptr)},
       {"issue_kind", issue_kind},
       {"schema_version", 1},
       {"semantic_version", semantic_version},
       {"source_coefficients", polynomial_json(source_coefficients)},
       {"source_input_indices", source_input_indices},
       {"source_representation_signature", source_representation_signature}};
  const std::string signature = contracts::sha256_json(material);
  return {.issue_id = "semantic:" + signature.substr(0, 24),
          .issue_kind = std::move(issue_kind),
          .coordinate_kind = std::move(coordinate_kind),
          .coordinate_index = coordinate_index,
          .coordinate_label = std::move(coordinate_label),
          .source_input_indices = std::move(source_input_indices),
          .source_coefficients = std::move(source_coefficients),
          .declared_meaning = std::move(declared_meaning),
          .affected_output_indices = std::move(affected_output_indices),
          .affected_output_meanings = std::move(output_meanings),
          .status = status,
          .resolution_required = status != "SEMANTICALLY_RESOLVED",
          .questions = std::move(questions),
          .source_representation_signature =
              std::move(source_representation_signature),
          .signature = signature};
}

[[nodiscard]] std::vector<std::size_t>
normalized_positions(std::vector<int> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  std::vector<std::size_t> result;
  for (const int value : values) {
    if (value < 0) {
      semantic_error("semantic output positions cannot be negative");
    }
    result.push_back(static_cast<std::size_t>(value));
  }
  return result;
}

[[nodiscard]] std::vector<std::string>
normalized_texts(std::vector<std::string> values) {
  std::vector<std::string> result;
  for (auto &value : values) {
    value = normalize_text(std::move(value));
    if (!value.empty()) {
      result.push_back(std::move(value));
    }
  }
  return result;
}

} // namespace

SemanticFalsifierResult::SemanticFalsifierResult(
    std::string falsifier_value, std::string outcome_value,
    std::optional<std::string> evidence_id_value)
    : falsifier(normalize_text(std::move(falsifier_value))),
      outcome(uppercase(std::move(outcome_value))),
      evidence_id(std::move(evidence_id_value)) {
  if (!falsifier_outcomes.contains(outcome)) {
    semantic_error("unsupported semantic falsifier outcome: " + outcome);
  }
  if (evidence_id) {
    *evidence_id = normalize_text(std::move(*evidence_id));
  }
}

Json to_json(const SemanticRepresentationIssue &value) {
  return {{"affected_output_indices", value.affected_output_indices},
          {"affected_output_meanings", value.affected_output_meanings},
          {"coordinate_index", value.coordinate_index},
          {"coordinate_kind", value.coordinate_kind},
          {"coordinate_label", value.coordinate_label},
          {"declared_meaning",
           value.declared_meaning ? Json(*value.declared_meaning)
                                  : Json(nullptr)},
          {"issue_id", value.issue_id},
          {"issue_kind", value.issue_kind},
          {"questions", value.questions},
          {"resolution_required", value.resolution_required},
          {"signature", value.signature},
          {"source_coefficients", polynomial_json(value.source_coefficients)},
          {"source_input_indices", value.source_input_indices},
          {"source_representation_signature",
           value.source_representation_signature},
          {"status", value.status}};
}

Json to_json(const SemanticRepresentationAssessment &value) {
  Json issues = Json::array();
  for (const auto &issue : value.issues) {
    issues.push_back(to_json(issue));
  }
  return {{"assessment_signature", value.assessment_signature},
          {"canonical_admission_eligible",
           value.canonical_admission_eligible},
          {"issues", issues},
          {"mathematical_admission_eligible",
           value.mathematical_admission_eligible},
          {"mathematical_assessment",
           to_json(value.mathematical_assessment)},
          {"schema_version", value.schema_version},
          {"semantic_status", value.semantic_status},
          {"semantic_version", value.semantic_version_value},
          {"warnings", value.warnings}};
}

Json to_json(const SemanticCandidateMeaning &value) {
  return {{"assumptions", value.assumptions},
          {"candidate_id", value.candidate_id},
          {"epistemic_status", value.epistemic_status},
          {"excluded_output_indices", value.excluded_output_indices},
          {"expected_output_indices", value.expected_output_indices},
          {"falsifiers", value.falsifiers},
          {"issue_id", value.issue_id},
          {"meaning", value.meaning},
          {"signature", value.signature}};
}

Json to_json(const SemanticFalsifierResult &value) {
  return {{"evidence_id",
           value.evidence_id ? Json(*value.evidence_id) : Json(nullptr)},
          {"falsifier", value.falsifier},
          {"outcome", value.outcome}};
}

Json to_json(const SemanticResolution &value) {
  Json falsifiers = Json::array();
  for (const auto &result : value.falsifier_results) {
    falsifiers.push_back(to_json(result));
  }
  return {{"candidate_id", value.candidate_id},
          {"canonical_semantic_eligible",
           value.canonical_semantic_eligible},
          {"evidence_ids", value.evidence_ids},
          {"falsifier_results", falsifiers},
          {"independent_review", value.independent_review},
          {"issue_id", value.issue_id},
          {"resolution_signature", value.resolution_signature},
          {"semantic_fit_bp", value.semantic_fit_bp},
          {"status", value.status},
          {"warnings", value.warnings}};
}

Json to_json(const SemanticPropagationDirective &value) {
  return {{"action", value.action},
          {"blocking", value.blocking},
          {"issue_id", value.issue_id},
          {"payload", value.payload},
          {"question_required", value.question_required},
          {"subsystem", value.subsystem}};
}

SemanticRepresentationAssessment assess_mimo_semantics(
    const CanonicalMIMOCoupling &mimo,
    const RepresentationAssessment *mathematical_assessment,
    const SemanticMap &input_semantics,
    const SemanticMap &output_semantics) {
  RepresentationAssessment math = mathematical_assessment
                                      ? *mathematical_assessment
                                      : assess_mimo_representation(mimo);
  const auto inputs = normalized_semantic_map(input_semantics);
  const auto outputs = normalized_semantic_map(output_semantics);
  std::vector<SemanticRepresentationIssue> issues;
  std::vector<std::string> warnings;
  if (math.status == "NON_REPRESENTATIVE_COUPLED") {
    for (std::size_t input = 0; input < mimo.input_count; ++input) {
      auto affected = affected_outputs(mimo, input);
      if (affected.size() <= 1U) {
        continue;
      }
      const auto declared = inputs.find(static_cast<int>(input));
      issues.push_back(make_issue(
          "COUPLED_INPUT", "SOURCE_INPUT", static_cast<int>(input),
          "u" + std::to_string(input), {input}, {1},
          declared == inputs.end()
              ? std::nullopt
              : std::optional<std::string>(declared->second),
          std::move(affected), outputs, "SEMANTIC_MISREPRESENTATION",
          mimo.ordered_signature));
    }
    if (issues.empty()) {
      for (std::size_t input = 0; input < mimo.input_count; ++input) {
        const auto declared = inputs.find(static_cast<int>(input));
        issues.push_back(make_issue(
            "COUPLED_INPUT", "SOURCE_INPUT", static_cast<int>(input),
            "u" + std::to_string(input), {input}, {1},
            declared == inputs.end()
                ? std::nullopt
                : std::optional<std::string>(declared->second),
            affected_outputs(mimo, input), outputs,
            "SEMANTIC_MISREPRESENTATION", mimo.ordered_signature));
      }
    }
  } else if (math.status == "NON_REPRESENTATIVE_REDUNDANT_INPUTS") {
    const std::vector<std::size_t> nonpivots =
        math.minimality ? math.minimality->nonpivot_input_positions
                        : std::vector<std::size_t>{};
    for (const std::size_t input : nonpivots) {
      const auto declared = inputs.find(static_cast<int>(input));
      issues.push_back(make_issue(
          "REDUNDANT_INPUT", "SOURCE_INPUT", static_cast<int>(input),
          "u" + std::to_string(input), {input}, {1},
          declared == inputs.end()
              ? std::nullopt
              : std::optional<std::string>(declared->second),
          affected_outputs(mimo, input), outputs,
          "SEMANTIC_MISREPRESENTATION", mimo.ordered_signature));
    }
  } else if (math.status == "REPRESENTATIVE_EXACT") {
    for (std::size_t input = 0; input < mimo.input_count; ++input) {
      const auto declared = inputs.find(static_cast<int>(input));
      const std::optional<std::string> meaning =
          declared == inputs.end()
              ? std::nullopt
              : std::optional<std::string>(declared->second);
      issues.push_back(make_issue(
          "SOURCE_INPUT_SEMANTICS", "SOURCE_INPUT", static_cast<int>(input),
          "u" + std::to_string(input), {input}, {1}, meaning,
          affected_outputs(mimo, input), outputs,
          meaning ? "DECLARED_SEMANTICS" : "UNRESOLVED_SEMANTICS",
          mimo.ordered_signature));
    }
  } else {
    warnings.push_back(
        "semantic resolution cannot establish canonical admission while mathematical representation is unresolved");
  }
  std::string semantic_status = "UNRESOLVED_SEMANTICS";
  if (std::any_of(issues.begin(), issues.end(), [](const auto &issue) {
        return issue.status == "SEMANTIC_MISREPRESENTATION";
      })) {
    semantic_status = "SEMANTIC_MISREPRESENTATION";
  } else if (std::any_of(issues.begin(), issues.end(), [](const auto &issue) {
               return issue.status == "UNRESOLVED_SEMANTICS";
             })) {
    semantic_status = "UNRESOLVED_SEMANTICS";
  } else if (!issues.empty()) {
    semantic_status = "DECLARED_SEMANTICS";
  }
  const bool mathematical_eligible = math.canonical_admission_eligible;
  Json issue_signatures = Json::array();
  for (const auto &issue : issues) {
    issue_signatures.push_back(issue.signature);
  }
  const Json payload =
      {{"canonical_admission_eligible", false},
       {"issue_signatures", issue_signatures},
       {"mathematical_admission_eligible", mathematical_eligible},
       {"mathematical_assessment_signature", math.assessment_signature},
       {"schema_version", 1},
       {"semantic_status", semantic_status},
       {"semantic_version", semantic_version}};
  return {.schema_version = 1,
          .semantic_version_value = std::string(semantic_version),
          .mathematical_assessment = std::move(math),
          .semantic_status = std::move(semantic_status),
          .issues = std::move(issues),
          .mathematical_admission_eligible = mathematical_eligible,
          .canonical_admission_eligible = false,
          .assessment_signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

std::vector<SemanticRepresentationIssue>
assess_representative_candidate_semantics(
    const CanonicalMIMOCoupling &mimo,
    const RepresentativeInputSearch &search,
    const SemanticMap &input_semantics,
    const SemanticMap &output_semantics) {
  if (!search.best_candidate) {
    return {};
  }
  const auto inputs = normalized_semantic_map(input_semantics);
  const auto outputs = normalized_semantic_map(output_semantics);
  const auto &candidate = *search.best_candidate;
  std::vector<SemanticRepresentationIssue> issues;
  for (std::size_t representative = 0;
       representative < candidate.representative_input_count;
       ++representative) {
    const auto &coefficients =
        candidate.source_to_representative_projection[representative];
    std::vector<std::size_t> sources;
    RationalPolynomial nonzero_coefficients;
    for (std::size_t source = 0; source < coefficients.size(); ++source) {
      if (coefficients[source] != 0) {
        sources.push_back(source);
        nonzero_coefficients.push_back(coefficients[source]);
      }
    }
    std::optional<std::string> inherited;
    if (sources.size() == 1U && nonzero_coefficients == RationalPolynomial{1}) {
      const auto found = inputs.find(static_cast<int>(sources.front()));
      if (found != inputs.end()) {
        inherited = found->second;
      }
    }
    issues.push_back(make_issue(
        "REPRESENTATIVE_COORDINATE", "REPRESENTATIVE_INPUT",
        static_cast<int>(representative), "v" + std::to_string(representative),
        std::move(sources), std::move(nonzero_coefficients), inherited,
        affected_outputs(candidate, representative), outputs,
        inherited ? "DECLARED_SEMANTICS" : "UNRESOLVED_SEMANTICS",
        mimo.ordered_signature));
  }
  return issues;
}

SemanticCandidateMeaning make_semantic_candidate(
    const SemanticRepresentationIssue &issue, std::string meaning,
    std::vector<int> expected_output_indices,
    std::vector<int> excluded_output_indices,
    std::vector<std::string> assumptions,
    std::vector<std::string> falsifiers) {
  meaning = normalize_text(std::move(meaning));
  if (meaning.empty()) {
    semantic_error("semantic candidate meaning must be non-empty");
  }
  auto expected = normalized_positions(std::move(expected_output_indices));
  auto excluded = normalized_positions(std::move(excluded_output_indices));
  for (const auto value : expected) {
    if (std::binary_search(excluded.begin(), excluded.end(), value)) {
      semantic_error(
          "semantic expected and excluded outputs must be disjoint");
    }
  }
  assumptions = normalized_texts(std::move(assumptions));
  falsifiers = normalized_texts(std::move(falsifiers));
  const Json material =
      {{"assumptions", assumptions},
       {"excluded_output_indices", excluded},
       {"expected_output_indices", expected},
       {"falsifiers", falsifiers},
       {"issue_id", issue.issue_id},
       {"meaning", meaning},
       {"schema_version", 1},
       {"semantic_version", semantic_version}};
  const std::string signature = contracts::sha256_json(material);
  return {.candidate_id = "semantic-candidate:" + signature.substr(0, 24),
          .issue_id = issue.issue_id,
          .meaning = std::move(meaning),
          .expected_output_indices = std::move(expected),
          .excluded_output_indices = std::move(excluded),
          .assumptions = std::move(assumptions),
          .falsifiers = std::move(falsifiers),
          .epistemic_status = "MODEL_PROPOSED_SEMANTICS",
          .signature = signature};
}

SemanticResolution evaluate_semantic_candidate(
    const SemanticRepresentationIssue &issue,
    const SemanticCandidateMeaning &candidate,
    std::vector<std::string> evidence_ids,
    std::vector<SemanticFalsifierResult> falsifier_results,
    bool independent_review) {
  if (candidate.issue_id != issue.issue_id) {
    semantic_error("semantic candidate does not belong to issue");
  }
  std::set<std::string> evidence_set;
  for (auto &value : evidence_ids) {
    value = normalize_text(std::move(value));
    if (!value.empty()) {
      evidence_set.insert(std::move(value));
    }
  }
  evidence_ids.assign(evidence_set.begin(), evidence_set.end());
  const std::set<std::size_t> affected(issue.affected_output_indices.begin(),
                                       issue.affected_output_indices.end());
  const std::set<std::size_t> expected(candidate.expected_output_indices.begin(),
                                       candidate.expected_output_indices.end());
  const std::set<std::size_t> excluded(candidate.excluded_output_indices.begin(),
                                       candidate.excluded_output_indices.end());
  bool excluded_clear = true;
  for (const auto output : affected) {
    excluded_clear = excluded_clear && !excluded.contains(output);
  }
  const int semantic_fit_bp =
      expected == affected && excluded_clear ? 10000 : 0;
  const bool triggered =
      std::any_of(falsifier_results.begin(), falsifier_results.end(),
                  [](const auto &result) {
                    return result.outcome == "TRIGGERED";
                  });
  bool all_falsifiers_tested = true;
  if (!candidate.falsifiers.empty()) {
    std::map<std::string, std::string> outcomes;
    for (const auto &result : falsifier_results) {
      outcomes[result.falsifier] = result.outcome;
    }
    for (const auto &falsifier : candidate.falsifiers) {
      const auto found = outcomes.find(falsifier);
      all_falsifiers_tested =
          all_falsifiers_tested && found != outcomes.end() &&
          found->second == "SURVIVED";
    }
  }
  std::string status;
  bool eligible = false;
  std::vector<std::string> warnings;
  if (triggered || semantic_fit_bp < 10000) {
    status = "SEMANTICALLY_CONTRADICTED";
  } else if (evidence_ids.empty()) {
    status = "CANDIDATE_REPRESENTATIVE_SEMANTICS";
    warnings.push_back(
        "semantic meaning cannot be resolved without grounded evidence");
  } else if (!all_falsifiers_tested) {
    status = "EVIDENCE_SUPPORTED_SEMANTICS";
    warnings.push_back("semantic falsifiers remain untested or unsatisfied");
  } else if (!independent_review) {
    status = "EVIDENCE_SUPPORTED_SEMANTICS";
    warnings.push_back(
        "independent semantic review is required before canonical admission");
  } else {
    status = "SEMANTICALLY_RESOLVED";
    eligible = true;
  }
  Json result_payload = Json::array();
  for (const auto &result : falsifier_results) {
    result_payload.push_back(to_json(result));
  }
  const Json material =
      {{"candidate_signature", candidate.signature},
       {"evidence_ids", evidence_ids},
       {"falsifier_results", result_payload},
       {"independent_review", independent_review},
       {"issue_id", issue.issue_id},
       {"schema_version", 1},
       {"semantic_fit_bp", semantic_fit_bp},
       {"semantic_version", semantic_version},
       {"status", status}};
  return {.issue_id = issue.issue_id,
          .candidate_id = candidate.candidate_id,
          .status = std::move(status),
          .semantic_fit_bp = semantic_fit_bp,
          .evidence_ids = std::move(evidence_ids),
          .falsifier_results = std::move(falsifier_results),
          .independent_review = independent_review,
          .canonical_semantic_eligible = eligible,
          .resolution_signature = contracts::sha256_json(material),
          .warnings = std::move(warnings)};
}

bool canonical_semantic_admission(
    bool mathematical_eligible,
    const std::vector<SemanticRepresentationIssue> &issues,
    const std::vector<SemanticResolution> &resolutions) {
  if (!mathematical_eligible) {
    return false;
  }
  std::map<std::string, const SemanticResolution *> by_issue;
  for (const auto &resolution : resolutions) {
    if (!by_issue.emplace(resolution.issue_id, &resolution).second) {
      semantic_error(
          "duplicate semantic resolutions for one issue are not allowed");
    }
  }
  return std::all_of(issues.begin(), issues.end(), [&](const auto &issue) {
    const auto found = by_issue.find(issue.issue_id);
    return found != by_issue.end() &&
           found->second->status == "SEMANTICALLY_RESOLVED" &&
           found->second->canonical_semantic_eligible;
  });
}

std::vector<SemanticPropagationDirective> propagate_semantic_issues(
    const std::vector<SemanticRepresentationIssue> &issues) {
  const std::map<std::string, std::pair<std::string, bool>> actions = {
      {"ALGORITHM_STORE", {"BLOCK_CANONICAL_ADMISSION", true}},
      {"BD_DL", {"DETERMINE_SEMANTIC_DOMAIN_AND_BOUNDS", false}},
      {"CFEL", {"REGISTER_SEMANTIC_COLLISION_ON_CONTRADICTORY_EFFECT", false}},
      {"EON", {"SURFACE_UNRESOLVED_SEMANTIC_REPRESENTATION", false}},
      {"HYPOTHESIS_STATE", {"RECORD_CANDIDATE_MEANINGS_AS_UNVERIFIED", false}},
      {"IURM", {"BLOCK_AS_INDEPENDENT_DIMENSION", true}},
      {"OURD", {"CREATE_SEMANTIC_RESOLUTION_OBJECTIVE", false}}};
  std::vector<SemanticPropagationDirective> directives;
  for (const auto &issue : issues) {
    const Json payload =
        {{"affected_output_indices", issue.affected_output_indices},
         {"coordinate_index", issue.coordinate_index},
         {"coordinate_kind", issue.coordinate_kind},
         {"coordinate_label", issue.coordinate_label},
         {"questions", issue.questions},
         {"status", issue.status}};
    for (const auto &subsystem : propagation_subsystems) {
      directives.push_back(
          {.issue_id = issue.issue_id,
           .subsystem = subsystem,
           .action = actions.at(subsystem).first,
           .blocking = actions.at(subsystem).second,
           .question_required = true,
           .payload = payload});
    }
  }
  return directives;
}

std::vector<std::string> semantic_followup_questions(
    const std::vector<SemanticRepresentationIssue> &issues) {
  std::set<std::string> seen;
  std::vector<std::string> questions;
  for (const auto &issue : issues) {
    for (const auto &question : issue.questions) {
      if (seen.insert(question).second) {
        questions.push_back(question);
      }
    }
  }
  return questions;
}

} // namespace statewright::saa
