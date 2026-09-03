#include "statewright/saa/nonlinear_transforms.hpp"

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

[[noreturn]] void transform_error(std::string message) {
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
    transform_error(std::string(label) + " must be exact and cannot be float");
  }
  return value.value();
}

[[nodiscard]] std::size_t coefficient_bits(const mpq_class &value) {
  const mpz_class numerator = abs(value.get_num());
  const mpz_class denominator = abs(value.get_den());
  return std::max(mpz_sizeinbase(numerator.get_mpz_t(), 2),
                  mpz_sizeinbase(denominator.get_mpz_t(), 2));
}

[[nodiscard]] Powers unit_powers(std::size_t count, std::size_t index) {
  Powers result(count, 0);
  result.at(index) = 1;
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

[[nodiscard]] std::string tuple_text(const std::set<std::size_t> &values) {
  std::ostringstream output;
  output << '(';
  bool first = true;
  for (const auto value : values) {
    if (!first) {
      output << ", ";
    }
    output << value;
    first = false;
  }
  if (values.size() == 1U) {
    output << ',';
  }
  output << ')';
  return output.str();
}

[[nodiscard]] mpq_class linear_coefficient(const CanonicalTaylorJet &jet,
                                           std::size_t output,
                                           std::size_t input_index) {
  const auto powers = unit_powers(jet.input_count, input_index);
  for (const auto &term : jet.terms) {
    if (term.output_index == output && term.powers == powers) {
      return term.coefficient;
    }
  }
  return 0;
}

[[nodiscard]] std::vector<ExactPolynomialShear> generated_grouped_shears(
    const CanonicalRepresentativeAlgorithmForm &form,
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
    static_cast<void>(left_coefficient);
    static_cast<void>(right_coefficient);
    return std::tuple(left_output, polynomial_degree(left_powers), left_powers) <
           std::tuple(right_output, polynomial_degree(right_powers),
                      right_powers);
  });
  std::map<std::size_t, std::vector<PolynomialShearTerm>> grouped;
  std::vector<std::pair<std::size_t, PolynomialShearTerm>> singles;
  for (const auto &[output, powers, coefficient] : ordered) {
    const auto target = output_to_input.find(output);
    if (target == output_to_input.end() ||
        powers.at(target->second) != 0 || polynomial_degree(powers) < 2) {
      continue;
    }
    const mpq_class linear =
        linear_coefficient(jet, output, target->second);
    if (linear == 0) {
      continue;
    }
    PolynomialShearTerm term{
        .powers = powers,
        .coefficient = mpq_class(coefficient / linear)};
    grouped[target->second].push_back(term);
    singles.emplace_back(target->second, std::move(term));
  }
  std::map<std::string, ExactPolynomialShear> candidates;
  for (const auto &[target, terms] : grouped) {
    try {
      auto transform = make_polynomial_shear(jet.input_count, target, terms);
      candidates[transform.transform_signature] = std::move(transform);
    } catch (const common::Error &) {
    }
  }
  for (const auto &[target, term] : singles) {
    try {
      auto transform =
          make_polynomial_shear(jet.input_count, target, {term});
      candidates[transform.transform_signature] = std::move(transform);
    } catch (const common::Error &) {
    }
  }
  std::vector<ExactPolynomialShear> result;
  result.reserve(candidates.size());
  for (auto &[signature, transform] : candidates) {
    static_cast<void>(signature);
    result.push_back(std::move(transform));
  }
  return result;
}

[[nodiscard]] std::vector<PolynomialSemanticIssue> issues_for_automorphism(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    const std::vector<ExactPolynomialShear> &transforms) {
  std::map<std::size_t, std::vector<const ExactPolynomialShear *>> by_target;
  for (const auto &transform : transforms) {
    by_target[transform.target_input_index].push_back(&transform);
  }
  std::map<std::size_t, std::string> meanings;
  for (const auto &input : form.inputs) {
    meanings[input.canonical_position] = input.canonical_meaning;
  }
  std::vector<PolynomialSemanticIssue> issues;
  for (const auto &[target, target_transforms] : by_target) {
    std::set<std::size_t> source_set = {target};
    std::vector<std::string> signatures;
    for (const auto *transform : target_transforms) {
      signatures.push_back(transform->transform_signature);
      for (const auto &term : transform->terms) {
        for (std::size_t index = 0; index < term.powers.size(); ++index) {
          if (term.powers[index] != 0) {
            source_set.insert(index);
          }
        }
      }
    }
    const std::vector<std::size_t> sources(source_set.begin(), source_set.end());
    const auto &affected = jet.coupling.dependency_by_input.at(target);
    const std::string previous = meanings.contains(target)
                                     ? meanings.at(target)
                                     : "r" + std::to_string(target);
    const Json payload =
        {{"affected_output_indices", affected},
         {"coordinate_index", target},
         {"previous_meaning", previous},
         {"schema_version", 1},
         {"source_input_indices", sources},
         {"transform_signatures", signatures},
         {"transform_version", nonlinear_transform_version}};
    const std::string signature = contracts::sha256_json(payload);
    issues.push_back(
        {.issue_id = "polynomial-semantic:" + signature.substr(0, 24),
         .coordinate_index = target,
         .affected_output_indices = affected,
         .source_input_indices = sources,
         .previous_meaning = previous,
         .transform_signatures = std::move(signatures),
         .status = "UNRESOLVED_NONLINEAR_SEMANTICS",
         .signature = signature,
         .questions = {
             "What independent quantity does polynomial representative coordinate v" +
                 std::to_string(target) + " mean?",
             "Does the previous meaning '" + previous +
                 "' survive the multi-term nonlinear transformation?",
             "Why do source coordinates " + tuple_text(source_set) +
                 " combine into v" + std::to_string(target) + "?",
             "What evidence falsifies this proposed polynomial coordinate meaning?",
             "Is this meaning stable across multiple operating regions?"}});
  }
  return issues;
}

[[nodiscard]] PolynomialAutomorphismCandidate make_candidate(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &source, CanonicalTaylorJet transformed,
    std::vector<ExactPolynomialShear> transforms) {
  auto issues = issues_for_automorphism(form, transformed, transforms);
  std::vector<std::string> transform_signatures;
  for (const auto &transform : transforms) {
    transform_signatures.push_back(transform.transform_signature);
  }
  std::vector<std::string> issue_signatures;
  for (const auto &issue : issues) {
    issue_signatures.push_back(issue.signature);
  }
  const std::size_t coupling_score = transformed.coupling.coupling_score;
  const bool mathematical_eligible = transformed.coupling.representative;
  const Json payload =
      {{"coupling_score", coupling_score},
       {"schema_version", 1},
       {"semantic_issue_signatures", issue_signatures},
       {"source_jet_signature", source.local_behavior_signature},
       {"transform_signatures", transform_signatures},
       {"transform_version", nonlinear_transform_version},
       {"transformed_jet_signature", transformed.local_behavior_signature}};
  return {.transformed_jet = std::move(transformed),
          .transforms = std::move(transforms),
          .source_coupling_score = source.coupling.coupling_score,
          .coupling_score = coupling_score,
          .exact_invertible = true,
          .mathematical_eligible = mathematical_eligible,
          .semantic_issues = std::move(issues),
          .candidate_signature = contracts::sha256_json(payload)};
}

[[nodiscard]] bool better_candidate(
    const PolynomialAutomorphismCandidate &candidate,
    const PolynomialAutomorphismCandidate &current) {
  return candidate.coupling_score < current.coupling_score ||
         (candidate.coupling_score == current.coupling_score &&
          candidate.transforms.size() < current.transforms.size()) ||
         (candidate.coupling_score == current.coupling_score &&
          candidate.transforms.size() == current.transforms.size() &&
          candidate.candidate_signature < current.candidate_signature);
}

} // namespace

ExactPolynomialShear make_polynomial_shear(
    std::size_t input_count, std::size_t target_input_index,
    const std::vector<PolynomialShearTerm> &terms) {
  if (input_count < 1U || input_count > max_jet_inputs) {
    transform_error(
        "SAA-7.5 polynomial shear input count outside bounded range");
  }
  if (target_input_index >= input_count) {
    transform_error(
        "SAA-7.5 polynomial shear target outside input dimension");
  }
  if (terms.empty() || terms.size() > max_polynomial_shear_terms) {
    transform_error(
        "SAA-7.5 polynomial shear term count outside bounded range");
  }
  std::map<Powers, mpq_class> accumulator;
  for (const auto &term : terms) {
    const mpq_class coefficient =
        exact_value(term.coefficient, "polynomial shear coefficient");
    if (term.powers.size() != input_count ||
        std::ranges::any_of(term.powers,
                            [](int power) { return power < 0; })) {
      transform_error("SAA-7.5 polynomial shear powers are invalid");
    }
    if (term.powers[target_input_index] != 0) {
      transform_error(
          "exact polynomial shear cannot depend on its target coordinate");
    }
    const int degree = polynomial_degree(term.powers);
    if (degree < 2 || degree > static_cast<int>(max_jet_order)) {
      transform_error(
          "SAA-7.5 shear terms must be nonlinear and within Taylor order cap");
    }
    if (coefficient_bits(coefficient) >
        max_polynomial_transform_coefficient_bits) {
      transform_error(
          "SAA-7.5 shear coefficient exceeds exact complexity budget");
    }
    if (coefficient != 0) {
      accumulator[term.powers] += coefficient;
    }
  }
  std::vector<CanonicalPolynomialShearTerm> canonical;
  for (const auto &[powers, coefficient] : accumulator) {
    if (coefficient != 0) {
      canonical.push_back({.powers = powers, .coefficient = coefficient});
    }
  }
  std::ranges::sort(canonical, [](const auto &left, const auto &right) {
    return std::tuple(polynomial_degree(left.powers), left.powers) <
           std::tuple(polynomial_degree(right.powers), right.powers);
  });
  if (canonical.empty()) {
    transform_error("SAA-7.5 polynomial shear collapses to identity");
  }
  Json term_payload = Json::array();
  for (const auto &term : canonical) {
    term_payload.push_back(to_json(term));
  }
  const Json payload =
      {{"input_count", input_count},
       {"kind", "EXACT_MULTI_TERM_POLYNOMIAL_SHEAR"},
       {"schema_version", 1},
       {"target_input_index", target_input_index},
       {"terms", term_payload},
       {"transform_version", nonlinear_transform_version}};
  return {.input_count = input_count,
          .target_input_index = target_input_index,
          .terms = std::move(canonical),
          .transform_signature = contracts::sha256_json(payload)};
}

CanonicalTaylorJet apply_polynomial_shear(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet, const ExactPolynomialShear &transform) {
  if (transform.input_count != jet.input_count) {
    transform_error("SAA-7.5 polynomial shear dimension mismatches jet");
  }
  if (jet.parent_representative_behavior_signature !=
      form.representative_behavior_signature) {
    transform_error("SAA-7.5 jet does not belong to supplied SAA-6 form");
  }
  auto current = jet;
  for (const auto &term : transform.terms) {
    current = apply_nonlinear_shear(
        form, current,
        {.target_input_index = transform.target_input_index,
         .monomial_powers = term.powers,
         .coefficient = term.coefficient,
         .transform_signature = {}});
  }
  return current;
}

ExactPolynomialAutomorphism make_polynomial_automorphism(
    std::vector<ExactPolynomialShear> transforms) {
  if (transforms.empty() ||
      transforms.size() >
          static_cast<std::size_t>(max_polynomial_automorphism_depth)) {
    transform_error("SAA-7.5 automorphism depth outside bounded range");
  }
  const std::size_t input_count = transforms.front().input_count;
  if (std::ranges::any_of(transforms, [&](const auto &transform) {
        return transform.input_count != input_count;
      })) {
    transform_error("SAA-7.5 automorphism transform dimensions differ");
  }
  std::vector<std::string> signatures;
  for (const auto &transform : transforms) {
    signatures.push_back(transform.transform_signature);
  }
  const Json payload =
      {{"input_count", input_count},
       {"kind", "EXACT_POLYNOMIAL_AUTOMORPHISM"},
       {"schema_version", 1},
       {"transform_signatures", signatures},
       {"transform_version", nonlinear_transform_version}};
  return {.input_count = input_count,
          .transforms = std::move(transforms),
          .automorphism_signature = contracts::sha256_json(payload)};
}

CanonicalTaylorJet apply_polynomial_automorphism(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    const ExactPolynomialAutomorphism &automorphism) {
  auto current = jet;
  for (const auto &transform : automorphism.transforms) {
    current = apply_polynomial_shear(form, current, transform);
  }
  return current;
}

PolynomialAutomorphismSearch search_polynomial_automorphisms(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet, int max_candidates, int max_depth) {
  if (jet.parent_representative_behavior_signature !=
      form.representative_behavior_signature) {
    transform_error(
        "SAA-7.5 source jet belongs to a different representative form");
  }
  if (max_candidates < 1 ||
      max_candidates > max_polynomial_automorphism_candidates) {
    transform_error("SAA-7.5 search budget outside bounded range");
  }
  if (max_depth < 0 || max_depth > max_polynomial_automorphism_depth) {
    transform_error("SAA-7.5 search depth outside bounded range");
  }
  const auto identity = make_candidate(form, jet, jet, {});
  if (identity.mathematical_eligible) {
    const Json payload =
        {{"candidate", identity.candidate_signature},
         {"source", jet.local_behavior_signature},
         {"status", "POLYNOMIAL_REPRESENTATIVE_ALREADY_FOUND"}};
    return {.schema_version = 1,
            .transform_version = std::string(nonlinear_transform_version),
            .source_jet_signature = jet.local_behavior_signature,
            .status = "POLYNOMIAL_REPRESENTATIVE_ALREADY_FOUND",
            .representative_found = true,
            .best_candidate = identity,
            .candidates_evaluated = 0,
            .search_depth = 0,
            .budget_exhausted = false,
            .audit_hash = contracts::sha256_json(payload),
            .warnings = {"No broader nonlinear transform was required."}};
  }

  using QueueItem =
      std::pair<CanonicalTaylorJet, std::vector<ExactPolynomialShear>>;
  std::deque<QueueItem> queue;
  queue.emplace_back(jet, std::vector<ExactPolynomialShear>{});
  std::set<std::string> visited = {jet.local_behavior_signature};
  PolynomialAutomorphismCandidate best = identity;
  int evaluated = 0;
  int reached_depth = 0;
  bool exhausted = false;
  while (!queue.empty()) {
    auto [current, history] = std::move(queue.front());
    queue.pop_front();
    if (static_cast<int>(history.size()) >= max_depth) {
      continue;
    }
    for (const auto &transform : generated_grouped_shears(form, current)) {
      if (evaluated >= max_candidates) {
        exhausted = true;
        queue.clear();
        break;
      }
      ++evaluated;
      CanonicalTaylorJet transformed;
      try {
        transformed = apply_polynomial_shear(form, current, transform);
      } catch (const common::Error &) {
        continue;
      }
      if (!visited.insert(transformed.local_behavior_signature).second) {
        continue;
      }
      auto next_history = history;
      next_history.push_back(transform);
      reached_depth =
          std::max(reached_depth, static_cast<int>(next_history.size()));
      auto candidate = make_candidate(form, jet, transformed, next_history);
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
  bool found = false;
  if (best.mathematical_eligible) {
    status = "POLYNOMIAL_REPRESENTATIVE_FORM_FOUND";
    found = true;
  } else if (exhausted) {
    status = "POLYNOMIAL_AUTOMORPHISM_BUDGET_EXHAUSTED";
  } else {
    status = "POLYNOMIAL_REPRESENTATION_UNRESOLVED";
  }
  const Json payload =
      {{"best", best.candidate_signature},
       {"budget_exhausted", exhausted},
       {"depth", reached_depth},
       {"evaluated", evaluated},
       {"schema_version", 1},
       {"source", jet.local_behavior_signature},
       {"status", status},
       {"transform_version", nonlinear_transform_version}};
  return {.schema_version = 1,
          .transform_version = std::string(nonlinear_transform_version),
          .source_jet_signature = jet.local_behavior_signature,
          .status = std::move(status),
          .representative_found = found,
          .best_candidate = std::move(best),
          .candidates_evaluated = evaluated,
          .search_depth = reached_depth,
          .budget_exhausted = exhausted,
          .audit_hash = contracts::sha256_json(payload),
          .warnings = {"SAA-7.5 searches bounded exact polynomial automorphisms generated by multi-term target-independent shears; unresolved does not prove no better nonlinear representation exists."}};
}

SemanticRepresentationIssue as_semantic_issue(
    const PolynomialSemanticIssue &issue) {
  std::vector<std::string> affected_meanings;
  for (const auto output : issue.affected_output_indices) {
    affected_meanings.push_back("y" + std::to_string(output));
  }
  return {.issue_id = issue.issue_id,
          .issue_kind = "POLYNOMIAL_AUTOMORPHISM_COORDINATE",
          .coordinate_kind = "NONLINEAR_REPRESENTATIVE_INPUT",
          .coordinate_index = static_cast<int>(issue.coordinate_index),
          .coordinate_label = "v" + std::to_string(issue.coordinate_index),
          .source_input_indices = issue.source_input_indices,
          .source_coefficients = {},
          .declared_meaning = issue.previous_meaning,
          .affected_output_indices = issue.affected_output_indices,
          .affected_output_meanings = std::move(affected_meanings),
          .status = issue.status,
          .resolution_required = true,
          .questions = issue.questions,
          .source_representation_signature = {},
          .signature = issue.signature};
}

SemanticCandidateMeaning make_semantic_candidate(
    const PolynomialSemanticIssue &issue, std::string meaning,
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
    const PolynomialSemanticIssue &issue,
    const SemanticCandidateMeaning &candidate,
    std::vector<std::string> evidence_ids,
    std::vector<SemanticFalsifierResult> falsifier_results,
    bool independent_review) {
  return evaluate_semantic_candidate(
      as_semantic_issue(issue), candidate, std::move(evidence_ids),
      std::move(falsifier_results), independent_review);
}

CanonicalNonlinearRepresentativeForm canonicalize_polynomial_representative(
    const CanonicalRepresentativeAlgorithmForm &form,
    const PolynomialAutomorphismSearch &search,
    const std::vector<SemanticCandidateMeaning> &semantic_candidates,
    const std::vector<SemanticResolution> &semantic_resolutions) {
  const auto &candidate = search.best_candidate;
  if (!search.representative_found || !candidate.mathematical_eligible) {
    transform_error(
        "SAA-7.5 has no mathematically representative polynomial candidate");
  }
  auto inputs = form.inputs;
  std::ranges::sort(inputs, {}, &CanonicalRepresentativeInput::canonical_position);
  std::vector<std::string> meanings;
  for (const auto &input : inputs) {
    meanings.push_back(input.canonical_meaning);
  }
  std::map<std::string, const PolynomialSemanticIssue *> issues;
  for (const auto &issue : candidate.semantic_issues) {
    if (!issues.emplace(issue.issue_id, &issue).second) {
      transform_error("duplicate SAA-7.5 polynomial semantic issue");
    }
  }
  std::map<std::string, const SemanticCandidateMeaning *> candidates;
  for (const auto &semantic_candidate : semantic_candidates) {
    if (!candidates.emplace(semantic_candidate.issue_id, &semantic_candidate)
             .second) {
      transform_error("duplicate SAA-7.5 semantic candidate");
    }
  }
  std::map<std::string, const SemanticResolution *> resolutions;
  for (const auto &resolution : semantic_resolutions) {
    if (!resolutions.emplace(resolution.issue_id, &resolution).second) {
      transform_error("duplicate SAA-7.5 semantic resolution");
    }
  }
  if (candidates.size() != issues.size() || resolutions.size() != issues.size()) {
    if (!issues.empty()) {
      transform_error(
          "SAA-7.5 transformed coordinates require exact semantic coverage");
    }
    transform_error(
        "SAA-7.5 received semantic records for unchanged coordinates");
  }
  for (const auto &[issue_id, issue] : issues) {
    if (!candidates.contains(issue_id) || !resolutions.contains(issue_id)) {
      transform_error(
          "SAA-7.5 transformed coordinates require exact semantic coverage");
    }
    const auto *semantic_candidate = candidates.at(issue_id);
    const auto *resolution = resolutions.at(issue_id);
    if (resolution->candidate_id != semantic_candidate->candidate_id) {
      transform_error(
          "SAA-7.5 semantic resolution references different candidate");
    }
    if (resolution->status != "SEMANTICALLY_RESOLVED" ||
        !resolution->canonical_semantic_eligible ||
        resolution->semantic_fit_bp != 10000) {
      transform_error(
          "SAA-7.5 polynomial coordinate semantics remain unresolved");
    }
    meanings.at(issue->coordinate_index) =
        canonical_text(semantic_candidate->meaning);
  }
  for (const auto &[issue_id, semantic_candidate] : candidates) {
    static_cast<void>(semantic_candidate);
    if (!issues.contains(issue_id)) {
      transform_error(
          "SAA-7.5 received semantic records for unchanged coordinates");
    }
  }
  for (const auto &[issue_id, resolution] : resolutions) {
    static_cast<void>(resolution);
    if (!issues.contains(issue_id)) {
      transform_error(
          "SAA-7.5 received semantic records for unchanged coordinates");
    }
  }

  std::vector<std::string> resolution_signatures;
  for (const auto &issue : candidate.semantic_issues) {
    resolution_signatures.push_back(
        resolutions.at(issue.issue_id)->resolution_signature);
  }
  const Json semantic_payload =
      {{"parent_semantic_signature", form.semantic_representative_signature},
       {"representation_version", nonlinear_transform_version},
       {"resolved_input_meanings", meanings},
       {"schema_version", 1},
       {"semantic_resolution_signatures", resolution_signatures}};
  const std::string semantic_signature =
      contracts::sha256_json(semantic_payload);
  const Json behavior_payload =
      {{"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"representation_version", nonlinear_transform_version},
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
       {"search_audit_hash", search.audit_hash},
       {"transform_signatures", transform_signatures}};
  return {.schema_version = 1,
          .representation_version = std::string(nonlinear_transform_version),
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
              "SAA-7.5 polynomial automorphism identity remains local to the qualified Taylor scope.",
              "Exact invertibility of the coordinate map does not convert a finite jet into global nonlinear equivalence."}};
}

Json to_json(const PolynomialShearTerm &value) {
  return {{"coefficient", rational_json(value.coefficient.value())},
          {"powers", value.powers}};
}

Json to_json(const CanonicalPolynomialShearTerm &value) {
  return {{"coefficient", rational_json(value.coefficient)},
          {"powers", value.powers}};
}

Json to_json(const ExactPolynomialShear &value) {
  Json terms = Json::array();
  for (const auto &term : value.terms) {
    terms.push_back(to_json(term));
  }
  return {{"input_count", value.input_count},
          {"kind", "EXACT_MULTI_TERM_POLYNOMIAL_SHEAR"},
          {"target_input_index", value.target_input_index},
          {"terms", terms},
          {"transform_signature", value.transform_signature}};
}

Json to_json(const ExactPolynomialAutomorphism &value) {
  Json transforms = Json::array();
  std::vector<std::string> inverse_order;
  for (const auto &transform : value.transforms) {
    transforms.push_back(to_json(transform));
  }
  for (auto iterator = value.transforms.rbegin();
       iterator != value.transforms.rend(); ++iterator) {
    inverse_order.push_back(iterator->transform_signature);
  }
  return {{"automorphism_signature", value.automorphism_signature},
          {"input_count", value.input_count},
          {"inverse_order", inverse_order},
          {"kind", "EXACT_POLYNOMIAL_AUTOMORPHISM"},
          {"transforms", transforms}};
}

Json to_json(const PolynomialSemanticIssue &value) {
  return {{"affected_output_indices", value.affected_output_indices},
          {"coordinate_index", value.coordinate_index},
          {"coordinate_kind", "NONLINEAR_REPRESENTATIVE_INPUT"},
          {"issue_id", value.issue_id},
          {"issue_kind", "POLYNOMIAL_AUTOMORPHISM_COORDINATE"},
          {"previous_meaning", value.previous_meaning},
          {"questions", value.questions},
          {"resolution_required", true},
          {"signature", value.signature},
          {"source_input_indices", value.source_input_indices},
          {"status", value.status},
          {"transform_signatures", value.transform_signatures}};
}

Json to_json(const PolynomialAutomorphismCandidate &value) {
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
          {"mathematical_eligible", value.mathematical_eligible},
          {"semantic_issues", issues},
          {"source_coupling_score", value.source_coupling_score},
          {"transformed_jet", to_json(value.transformed_jet)},
          {"transforms", transforms}};
}

Json to_json(const PolynomialAutomorphismSearch &value) {
  return {{"audit_hash", value.audit_hash},
          {"best_candidate", to_json(value.best_candidate)},
          {"budget_exhausted", value.budget_exhausted},
          {"candidates_evaluated", value.candidates_evaluated},
          {"representative_found", value.representative_found},
          {"schema_version", value.schema_version},
          {"search_depth", value.search_depth},
          {"source_jet_signature", value.source_jet_signature},
          {"status", value.status},
          {"transform_version", value.transform_version},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
