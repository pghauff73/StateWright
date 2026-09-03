#include "statewright/saa/nonlinear_search.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;
using Powers = std::vector<int>;
using ExactPolynomial = std::map<Powers, mpq_class>;

[[noreturn]] void nonlinear_search_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] int polynomial_degree(const Powers &powers) {
  int result = 0;
  for (const int power : powers) {
    result += power;
  }
  return result;
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    nonlinear_search_error(std::string(label) +
                           " must use an exact integer or rational, not float");
  }
  return value.value();
}

[[nodiscard]] std::size_t coefficient_bits(const mpq_class &value) {
  const mpz_class numerator = abs(value.get_num());
  const mpz_class denominator = abs(value.get_den());
  return std::max(mpz_sizeinbase(numerator.get_mpz_t(), 2),
                  mpz_sizeinbase(denominator.get_mpz_t(), 2));
}

void validate_parent(const CanonicalRepresentativeAlgorithmForm &form) {
  if (!form.canonical_admission_eligible) {
    nonlinear_search_error(
        "SAA-7.1 parent SAA-6 form is not canonical-admission eligible");
  }
}

[[nodiscard]] Powers zero_powers(std::size_t input_count) {
  return Powers(input_count, 0);
}

[[nodiscard]] Powers unit_powers(std::size_t input_count,
                                 std::size_t index) {
  Powers result(input_count, 0);
  result.at(index) = 1;
  return result;
}

[[nodiscard]] Powers add_powers(const Powers &left, const Powers &right) {
  if (left.size() != right.size()) {
    nonlinear_search_error("polynomial dimensions differ");
  }
  Powers result(left.size(), 0);
  for (std::size_t index = 0; index < left.size(); ++index) {
    result[index] = left[index] + right[index];
  }
  return result;
}

[[nodiscard]] ExactPolynomial add(ExactPolynomial left,
                                  const ExactPolynomial &right) {
  for (const auto &[powers, coefficient] : right) {
    left[powers] += coefficient;
    if (left[powers] == 0) {
      left.erase(powers);
    }
  }
  return left;
}

[[nodiscard]] ExactPolynomial multiply(const ExactPolynomial &left,
                                       const ExactPolynomial &right,
                                       int order) {
  ExactPolynomial result;
  for (const auto &[left_powers, left_coefficient] : left) {
    for (const auto &[right_powers, right_coefficient] : right) {
      auto powers = add_powers(left_powers, right_powers);
      if (polynomial_degree(powers) > order) {
        continue;
      }
      result[std::move(powers)] += left_coefficient * right_coefficient;
    }
  }
  for (auto iterator = result.begin(); iterator != result.end();) {
    if (iterator->second == 0) {
      iterator = result.erase(iterator);
    } else {
      ++iterator;
    }
  }
  return result;
}

[[nodiscard]] ExactPolynomial power(ExactPolynomial base, int exponent,
                                    std::size_t input_count, int order) {
  if (exponent < 0) {
    nonlinear_search_error("polynomial exponent cannot be negative");
  }
  ExactPolynomial result = {{zero_powers(input_count), 1}};
  int remaining = exponent;
  while (remaining != 0) {
    if ((remaining & 1) != 0) {
      result = multiply(result, base, order);
    }
    remaining >>= 1;
    if (remaining != 0) {
      base = multiply(base, base, order);
    }
  }
  return result;
}

[[nodiscard]] std::vector<ExactPolynomial>
jet_polynomials(const CanonicalTaylorJet &jet) {
  std::vector<ExactPolynomial> result(jet.output_count);
  for (const auto &term : jet.terms) {
    result.at(term.output_index)[term.powers] = term.coefficient;
  }
  return result;
}

[[nodiscard]] std::vector<ExactPolynomial>
inverse_shear_variables(const NonlinearShearTransform &transform,
                        std::size_t input_count) {
  std::vector<ExactPolynomial> variables;
  variables.reserve(input_count);
  for (std::size_t index = 0; index < input_count; ++index) {
    variables.push_back({{unit_powers(input_count, index), 1}});
  }
  auto &target = variables.at(transform.target_input_index);
  target[transform.monomial_powers] -= transform.coefficient;
  if (target[transform.monomial_powers] == 0) {
    target.erase(transform.monomial_powers);
  }
  return variables;
}

[[nodiscard]] std::vector<TaylorJetTerm>
substitute_inverse_shear(const CanonicalTaylorJet &jet,
                         const NonlinearShearTransform &transform) {
  const auto variables =
      inverse_shear_variables(transform, jet.input_count);
  std::vector<TaylorJetTerm> result;
  const auto polynomials = jet_polynomials(jet);
  for (std::size_t output_index = 0; output_index < polynomials.size();
       ++output_index) {
    const auto &polynomial = polynomials[output_index];
    ExactPolynomial transformed;
    for (const auto &[powers, coefficient] : polynomial) {
      ExactPolynomial term = {{zero_powers(jet.input_count), coefficient}};
      for (std::size_t input = 0; input < powers.size(); ++input) {
        if (powers[input] != 0) {
          term = multiply(term,
                          power(variables[input], powers[input],
                                jet.input_count, static_cast<int>(jet.order)),
                          static_cast<int>(jet.order));
        }
      }
      transformed = add(std::move(transformed), term);
    }
    for (const auto &[powers, coefficient] : transformed) {
      result.push_back({.output_index = output_index,
                        .powers = powers,
                        .coefficient = coefficient});
    }
  }
  return result;
}

[[nodiscard]] std::vector<mpq_class>
transformed_radius(const CanonicalTaylorJet &jet,
                   const NonlinearShearTransform &transform) {
  mpq_class monomial_bound = 1;
  for (std::size_t index = 0; index < transform.monomial_powers.size();
       ++index) {
    for (int power_index = 0;
         power_index < transform.monomial_powers[index]; ++power_index) {
      monomial_bound *= jet.validity_radius[index];
    }
  }
  const mpq_class excursion = abs(transform.coefficient) * monomial_bound;
  auto radius = jet.validity_radius;
  const std::size_t target = transform.target_input_index;
  radius[target] -= excursion;
  if (radius[target] <= 0) {
    nonlinear_search_error(
        "nonlinear shear consumes the complete certified local target radius");
  }
  if (jet.center[target] - radius[target] < 0 ||
      jet.center[target] + radius[target] > 1) {
    nonlinear_search_error(
        "nonlinear shear transformed local domain leaves normalized [0,1]");
  }
  return radius;
}

[[nodiscard]] std::vector<NumericCoefficient>
numeric_values(const std::vector<mpq_class> &values) {
  std::vector<NumericCoefficient> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    result.emplace_back(value);
  }
  return result;
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

[[nodiscard]] std::vector<NonlinearSemanticRepresentationIssue>
semantic_issues_for_history(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    const std::vector<NonlinearShearTransform> &transforms) {
  if (transforms.empty()) {
    return {};
  }
  std::map<std::size_t, std::vector<const NonlinearShearTransform *>> by_target;
  for (const auto &transform : transforms) {
    by_target[transform.target_input_index].push_back(&transform);
  }
  std::map<std::size_t, std::string> input_meanings;
  for (const auto &input : form.inputs) {
    input_meanings[input.canonical_position] = input.canonical_meaning;
  }
  std::vector<NonlinearSemanticRepresentationIssue> issues;
  for (const auto &[target, target_transforms] : by_target) {
    std::set<std::size_t> source_set = {target};
    std::vector<std::string> transform_signatures;
    for (const auto *transform : target_transforms) {
      transform_signatures.push_back(transform->transform_signature);
      for (std::size_t index = 0; index < transform->monomial_powers.size();
           ++index) {
        if (transform->monomial_powers[index] != 0) {
          source_set.insert(index);
        }
      }
    }
    const std::vector<std::size_t> sources(source_set.begin(), source_set.end());
    const auto &affected = jet.coupling.dependency_by_input.at(target);
    const std::string previous = input_meanings.contains(target)
                                     ? input_meanings.at(target)
                                     : "r" + std::to_string(target);
    const Json material =
        {{"affected_output_indices", affected},
         {"coordinate_index", target},
         {"issue_kind", "NONLINEAR_REPRESENTATIVE_COORDINATE"},
         {"previous_meaning", previous},
         {"representation_version", nonlinear_representation_version},
         {"schema_version", 1},
         {"source_input_indices", sources},
         {"source_representation_signature", jet.local_behavior_signature},
         {"transform_signatures", transform_signatures}};
    const std::string signature = contracts::sha256_json(material);
    const std::string coordinate = "v" + std::to_string(target);
    issues.push_back(
        {.issue_id = "nonlinear-semantic:" + signature.substr(0, 24),
         .issue_kind = "NONLINEAR_REPRESENTATIVE_COORDINATE",
         .coordinate_kind = "NONLINEAR_REPRESENTATIVE_INPUT",
         .coordinate_index = target,
         .coordinate_label = coordinate,
         .previous_meaning = previous,
         .transform_signatures = std::move(transform_signatures),
         .source_input_indices = sources,
         .affected_output_indices = affected,
         .status = "UNRESOLVED_NONLINEAR_SEMANTICS",
         .resolution_required = true,
         .questions = {
             "What independent domain quantity does nonlinear coordinate " +
                 coordinate + " actually represent?",
             "Does the previous meaning '" + previous +
                 "' remain valid after the nonlinear coordinate change?",
             "Which mechanism explains why " + coordinate +
                 " requires the discovered nonlinear combination of source coordinates?",
             "Which outputs should vary when only " + coordinate +
                 " changes inside the qualified local domain?",
             "What observation would falsify the proposed meaning of " +
                 coordinate + "?",
             "Is the proposed meaning stable across more than one expansion point, or only a local interpretation?"},
         .source_representation_signature = jet.local_behavior_signature,
         .signature = signature});
  }
  return issues;
}

[[nodiscard]] mpq_class linear_coefficient(const CanonicalTaylorJet &jet,
                                           std::size_t output_index,
                                           std::size_t input_index) {
  const auto target = unit_powers(jet.input_count, input_index);
  for (const auto &term : jet.terms) {
    if (term.output_index == output_index && term.powers == target) {
      return term.coefficient;
    }
  }
  return 0;
}

[[nodiscard]] std::vector<NonlinearShearTransform>
generated_shears(const CanonicalRepresentativeAlgorithmForm &form,
                 const CanonicalTaylorJet &jet) {
  std::map<std::size_t, std::size_t> output_to_input;
  for (const auto &input : form.inputs) {
    output_to_input[input.paired_output_index] = input.canonical_position;
  }
  using Problem = std::tuple<std::size_t, Powers, mpq_class>;
  std::set<Problem> problematic;
  const auto collect = [&](const auto &terms) {
    for (const auto &term : terms) {
      problematic.emplace(term.output_index, term.powers, term.coefficient);
    }
  };
  collect(jet.coupling.cross_terms);
  collect(jet.coupling.off_pair_terms);
  std::vector<Problem> ordered(problematic.begin(), problematic.end());
  std::ranges::sort(ordered, [](const Problem &left, const Problem &right) {
    const auto &[left_output, left_powers, left_coefficient] = left;
    const auto &[right_output, right_powers, right_coefficient] = right;
    return std::tuple(left_output, polynomial_degree(left_powers), left_powers,
                      left_coefficient) <
           std::tuple(right_output, polynomial_degree(right_powers), right_powers,
                      right_coefficient);
  });
  std::map<std::string, NonlinearShearTransform> candidates;
  for (const auto &[output, powers, coefficient] : ordered) {
    if (polynomial_degree(powers) < 2) {
      continue;
    }
    const auto target = output_to_input.find(output);
    if (target == output_to_input.end() || powers.at(target->second) != 0) {
      continue;
    }
    const mpq_class linear =
        linear_coefficient(jet, output, target->second);
    if (linear == 0) {
      continue;
    }
    try {
      auto transform = make_nonlinear_shear(
          target->second, powers, NumericCoefficient(coefficient / linear));
      candidates[transform.transform_signature] = std::move(transform);
    } catch (const common::Error &) {
    }
  }
  std::vector<NonlinearShearTransform> result;
  result.reserve(candidates.size());
  for (auto &[signature, transform] : candidates) {
    static_cast<void>(signature);
    result.push_back(std::move(transform));
  }
  return result;
}

[[nodiscard]] NonlinearRepresentativeCandidate candidate_for(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &source, CanonicalTaylorJet transformed,
    std::vector<NonlinearShearTransform> transforms) {
  auto issues = semantic_issues_for_history(form, transformed, transforms);
  std::vector<std::string> transform_signatures;
  for (const auto &transform : transforms) {
    transform_signatures.push_back(transform.transform_signature);
  }
  std::vector<std::string> issue_signatures;
  for (const auto &issue : issues) {
    issue_signatures.push_back(issue.signature);
  }
  const bool mathematical_eligible = transformed.coupling.representative;
  const std::size_t coupling_score = transformed.coupling.coupling_score;
  const Json material =
      {{"coupling_score", transformed.coupling.coupling_score},
       {"representation_version", nonlinear_representation_version},
       {"schema_version", 1},
       {"semantic_issue_signatures", issue_signatures},
       {"source_coupling_score", source.coupling.coupling_score},
       {"source_jet_signature", source.local_behavior_signature},
       {"transform_signatures", transform_signatures},
       {"transformed_jet_signature", transformed.local_behavior_signature}};
  return {.source_jet_signature = source.local_behavior_signature,
          .transformed_jet = std::move(transformed),
          .transforms = std::move(transforms),
          .source_coupling_score = source.coupling.coupling_score,
          .coupling_score = coupling_score,
          .exact_invertible = true,
          .mathematical_eligible = mathematical_eligible,
          .semantic_issues = std::move(issues),
          .local_canonical_eligible = mathematical_eligible &&
                                      issue_signatures.empty(),
          .candidate_signature = contracts::sha256_json(material)};
}

[[nodiscard]] bool better_candidate(
    const NonlinearRepresentativeCandidate &candidate,
    const NonlinearRepresentativeCandidate &current) {
  return candidate.coupling_score < current.coupling_score ||
         (candidate.coupling_score == current.coupling_score &&
          candidate.transforms.size() < current.transforms.size()) ||
         (candidate.coupling_score == current.coupling_score &&
          candidate.transforms.size() == current.transforms.size() &&
          candidate.candidate_signature < current.candidate_signature);
}

} // namespace

int NonlinearShearTransform::degree() const {
  return polynomial_degree(monomial_powers);
}

NonlinearShearTransform make_nonlinear_shear(
    std::size_t target_input_index, std::vector<int> monomial_powers,
    NumericCoefficient coefficient) {
  if (target_input_index >= monomial_powers.size()) {
    nonlinear_search_error("nonlinear shear target outside coordinate dimension");
  }
  if (std::ranges::any_of(monomial_powers,
                          [](int power) { return power < 0; })) {
    nonlinear_search_error("nonlinear shear powers cannot be negative");
  }
  if (monomial_powers[target_input_index] != 0) {
    nonlinear_search_error(
        "exact triangular shear monomial cannot depend on its target coordinate");
  }
  const int transform_degree = polynomial_degree(monomial_powers);
  if (transform_degree < 2 ||
      transform_degree > static_cast<int>(max_jet_order)) {
    nonlinear_search_error(
        "SAA-7.1 shear monomial degree must be nonlinear and within jet cap");
  }
  const mpq_class exact_coefficient =
      exact_value(coefficient, "nonlinear shear coefficient");
  if (exact_coefficient == 0) {
    nonlinear_search_error("nonlinear shear coefficient cannot be zero");
  }
  if (coefficient_bits(exact_coefficient) >
      max_nonlinear_transform_coefficient_bits) {
    nonlinear_search_error(
        "nonlinear shear coefficient exceeds bounded exact complexity");
  }
  const Json material =
      {{"coefficient", rational_json(exact_coefficient)},
       {"kind", "EXACT_TRIANGULAR_POLYNOMIAL_SHEAR"},
       {"monomial_powers", monomial_powers},
       {"representation_version", nonlinear_representation_version},
       {"schema_version", 1},
       {"target_input_index", target_input_index}};
  return {.target_input_index = target_input_index,
          .monomial_powers = std::move(monomial_powers),
          .coefficient = exact_coefficient,
          .transform_signature = contracts::sha256_json(material)};
}

CanonicalTaylorJet apply_nonlinear_shear(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet, const NonlinearShearTransform &transform) {
  validate_parent(form);
  if (jet.parent_representative_behavior_signature !=
      form.representative_behavior_signature) {
    nonlinear_search_error(
        "nonlinear shear jet does not belong to supplied SAA-6 form");
  }
  if (transform.monomial_powers.size() != jet.input_count) {
    nonlinear_search_error("nonlinear shear dimension mismatches Taylor jet");
  }
  return canonicalize_taylor_jet(
      form,
      {.input_count = jet.input_count,
       .output_count = jet.output_count,
       .order = jet.order,
       .center = numeric_values(jet.center),
       .validity_radius = numeric_values(transformed_radius(jet, transform)),
       .terms = substitute_inverse_shear(jet, transform)});
}

NonlinearRepresentativeSearch search_nonlinear_representative_coordinates(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet, int max_candidates, int max_depth) {
  validate_parent(form);
  if (jet.parent_representative_behavior_signature !=
      form.representative_behavior_signature) {
    nonlinear_search_error(
        "SAA-7.1 Taylor jet does not belong to supplied SAA-6 form");
  }
  if (max_candidates < 1 ||
      max_candidates > max_nonlinear_search_candidates) {
    nonlinear_search_error("SAA-7.1 candidate budget outside bounded range");
  }
  if (max_depth < 0 || max_depth > max_nonlinear_search_depth) {
    nonlinear_search_error("SAA-7.1 search depth outside bounded range");
  }

  const auto identity = candidate_for(form, jet, jet, {});
  if (jet.coupling.representative) {
    const Json material =
        {{"best_candidate", identity.candidate_signature},
         {"representation_version", nonlinear_representation_version},
         {"schema_version", 1},
         {"source_jet_signature", jet.local_behavior_signature},
         {"status", "NONLINEAR_REPRESENTATIVE_ALREADY_FOUND"}};
    return {.schema_version = 1,
            .representation_version =
                std::string(nonlinear_representation_version),
            .source_jet_signature = jet.local_behavior_signature,
            .status = "NONLINEAR_REPRESENTATIVE_ALREADY_FOUND",
            .representative_found = true,
            .best_candidate = identity,
            .candidates_evaluated = 0,
            .search_depth = 0,
            .search_budget = max_candidates,
            .budget_exhausted = false,
            .explored_signatures = {jet.local_behavior_signature},
            .audit_hash = contracts::sha256_json(material),
            .warnings = {"SAA-7.1 found no nonlinear representational defect; existing SAA-6 semantics remain inherited."}};
  }

  using QueueItem =
      std::pair<CanonicalTaylorJet, std::vector<NonlinearShearTransform>>;
  std::deque<QueueItem> queue;
  queue.emplace_back(jet, std::vector<NonlinearShearTransform>{});
  std::set<std::string> visited = {jet.local_behavior_signature};
  std::vector<std::string> explored = {jet.local_behavior_signature};
  int evaluated = 0;
  NonlinearRepresentativeCandidate best = identity;
  bool budget_exhausted = false;
  int reached_depth = 0;

  while (!queue.empty()) {
    auto [current, history] = std::move(queue.front());
    queue.pop_front();
    if (static_cast<int>(history.size()) >= max_depth) {
      continue;
    }
    const auto transforms = generated_shears(form, current);
    for (const auto &transform : transforms) {
      if (evaluated >= max_candidates) {
        budget_exhausted = true;
        queue.clear();
        break;
      }
      ++evaluated;
      CanonicalTaylorJet transformed;
      try {
        transformed = apply_nonlinear_shear(form, current, transform);
      } catch (const common::Error &) {
        continue;
      }
      if (!visited.insert(transformed.local_behavior_signature).second) {
        continue;
      }
      explored.push_back(transformed.local_behavior_signature);
      auto next_history = history;
      next_history.push_back(transform);
      reached_depth =
          std::max(reached_depth, static_cast<int>(next_history.size()));
      auto candidate = candidate_for(form, jet, transformed, next_history);
      if (better_candidate(candidate, best)) {
        best = candidate;
      }
      if (candidate.mathematical_eligible) {
        queue.clear();
        break;
      }
      queue.emplace_back(std::move(transformed), std::move(next_history));
    }
    if (best.mathematical_eligible) {
      break;
    }
  }

  std::string status;
  bool representative_found = false;
  if (best.mathematical_eligible) {
    status = "NONLINEAR_REPRESENTATIVE_FORM_FOUND";
    representative_found = true;
  } else if (budget_exhausted) {
    status = "NONLINEAR_SEARCH_BUDGET_EXHAUSTED";
  } else {
    status = "NONLINEAR_REPRESENTATION_UNRESOLVED";
  }
  const Json material =
      {{"best_candidate", best.candidate_signature},
       {"budget_exhausted", budget_exhausted},
       {"candidates_evaluated", evaluated},
       {"explored_signatures", explored},
       {"representation_version", nonlinear_representation_version},
       {"schema_version", 1},
       {"search_budget", max_candidates},
       {"search_depth", reached_depth},
       {"source_jet_signature", jet.local_behavior_signature},
       {"status", status}};
  return {.schema_version = 1,
          .representation_version =
              std::string(nonlinear_representation_version),
          .source_jet_signature = jet.local_behavior_signature,
          .status = std::move(status),
          .representative_found = representative_found,
          .best_candidate = std::move(best),
          .candidates_evaluated = evaluated,
          .search_depth = reached_depth,
          .search_budget = max_candidates,
          .budget_exhausted = budget_exhausted,
          .explored_signatures = std::move(explored),
          .audit_hash = contracts::sha256_json(material),
          .warnings = {
              "SAA-7.1 searches only bounded exact triangular polynomial shears; unresolved does not prove that no representative nonlinear coordinates exist.",
              "Any transformed coordinate receives unresolved semantics and cannot become locally canonical until its new meaning is independently evidenced and falsifier-tested."}};
}

SemanticRepresentationIssue
as_semantic_issue(const NonlinearSemanticRepresentationIssue &issue) {
  std::vector<std::string> affected_meanings;
  for (const auto output : issue.affected_output_indices) {
    affected_meanings.push_back("y" + std::to_string(output));
  }
  return {.issue_id = issue.issue_id,
          .issue_kind = issue.issue_kind,
          .coordinate_kind = issue.coordinate_kind,
          .coordinate_index = static_cast<int>(issue.coordinate_index),
          .coordinate_label = issue.coordinate_label,
          .source_input_indices = issue.source_input_indices,
          .source_coefficients = {},
          .declared_meaning = issue.previous_meaning,
          .affected_output_indices = issue.affected_output_indices,
          .affected_output_meanings = std::move(affected_meanings),
          .status = issue.status,
          .resolution_required = issue.resolution_required,
          .questions = issue.questions,
          .source_representation_signature =
              issue.source_representation_signature,
          .signature = issue.signature};
}

SemanticCandidateMeaning make_semantic_candidate(
    const NonlinearSemanticRepresentationIssue &issue, std::string meaning,
    std::vector<int> expected_output_indices,
    std::vector<int> excluded_output_indices,
    std::vector<std::string> assumptions,
    std::vector<std::string> falsifiers) {
  return make_semantic_candidate(
      as_semantic_issue(issue), std::move(meaning),
      std::move(expected_output_indices), std::move(excluded_output_indices),
      std::move(assumptions), std::move(falsifiers));
}

SemanticResolution evaluate_semantic_candidate(
    const NonlinearSemanticRepresentationIssue &issue,
    const SemanticCandidateMeaning &candidate,
    std::vector<std::string> evidence_ids,
    std::vector<SemanticFalsifierResult> falsifier_results,
    bool independent_review) {
  return evaluate_semantic_candidate(
      as_semantic_issue(issue), candidate, std::move(evidence_ids),
      std::move(falsifier_results), independent_review);
}

std::vector<SemanticPropagationDirective> propagate_semantic_issues(
    const std::vector<NonlinearSemanticRepresentationIssue> &issues) {
  std::vector<SemanticRepresentationIssue> converted;
  converted.reserve(issues.size());
  for (const auto &issue : issues) {
    converted.push_back(as_semantic_issue(issue));
  }
  return propagate_semantic_issues(converted);
}

CanonicalNonlinearRepresentativeForm canonicalize_nonlinear_representative(
    const CanonicalRepresentativeAlgorithmForm &form,
    const NonlinearRepresentativeSearch &search,
    const std::vector<SemanticCandidateMeaning> &semantic_candidates,
    const std::vector<SemanticResolution> &semantic_resolutions) {
  validate_parent(form);
  if (!search.representative_found || !search.best_candidate) {
    nonlinear_search_error(
        "SAA-7.1 has not found a mathematically representative local form");
  }
  const auto &candidate = *search.best_candidate;
  if (!candidate.mathematical_eligible || !candidate.exact_invertible) {
    nonlinear_search_error(
        "SAA-7.1 candidate is not exact and mathematically representative");
  }

  std::map<std::size_t, std::string> base_meanings;
  for (const auto &input : form.inputs) {
    base_meanings[input.canonical_position] = input.canonical_meaning;
  }
  std::vector<std::string> meanings;
  meanings.reserve(form.representative_input_count);
  for (std::size_t index = 0; index < form.representative_input_count;
       ++index) {
    meanings.push_back(base_meanings.contains(index)
                           ? base_meanings.at(index)
                           : "r" + std::to_string(index));
  }

  std::map<std::string, const NonlinearSemanticRepresentationIssue *>
      issue_by_id;
  for (const auto &issue : candidate.semantic_issues) {
    if (!issue_by_id.emplace(issue.issue_id, &issue).second) {
      nonlinear_search_error("duplicate SAA-7.1 nonlinear semantic issue");
    }
  }
  std::map<std::string, const SemanticCandidateMeaning *> candidate_by_issue;
  for (const auto &semantic_candidate : semantic_candidates) {
    if (!candidate_by_issue
             .emplace(semantic_candidate.issue_id, &semantic_candidate)
             .second) {
      nonlinear_search_error(
          "duplicate SAA-7.1 semantic candidate for one nonlinear issue");
    }
  }
  std::map<std::string, const SemanticResolution *> resolution_by_issue;
  for (const auto &resolution : semantic_resolutions) {
    if (!resolution_by_issue.emplace(resolution.issue_id, &resolution).second) {
      nonlinear_search_error(
          "duplicate SAA-7.1 semantic resolution for one nonlinear issue");
    }
  }

  for (const auto &issue : candidate.semantic_issues) {
    const auto semantic_candidate = candidate_by_issue.find(issue.issue_id);
    const auto resolution = resolution_by_issue.find(issue.issue_id);
    if (semantic_candidate == candidate_by_issue.end() ||
        resolution == resolution_by_issue.end()) {
      nonlinear_search_error(
          "SAA-7.1 transformed coordinates require explicit semantic candidate and resolution");
    }
    if (semantic_candidate->second->candidate_id !=
        resolution->second->candidate_id) {
      nonlinear_search_error(
          "SAA-7.1 semantic resolution references a different candidate");
    }
    if (resolution->second->status != "SEMANTICALLY_RESOLVED" ||
        !resolution->second->canonical_semantic_eligible ||
        resolution->second->semantic_fit_bp != 10000) {
      nonlinear_search_error(
          "SAA-7.1 nonlinear coordinate meaning is not semantically resolved");
    }
    meanings.at(issue.coordinate_index) =
        canonical_text(semantic_candidate->second->meaning);
  }
  if (candidate_by_issue.size() != issue_by_id.size()) {
    nonlinear_search_error(
        "SAA-7.1 semantic candidates must exactly cover transformed coordinates");
  }
  if (resolution_by_issue.size() != issue_by_id.size()) {
    nonlinear_search_error(
        "SAA-7.1 semantic resolutions must exactly cover transformed coordinates");
  }
  for (const auto &[issue_id, issue] : candidate_by_issue) {
    static_cast<void>(issue);
    if (!issue_by_id.contains(issue_id)) {
      nonlinear_search_error(
          "SAA-7.1 semantic candidates must exactly cover transformed coordinates");
    }
  }
  for (const auto &[issue_id, resolution] : resolution_by_issue) {
    static_cast<void>(resolution);
    if (!issue_by_id.contains(issue_id)) {
      nonlinear_search_error(
          "SAA-7.1 semantic resolutions must exactly cover transformed coordinates");
    }
  }

  std::vector<std::string> resolution_signatures;
  for (const auto &issue : candidate.semantic_issues) {
    resolution_signatures.push_back(
        resolution_by_issue.at(issue.issue_id)->resolution_signature);
  }
  const Json semantic_payload =
      {{"parent_semantic_signature", form.semantic_representative_signature},
       {"representation_version", nonlinear_representation_version},
       {"resolved_input_meanings", meanings},
       {"schema_version", 1},
       {"semantic_resolution_signatures", resolution_signatures}};
  const std::string semantic_signature =
      contracts::sha256_json(semantic_payload);
  const Json behavior_payload =
      {{"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"representation_version", nonlinear_representation_version},
       {"schema_version", 1},
       {"semantic_signature", semantic_signature},
       {"transformed_jet_coefficient_signature",
        candidate.transformed_jet.coefficient_signature},
       {"transformed_jet_scope_signature",
        candidate.transformed_jet.scope_signature}};
  const std::string behavior_signature =
      contracts::sha256_json(behavior_payload);
  std::vector<std::string> transform_signatures;
  for (const auto &transform : candidate.transforms) {
    transform_signatures.push_back(transform.transform_signature);
  }
  const Json audit_payload =
      {{"candidate_signature", candidate.candidate_signature},
       {"local_representative_behavior_signature", behavior_signature},
       {"representation_version", nonlinear_representation_version},
       {"schema_version", 1},
       {"search_audit_hash", search.audit_hash},
       {"semantic_signature", semantic_signature},
       {"transform_signatures", transform_signatures}};
  return {.schema_version = 1,
          .representation_version =
              std::string(nonlinear_representation_version),
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .source_jet_signature = search.source_jet_signature,
          .transformed_jet = candidate.transformed_jet,
          .transform_signatures = std::move(transform_signatures),
          .resolved_input_meanings = std::move(meanings),
          .semantic_signature = semantic_signature,
          .local_representative_behavior_signature = behavior_signature,
          .local_canonical_eligible = true,
          .global_equivalence_eligible = false,
          .store_status =
              "ELIGIBLE_LOCAL_NONLINEAR_REPRESENTATIVE_FORM",
          .audit_hash = contracts::sha256_json(audit_payload),
          .warnings = {
              "SAA-7.1 canonicality is local to the exact Taylor expansion point, truncation order, and certified validity box.",
              "Global nonlinear equivalence remains unproved and is explicitly ineligible."}};
}

Json to_json(const NonlinearShearTransform &value) {
  return {{"coefficient", rational_json(value.coefficient)},
          {"degree", value.degree()},
          {"kind", "EXACT_TRIANGULAR_POLYNOMIAL_SHEAR"},
          {"monomial_powers", value.monomial_powers},
          {"target_input_index", value.target_input_index},
          {"transform_signature", value.transform_signature}};
}

Json to_json(const NonlinearSemanticRepresentationIssue &value) {
  return {{"affected_output_indices", value.affected_output_indices},
          {"coordinate_index", value.coordinate_index},
          {"coordinate_kind", value.coordinate_kind},
          {"coordinate_label", value.coordinate_label},
          {"issue_id", value.issue_id},
          {"issue_kind", value.issue_kind},
          {"previous_meaning", value.previous_meaning},
          {"questions", value.questions},
          {"resolution_required", value.resolution_required},
          {"signature", value.signature},
          {"source_input_indices", value.source_input_indices},
          {"source_representation_signature",
           value.source_representation_signature},
          {"status", value.status},
          {"transform_signatures", value.transform_signatures}};
}

Json to_json(const NonlinearRepresentativeCandidate &value) {
  Json transforms = Json::array();
  for (const auto &transform : value.transforms) {
    transforms.push_back(to_json(transform));
  }
  Json issues = Json::array();
  for (const auto &issue : value.semantic_issues) {
    issues.push_back(to_json(issue));
  }
  return {{"candidate_signature", value.candidate_signature},
          {"coupling_score", value.coupling_score},
          {"exact_invertible", value.exact_invertible},
          {"local_canonical_eligible", value.local_canonical_eligible},
          {"mathematical_eligible", value.mathematical_eligible},
          {"semantic_issues", issues},
          {"source_coupling_score", value.source_coupling_score},
          {"source_jet_signature", value.source_jet_signature},
          {"transformed_jet", to_json(value.transformed_jet)},
          {"transforms", transforms}};
}

Json to_json(const NonlinearRepresentativeSearch &value) {
  return {{"audit_hash", value.audit_hash},
          {"best_candidate",
           value.best_candidate ? to_json(*value.best_candidate) : Json(nullptr)},
          {"budget_exhausted", value.budget_exhausted},
          {"candidates_evaluated", value.candidates_evaluated},
          {"explored_signatures", value.explored_signatures},
          {"representation_version", value.representation_version},
          {"representative_found", value.representative_found},
          {"schema_version", value.schema_version},
          {"search_budget", value.search_budget},
          {"search_depth", value.search_depth},
          {"source_jet_signature", value.source_jet_signature},
          {"status", value.status},
          {"warnings", value.warnings}};
}

Json to_json(const CanonicalNonlinearRepresentativeForm &value) {
  return {{"audit_hash", value.audit_hash},
          {"global_equivalence_eligible", value.global_equivalence_eligible},
          {"local_canonical_eligible", value.local_canonical_eligible},
          {"local_representative_behavior_signature",
           value.local_representative_behavior_signature},
          {"parent_representative_behavior_signature",
           value.parent_representative_behavior_signature},
          {"representation_version", value.representation_version},
          {"resolved_input_meanings", value.resolved_input_meanings},
          {"schema_version", value.schema_version},
          {"semantic_signature", value.semantic_signature},
          {"source_jet_signature", value.source_jet_signature},
          {"store_status", value.store_status},
          {"transform_signatures", value.transform_signatures},
          {"transformed_jet", to_json(value.transformed_jet)},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
