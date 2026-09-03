#pragma once

#include "statewright/saa/representative_form.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace statewright::tests {

inline contracts::Json nonlinear_mapping(std::size_t inputs,
                                         std::size_t outputs) {
  contracts::Json nodes = contracts::Json::array();
  contracts::Json input_specs = contracts::Json::array();
  contracts::Json output_specs = contracts::Json::array();
  contracts::Json entry_nodes = contracts::Json::array();
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
          {"name", "saa6-fixture"},
          {"nodes", nodes},
          {"outputs", output_specs}};
}

inline saa::NormalizationContract nonlinear_normalization(std::size_t inputs,
                                                          std::size_t outputs) {
  saa::BoundMap input_bounds;
  saa::BoundMap output_bounds;
  for (std::size_t input = 0; input < inputs; ++input) {
    input_bounds.emplace(static_cast<int>(input),
                         saa::NumericBound(0.0, 1.0, "EXACT_BOUND"));
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    output_bounds.emplace(static_cast<int>(output),
                          saa::NumericBound(0.0, 1.0, "EXACT_BOUND"));
  }
  return saa::build_normalization_contract(
      saa::structure_from_mapping(nonlinear_mapping(inputs, outputs)),
      input_bounds, {}, {}, output_bounds, saa::TimeNormalization(1.0));
}

inline saa::LinearTransferFunction nonlinear_constant(std::string value) {
  return {"CONTINUOUS", {std::move(value)}, {1}};
}

inline saa::CanonicalRepresentativeAlgorithmForm
nonlinear_parent_form(const std::vector<std::string> &meanings);

inline saa::CanonicalRepresentativeAlgorithmForm nonlinear_parent_form(
    std::string first_meaning = "temperature control",
    std::string second_meaning = "pressure control") {
  return nonlinear_parent_form(
      std::vector<std::string>{std::move(first_meaning),
                               std::move(second_meaning)});
}

inline saa::CanonicalRepresentativeAlgorithmForm
nonlinear_parent_form(const std::vector<std::string> &meanings) {
  const std::size_t dimensions = meanings.size();
  if (dimensions == 0U) {
    throw std::invalid_argument("nonlinear fixture requires a dimension");
  }
  auto normalization = nonlinear_normalization(dimensions, dimensions);
  saa::TransferFunctionMatrix rows;
  for (std::size_t output = 0; output < dimensions; ++output) {
    std::vector<saa::LinearTransferFunction> row;
    for (std::size_t input = 0; input < dimensions; ++input) {
      row.push_back(nonlinear_constant(output == input ? "1" : "0"));
    }
    rows.push_back(std::move(row));
  }
  auto mimo = saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS", std::move(rows)}, normalization);
  auto search = saa::discover_representative_inputs(mimo);
  auto issues = saa::assess_representative_candidate_semantics(mimo, search);
  std::vector<saa::SemanticCandidateMeaning> candidates;
  std::vector<saa::SemanticResolution> resolutions;
  for (const auto &issue : issues) {
    std::vector<int> expected;
    std::vector<int> excluded;
    for (std::size_t output = 0; output < dimensions; ++output) {
      const bool affected =
          std::find(issue.affected_output_indices.begin(),
                    issue.affected_output_indices.end(), output) !=
          issue.affected_output_indices.end();
      (affected ? expected : excluded).push_back(static_cast<int>(output));
    }
    const std::string falsifier =
        "coordinate " + std::to_string(issue.coordinate_index) +
        " changes an excluded output";
    candidates.push_back(saa::make_semantic_candidate(
        issue, meanings.at(static_cast<std::size_t>(issue.coordinate_index)),
        expected, excluded, {},
        {falsifier}));
    resolutions.push_back(saa::evaluate_semantic_candidate(
        issue, candidates.back(),
        {"evidence:" + std::to_string(issue.coordinate_index)},
        {{falsifier, "SURVIVED",
          "evidence:" + std::to_string(issue.coordinate_index)}},
        true));
  }
  return saa::canonicalize_representative_algorithm(
      saa::canonicalize_mapping(nonlinear_mapping(dimensions, dimensions)),
      normalization, mimo, search, issues, candidates, resolutions);
}

} // namespace statewright::tests
