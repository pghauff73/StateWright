#include "statewright/common/error.hpp"
#include "statewright/saa/representative_form.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <tuple>

namespace {

statewright::contracts::Json saa6_mapping(std::size_t inputs,
                                         std::size_t outputs,
                                         bool extra_node = false) {
  statewright::contracts::Json nodes = statewright::contracts::Json::array();
  statewright::contracts::Json input_specs =
      statewright::contracts::Json::array();
  statewright::contracts::Json output_specs =
      statewright::contracts::Json::array();
  statewright::contracts::Json entry_nodes =
      statewright::contracts::Json::array();
  for (std::size_t input = 0; input < inputs; ++input) {
    input_specs.push_back({{"data_type", "scalar"},
                           {"name", "u" + std::to_string(input)},
                           {"position", input}});
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    const std::string node = "out" + std::to_string(output);
    nodes.push_back({{"id", node},
                     {"operands", {{{"constant", 0}}}},
                     {"primitive", "CONST"}});
    output_specs.push_back({{"data_type", "scalar"},
                            {"name", "y" + std::to_string(output)},
                            {"position", output},
                            {"source", {{"node", node}}}});
    entry_nodes.push_back(node);
  }
  if (extra_node) {
    nodes.push_back({{"id", "extra"},
                     {"operands", {{{"constant", 1}}}},
                     {"primitive", "CONST"}});
    entry_nodes.push_back("extra");
  }
  return {{"entry_nodes", entry_nodes},
          {"inputs", input_specs},
          {"name", "saa6-fixture"},
          {"nodes", nodes},
          {"outputs", output_specs}};
}

statewright::saa::NormalizationContract saa6_normalization(
    std::size_t inputs, std::size_t outputs, bool approximate = false) {
  const std::string kind = approximate ? "ENGINEERING_BOUND" : "EXACT_BOUND";
  statewright::saa::BoundMap input_bounds;
  statewright::saa::BoundMap output_bounds;
  for (std::size_t input = 0; input < inputs; ++input) {
    input_bounds.emplace(static_cast<int>(input),
                         statewright::saa::NumericBound(0.0, 1.0, kind));
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    output_bounds.emplace(static_cast<int>(output),
                          statewright::saa::NumericBound(0.0, 1.0, kind));
  }
  return statewright::saa::build_normalization_contract(
      statewright::saa::structure_from_mapping(saa6_mapping(inputs, outputs)),
      input_bounds, {}, {}, output_bounds,
      statewright::saa::TimeNormalization(1.0));
}

statewright::saa::LinearTransferFunction saa6_constant(std::string value) {
  return {"CONTINUOUS", {std::move(value)}, {1}};
}

struct FormFixture final {
  statewright::saa::CanonicalRepresentativeAlgorithmForm form;
  statewright::saa::CanonicalMIMOCoupling mimo;
  statewright::saa::RepresentativeInputSearch search;
  statewright::saa::NormalizationContract normalization;
};

FormFixture build_form(statewright::saa::TransferFunctionMatrix rows,
                       std::map<int, std::string> meanings = {},
                       bool structural_extra = false) {
  const std::size_t outputs = rows.size();
  const std::size_t inputs = rows.front().size();
  auto normalization = saa6_normalization(inputs, outputs);
  auto mimo = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS", std::move(rows)}, normalization);
  auto search = statewright::saa::discover_representative_inputs(mimo);
  auto issues =
      statewright::saa::assess_representative_candidate_semantics(mimo, search);
  std::vector<statewright::saa::SemanticCandidateMeaning> candidates;
  std::vector<statewright::saa::SemanticResolution> resolutions;
  for (const auto &issue : issues) {
    std::vector<int> expected;
    std::vector<int> excluded;
    for (std::size_t output = 0; output < outputs; ++output) {
      const bool affected = std::find(issue.affected_output_indices.begin(),
                                      issue.affected_output_indices.end(),
                                      output) !=
                            issue.affected_output_indices.end();
      (affected ? expected : excluded).push_back(static_cast<int>(output));
    }
    const auto found = meanings.find(issue.coordinate_index);
    const std::string meaning =
        found == meanings.end()
            ? "representative effect on y" +
                  (expected.empty() ? std::string("none")
                                    : std::to_string(expected.front()))
            : found->second;
    const std::string falsifier =
        "coordinate " + std::to_string(issue.coordinate_index) +
        " changes an excluded output";
    candidates.push_back(statewright::saa::make_semantic_candidate(
        issue, meaning, expected, excluded, {}, {falsifier}));
    resolutions.push_back(statewright::saa::evaluate_semantic_candidate(
        issue, candidates.back(),
        {"evidence:" + std::to_string(issue.coordinate_index)},
        {{falsifier, "SURVIVED",
          "evidence:" + std::to_string(issue.coordinate_index)}},
        true));
  }
  auto form = statewright::saa::canonicalize_representative_algorithm(
      statewright::saa::canonicalize_mapping(
          saa6_mapping(inputs, outputs, structural_extra)),
      normalization, mimo, search, issues, candidates, resolutions);
  return {.form = std::move(form),
          .mimo = std::move(mimo),
          .search = std::move(search),
          .normalization = std::move(normalization)};
}

} // namespace

TEST_CASE("SAA canonical representative form derives exact source-box bounds") {
  const auto fixture = build_form(
      {{saa6_constant("1"), saa6_constant("1/2")},
       {saa6_constant("1/4"), saa6_constant("1")}});
  REQUIRE(fixture.form.canonical_admission_eligible);
  REQUIRE(fixture.form.store_status ==
          "ELIGIBLE_CANONICAL_REPRESENTATIVE_FORM");
  REQUIRE(fixture.form.inputs.size() == 2);
  REQUIRE(fixture.form.canonical_input_permutation ==
          std::vector<std::size_t>{1, 0});
  REQUIRE(fixture.form.mathematical_representative_signature ==
          "250e08fe9ef975160f1cba883306be5a5ebfeefffc97671d8570562227b71d54");
  REQUIRE(fixture.form.semantic_representative_signature ==
          "77a8f2c296bde67c4f4d68aca3d8f3bccbf04b93d597fbd70c88da1a65923355");
  REQUIRE(fixture.form.representative_behavior_signature ==
          "6d865bffe9efe8214a7f4a3a630c8779972355cd89472b4aae12aa49e906d54d");
  REQUIRE(fixture.form.canonical_algorithm_signature ==
          "8ecaab8a5ad2ad1c9d4b11557d64346086c330cd4f3d1b914fb283314a27c1d9");
  REQUIRE(fixture.form.audit_hash ==
          "09c1a380a3aa991ee38aac3bcbaceb95b52b859fd43c73ca9452850fb0c46e7b");

  std::map<std::size_t,
           const statewright::saa::CanonicalRepresentativeInput *>
      by_original;
  for (const auto &input : fixture.form.inputs) {
    by_original.emplace(input.candidate_input_index, &input);
  }
  REQUIRE(by_original.at(1)->boundary.raw_minimum == 0);
  REQUIRE(by_original.at(1)->boundary.raw_maximum == mpq_class(12, 7));
  REQUIRE(by_original.at(0)->boundary.raw_minimum == mpq_class(-5, 7));
  REQUIRE(by_original.at(0)->boundary.raw_maximum == 0);
  for (const auto &[index, input] : by_original) {
    static_cast<void>(index);
    REQUIRE(statewright::saa::normalize_representative_value(
                input->boundary, input->boundary.raw_minimum) == 0);
    REQUIRE(statewright::saa::normalize_representative_value(
                input->boundary, input->boundary.raw_maximum) == 1);
    REQUIRE(statewright::saa::denormalize_representative_value(
                input->boundary, 0) == input->boundary.raw_minimum);
    REQUIRE(statewright::saa::denormalize_representative_value(
                input->boundary, 1) == input->boundary.raw_maximum);
  }
}

TEST_CASE("SAA selector representative coordinates retain unit bounds") {
  const auto fixture = build_form(
      {{saa6_constant("2"), saa6_constant("0")},
       {saa6_constant("0"), saa6_constant("3")}});
  REQUIRE(fixture.form.inputs.size() == 2);
  REQUIRE(fixture.form.mathematical_representative_signature ==
          "365287109ba8f23f5b7e4ae2e06cb8ba736f0f2d207098e97b7379f332e7f3f0");
  REQUIRE(fixture.form.audit_hash ==
          "9631e8c2ae970952d096e520e462e29af24590723d78bcf93906292e789bdc0c");
  for (const auto &input : fixture.form.inputs) {
    REQUIRE(input.boundary.raw_minimum == 0);
    REQUIRE(input.boundary.raw_maximum == 1);
    REQUIRE(input.boundary.raw_width == 1);
  }
  REQUIRE_THROWS_AS(statewright::saa::normalize_representative_value(
                        fixture.form.inputs.front().boundary, 2),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::saa::denormalize_representative_value(
                        fixture.form.inputs.front().boundary,
                        mpq_class(3, 2)),
                    statewright::common::Error);
}

TEST_CASE("SAA canonical representative admission rejects unresolved semantics") {
  const std::size_t inputs = 2;
  const std::size_t outputs = 2;
  auto normalization = saa6_normalization(inputs, outputs);
  const auto mimo = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{saa6_constant("2"), saa6_constant("0")},
        {saa6_constant("0"), saa6_constant("3")}}},
      normalization);
  const auto search = statewright::saa::discover_representative_inputs(mimo);
  const auto issues =
      statewright::saa::assess_representative_candidate_semantics(mimo, search);
  std::vector<statewright::saa::SemanticCandidateMeaning> candidates;
  std::vector<statewright::saa::SemanticResolution> resolutions;
  for (const auto &issue : issues) {
    std::vector<int> expected;
    for (const auto output : issue.affected_output_indices) {
      expected.push_back(static_cast<int>(output));
    }
    candidates.push_back(statewright::saa::make_semantic_candidate(
        issue, "meaning " + std::to_string(issue.coordinate_index), expected));
    resolutions.push_back(statewright::saa::evaluate_semantic_candidate(
        issue, candidates.back()));
  }
  REQUIRE_THROWS_AS(
      statewright::saa::canonicalize_representative_algorithm(
          statewright::saa::canonicalize_mapping(saa6_mapping(2, 2)),
          normalization, mimo, search, issues, candidates, resolutions),
      statewright::common::Error);
}

TEST_CASE("SAA canonical representative admission requires exact normalization") {
  auto exact = saa6_normalization(2, 2);
  const auto mimo = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{saa6_constant("2"), saa6_constant("0")},
        {saa6_constant("0"), saa6_constant("3")}}},
      exact);
  const auto search = statewright::saa::discover_representative_inputs(mimo);
  const auto issues =
      statewright::saa::assess_representative_candidate_semantics(mimo, search);
  std::vector<statewright::saa::SemanticCandidateMeaning> candidates;
  std::vector<statewright::saa::SemanticResolution> resolutions;
  for (const auto &issue : issues) {
    std::vector<int> expected;
    std::vector<int> excluded;
    for (std::size_t output = 0; output < 2; ++output) {
      const bool affected = std::find(issue.affected_output_indices.begin(),
                                      issue.affected_output_indices.end(),
                                      output) != issue.affected_output_indices.end();
      (affected ? expected : excluded).push_back(static_cast<int>(output));
    }
    const std::string falsifier = "f" + std::to_string(issue.coordinate_index);
    candidates.push_back(statewright::saa::make_semantic_candidate(
        issue, "meaning", expected, excluded, {}, {falsifier}));
    resolutions.push_back(statewright::saa::evaluate_semantic_candidate(
        issue, candidates.back(), {"evidence"},
        {{falsifier, "SURVIVED"}}, true));
  }
  REQUIRE_THROWS_AS(
      statewright::saa::canonicalize_representative_algorithm(
          statewright::saa::canonicalize_mapping(saa6_mapping(2, 2)),
          saa6_normalization(2, 2, true), mimo, search, issues, candidates,
          resolutions),
      statewright::common::Error);
}

TEST_CASE("SAA zero behavior admits a zero-input canonical form") {
  const auto fixture = build_form(
      {{saa6_constant("0"), saa6_constant("0")},
       {saa6_constant("0"), saa6_constant("0")}});
  REQUIRE(fixture.form.representative_input_count == 0);
  REQUIRE(fixture.form.inputs.empty());
  REQUIRE(std::all_of(fixture.form.normalized_channels.begin(),
                      fixture.form.normalized_channels.end(),
                      [](const auto &row) { return row.empty(); }));
  REQUIRE(fixture.form.canonical_admission_eligible);
  REQUIRE(fixture.form.mathematical_representative_signature ==
          "8b122868750d99be2d6ce43e977e48dc3f5103ff66065d1272304a2395fb7368");
  REQUIRE(fixture.form.audit_hash ==
          "35d67d2a36d9dbe24e678684ad210f0916d0e2cc0f8cdd6518e989622b554b98");
}

TEST_CASE("SAA representative identity removes port order but preserves structure") {
  const auto diagonal = build_form(
      {{saa6_constant("2"), saa6_constant("0")},
       {saa6_constant("0"), saa6_constant("3")}},
      {{0, "effect zero"}, {1, "effect one"}});
  const auto crossed = build_form(
      {{saa6_constant("0"), saa6_constant("2")},
       {saa6_constant("3"), saa6_constant("0")}},
      {{0, "effect one"}, {1, "effect zero"}});
  REQUIRE(crossed.search.best_candidate->preferred_input_to_output_pairing ==
          std::vector<std::size_t>{1, 0});
  REQUIRE(diagonal.form.mathematical_representative_signature ==
          crossed.form.mathematical_representative_signature);
  REQUIRE(diagonal.form.semantic_representative_signature ==
          crossed.form.semantic_representative_signature);
  REQUIRE(diagonal.form.representative_behavior_signature ==
          crossed.form.representative_behavior_signature);

  const auto upper = build_form(
      {{saa6_constant("2"), saa6_constant("0")},
       {saa6_constant("0"), saa6_constant("3")}},
      {{0, "Temperature Deviation"}, {1, "Pressure Deviation"}});
  const auto lower = build_form(
      {{saa6_constant("2"), saa6_constant("0")},
       {saa6_constant("0"), saa6_constant("3")}},
      {{0, "temperature deviation"}, {1, "pressure deviation"}});
  REQUIRE(upper.form.semantic_representative_signature ==
          lower.form.semantic_representative_signature);
  REQUIRE(upper.form.representative_behavior_signature ==
          lower.form.representative_behavior_signature);

  const auto ordinary = build_form(
      {{saa6_constant("2"), saa6_constant("0")},
       {saa6_constant("0"), saa6_constant("3")}},
      {{0, "dimension zero"}, {1, "dimension one"}}, false);
  const auto extra = build_form(
      {{saa6_constant("2"), saa6_constant("0")},
       {saa6_constant("0"), saa6_constant("3")}},
      {{0, "dimension zero"}, {1, "dimension one"}}, true);
  REQUIRE(ordinary.form.representative_behavior_signature ==
          extra.form.representative_behavior_signature);
  REQUIRE(ordinary.form.source_structural_hash !=
          extra.form.source_structural_hash);
  REQUIRE(ordinary.form.canonical_algorithm_signature !=
          extra.form.canonical_algorithm_signature);
  REQUIRE(ordinary.form.structural_binding_policy_value ==
          "CONSERVATIVE_SOURCE_STRUCTURE_BINDING");
}

TEST_CASE("SAA canonical representative form is deterministic") {
  const auto first = build_form(
      {{saa6_constant("1"), saa6_constant("1/2")},
       {saa6_constant("1/4"), saa6_constant("1")}},
      {{0, "first latent dimension"}, {1, "second latent dimension"}});
  const auto second = build_form(
      {{saa6_constant("1"), saa6_constant("1/2")},
       {saa6_constant("1/4"), saa6_constant("1")}},
      {{0, "first latent dimension"}, {1, "second latent dimension"}});
  REQUIRE(first.form.audit_hash == second.form.audit_hash);
  REQUIRE(first.form.canonical_algorithm_signature ==
          second.form.canonical_algorithm_signature);
  REQUIRE(statewright::saa::to_json(first.form) ==
          statewright::saa::to_json(second.form));
}
