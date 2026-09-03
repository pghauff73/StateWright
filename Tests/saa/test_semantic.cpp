#include "statewright/common/error.hpp"
#include "statewright/saa/semantic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

namespace {

statewright::contracts::Json semantic_mapping(std::size_t inputs,
                                              std::size_t outputs) {
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
  return {{"entry_nodes", entry_nodes},
          {"inputs", input_specs},
          {"name", "semantic-fixture"},
          {"nodes", nodes},
          {"outputs", output_specs}};
}

statewright::saa::NormalizationContract semantic_normalization(
    std::size_t inputs, std::size_t outputs) {
  statewright::saa::BoundMap input_bounds;
  statewright::saa::BoundMap output_bounds;
  for (std::size_t input = 0; input < inputs; ++input) {
    input_bounds.emplace(static_cast<int>(input),
                         statewright::saa::NumericBound(0.0, 1.0));
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    output_bounds.emplace(static_cast<int>(output),
                          statewright::saa::NumericBound(0.0, 1.0));
  }
  return statewright::saa::build_normalization_contract(
      statewright::saa::structure_from_mapping(
          semantic_mapping(inputs, outputs)),
      input_bounds, {}, {}, output_bounds,
      statewright::saa::TimeNormalization(1.0));
}

statewright::saa::LinearTransferFunction semantic_constant(std::string value) {
  return {"CONTINUOUS", {std::move(value)}, {1}};
}

statewright::saa::CanonicalMIMOCoupling semantic_canonical(
    statewright::saa::TransferFunctionMatrix matrix) {
  const std::size_t outputs = matrix.size();
  const std::size_t inputs = matrix.front().size();
  return statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS", std::move(matrix)},
      semantic_normalization(inputs, outputs));
}

} // namespace

TEST_CASE("SAA semantic assessment exposes coupled misrepresentation") {
  const auto mimo = semantic_canonical(
      {{semantic_constant("1"), semantic_constant("1/2")},
       {semantic_constant("1/4"), semantic_constant("1")}});
  const auto math = statewright::saa::assess_mimo_representation(mimo);
  const auto semantic = statewright::saa::assess_mimo_semantics(
      mimo, &math, {{0, "confidence"}, {1, "support"}},
      {{0, "acceptance"}, {1, "termination"}});
  REQUIRE(semantic.semantic_status == "SEMANTIC_MISREPRESENTATION");
  REQUIRE_FALSE(semantic.canonical_admission_eligible);
  REQUIRE(semantic.issues.size() == 2);
  REQUIRE(semantic.assessment_signature ==
          "263c517dbec41c88e0099177911d93573f5d83694696e2cec4e51b795d917424");
  REQUIRE(semantic.issues[0].issue_id ==
          "semantic:69f07235a67c34481232480e");
  REQUIRE(semantic.issues[1].issue_id ==
          "semantic:2b409b4d71a28d85787f0132");
  for (const auto &issue : semantic.issues) {
    REQUIRE(issue.status == "SEMANTIC_MISREPRESENTATION");
    REQUIRE(issue.issue_kind == "COUPLED_INPUT");
    REQUIRE(issue.affected_output_indices.size() == 2);
  }
  const auto questions =
      statewright::saa::semantic_followup_questions(semantic.issues);
  REQUIRE(std::any_of(questions.begin(), questions.end(), [](const auto &value) {
    return value.find("independent quantity") != std::string::npos;
  }));
  REQUIRE(std::any_of(questions.begin(), questions.end(), [](const auto &value) {
    return value.find("confidence") != std::string::npos;
  }));
}

TEST_CASE("SAA declared semantics remain non-authoritative") {
  const auto crossed = statewright::saa::assess_mimo_semantics(
      semantic_canonical(
          {{semantic_constant("0"), semantic_constant("2")},
           {semantic_constant("3"), semantic_constant("0")}}));
  REQUIRE(crossed.semantic_status == "UNRESOLVED_SEMANTICS");
  REQUIRE(crossed.assessment_signature ==
          "20f8f0c339ff60091742046136b92a138cea7904adb01420e589838b79bf077d");

  const auto diagonal = statewright::saa::assess_mimo_semantics(
      semantic_canonical(
          {{semantic_constant("2"), semantic_constant("0")},
           {semantic_constant("0"), semantic_constant("3")}}),
      nullptr, {{0, "temperature error"}, {1, "pressure error"}});
  REQUIRE(diagonal.mathematical_admission_eligible);
  REQUIRE_FALSE(diagonal.canonical_admission_eligible);
  REQUIRE(diagonal.semantic_status == "DECLARED_SEMANTICS");
  REQUIRE(diagonal.assessment_signature ==
          "10b4fc3edc76d4cbcbcc0629da19b0158ac15589a511a3d741461f5949de78e7");
}

TEST_CASE("SAA redundant inputs become explicit semantic issues") {
  const auto semantic = statewright::saa::assess_mimo_semantics(
      semantic_canonical(
          {{semantic_constant("1"), semantic_constant("2"),
            semantic_constant("0")},
           {semantic_constant("0"), semantic_constant("0"),
            semantic_constant("1")}}),
      nullptr, {{1, "duplicate gain"}});
  REQUIRE(semantic.semantic_status == "SEMANTIC_MISREPRESENTATION");
  REQUIRE(semantic.issues.size() == 1);
  REQUIRE(semantic.issues.front().issue_kind == "REDUNDANT_INPUT");
  REQUIRE(semantic.issues.front().coordinate_index == 1);
  REQUIRE(semantic.assessment_signature ==
          "12ee7fd2a8f47bfe3d45034cf13f595ebf09af727225316968ed2480531fcc51");
}

TEST_CASE("SAA representative coordinates require fresh semantic resolution") {
  const auto mimo = semantic_canonical(
      {{semantic_constant("1"), semantic_constant("1/2")},
       {semantic_constant("1/4"), semantic_constant("1")}});
  const auto search = statewright::saa::discover_representative_inputs(mimo);
  const auto issues =
      statewright::saa::assess_representative_candidate_semantics(
          mimo, search,
          {{0, "temperature error"}, {1, "pressure error"}},
          {{0, "temperature"}, {1, "pressure"}});
  REQUIRE(issues.size() == 2);
  REQUIRE(issues[0].issue_id == "semantic:384ef63a9fa7382b2dfbee4e");
  REQUIRE(issues[1].issue_id == "semantic:40f0c1e1ac5bbb4e34a70c4e");
  REQUIRE(issues[0].status == "UNRESOLVED_SEMANTICS");
  REQUIRE(issues[0].source_input_indices == std::vector<std::size_t>{0, 1});
  REQUIRE(issues[0].source_coefficients ==
          statewright::saa::RationalPolynomial{mpq_class(-1, 7),
                                                mpq_class(-4, 7)});
}

TEST_CASE("SAA semantic resolution requires evidence falsification and review") {
  const auto mimo = semantic_canonical(
      {{semantic_constant("1"), semantic_constant("1/2")},
       {semantic_constant("1/4"), semantic_constant("1")}});
  const auto search = statewright::saa::discover_representative_inputs(mimo);
  const auto issue =
      statewright::saa::assess_representative_candidate_semantics(
          mimo, search,
          {{0, "temperature error"}, {1, "pressure error"}},
          {{0, "temperature"}, {1, "pressure"}})
          .front();
  std::vector<int> expected_outputs;
  std::vector<int> excluded_outputs;
  for (std::size_t output = 0; output < mimo.output_count; ++output) {
    const bool affected = std::find(issue.affected_output_indices.begin(),
                                    issue.affected_output_indices.end(),
                                    output) !=
                          issue.affected_output_indices.end();
    (affected ? expected_outputs : excluded_outputs)
        .push_back(static_cast<int>(output));
  }
  const auto candidate = statewright::saa::make_semantic_candidate(
      issue, "temperature-axis error", expected_outputs, excluded_outputs,
      {"pressure state remains fixed"},
      {"temperature coordinate changes pressure output"});
  REQUIRE(candidate.epistemic_status == "MODEL_PROPOSED_SEMANTICS");
  REQUIRE(candidate.candidate_id ==
          "semantic-candidate:393b361da1f85ba8b0e86d74");
  REQUIRE(candidate.signature ==
          "393b361da1f85ba8b0e86d74ceca9da0163d6a4c19e939774c6d8df3a84a69b0");

  const auto ungrounded =
      statewright::saa::evaluate_semantic_candidate(issue, candidate);
  REQUIRE(ungrounded.status == "CANDIDATE_REPRESENTATIVE_SEMANTICS");
  REQUIRE_FALSE(ungrounded.canonical_semantic_eligible);

  const auto untested = statewright::saa::evaluate_semantic_candidate(
      issue, candidate, {"evidence:1"},
      {{"temperature coordinate changes pressure output", "UNTESTED"}}, true);
  REQUIRE(untested.status == "EVIDENCE_SUPPORTED_SEMANTICS");

  const auto resolved = statewright::saa::evaluate_semantic_candidate(
      issue, candidate, {"evidence:2", "evidence:1"},
      {{"temperature coordinate changes pressure output", "SURVIVED",
        "evidence:2"}},
      true);
  REQUIRE(resolved.status == "SEMANTICALLY_RESOLVED");
  REQUIRE(resolved.semantic_fit_bp == 10000);
  REQUIRE(resolved.canonical_semantic_eligible);
  REQUIRE(resolved.resolution_signature ==
          "93a619b1b6947d3c7c75e484b41c93fd9890efbbee091ea938432340f34ca616");

  const auto contradicted = statewright::saa::evaluate_semantic_candidate(
      issue,
      statewright::saa::make_semantic_candidate(
          issue, "wrong", excluded_outputs, expected_outputs),
      {"evidence:1"}, {}, true);
  REQUIRE(contradicted.status == "SEMANTICALLY_CONTRADICTED");
  REQUIRE(contradicted.semantic_fit_bp == 0);
}

TEST_CASE("SAA canonical semantic admission requires every resolved issue") {
  const auto mimo = semantic_canonical(
      {{semantic_constant("1"), semantic_constant("1/2")},
       {semantic_constant("1/4"), semantic_constant("1")}});
  const auto search = statewright::saa::discover_representative_inputs(mimo);
  const auto issues =
      statewright::saa::assess_representative_candidate_semantics(mimo, search);
  std::vector<statewright::saa::SemanticResolution> resolutions;
  for (std::size_t index = 0; index < issues.size(); ++index) {
    std::vector<int> expected;
    std::vector<int> excluded;
    for (std::size_t output = 0; output < mimo.output_count; ++output) {
      const bool affected = std::find(issues[index].affected_output_indices.begin(),
                                      issues[index].affected_output_indices.end(),
                                      output) !=
                            issues[index].affected_output_indices.end();
      (affected ? expected : excluded).push_back(static_cast<int>(output));
    }
    const std::string falsifier = "falsifier " + std::to_string(index);
    const auto candidate = statewright::saa::make_semantic_candidate(
        issues[index], "resolved dimension " + std::to_string(index), expected,
        excluded, {}, {falsifier});
    resolutions.push_back(statewright::saa::evaluate_semantic_candidate(
        issues[index], candidate, {"evidence:" + std::to_string(index)},
        {{falsifier, "SURVIVED"}}, true));
  }
  REQUIRE(statewright::saa::canonical_semantic_admission(true, issues,
                                                         resolutions));
  REQUIRE_FALSE(statewright::saa::canonical_semantic_admission(
      false, issues, resolutions));
  auto duplicates = resolutions;
  duplicates.push_back(resolutions.front());
  REQUIRE_THROWS_AS(
      statewright::saa::canonical_semantic_admission(true, issues, duplicates),
      statewright::common::Error);
}

TEST_CASE("SAA semantic issues propagate through every governance subsystem") {
  const auto semantic = statewright::saa::assess_mimo_semantics(
      semantic_canonical(
          {{semantic_constant("1"), semantic_constant("1/2")},
           {semantic_constant("1/4"), semantic_constant("1")}}));
  const auto directives =
      statewright::saa::propagate_semantic_issues(semantic.issues);
  REQUIRE(directives.size() == semantic.issues.size() * 7U);
  std::set<std::string> subsystems;
  for (const auto &directive : directives) {
    subsystems.insert(directive.subsystem);
  }
  const std::set<std::string> expected = {
      "ALGORITHM_STORE", "BD_DL", "CFEL", "EON",
      "HYPOTHESIS_STATE", "IURM", "OURD"};
  REQUIRE(subsystems == expected);
}
