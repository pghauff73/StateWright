#include "statewright/common/error.hpp"
#include "statewright/saa/search.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::contracts::Json add_algorithm(std::string primitive = "ADD") {
  return {{"entry_nodes", {"combine"}},
          {"inputs", {{{"position", 0}}, {{"position", 1}}}},
          {"nodes",
           {{{"id", "combine"},
             {"operands", {{{"input", 0}}, {{"input", 1}}}},
             {"primitive", primitive}}}},
          {"outputs",
           {{{"position", 0}, {"source", {{"node", "combine"}}}}}}};
}

} // namespace

TEST_CASE("searchable SAA algorithms have content-addressed semantic identity") {
  const auto ir = statewright::saa::canonicalize_mapping(add_algorithm());
  const auto first = statewright::saa::make_searchable_algorithm(
      ir, "Numerical Control", {"accumulation", "sum", "sum"},
      {"bounded output"}, {"evidence:1"}, "QUALIFIED", "snapshot");
  const auto second = statewright::saa::make_searchable_algorithm(
      ir, " numerical   control ", {"SUM", "accumulation"},
      {"bounded output"}, {"evidence:1"}, "QUALIFIED", "snapshot");
  REQUIRE(first.canonical_algorithm_id == second.canonical_algorithm_id);
  REQUIRE(first.signature == second.signature);
}

TEST_CASE("searchable SAA retrieval is deterministic and explained") {
  statewright::saa::AlgorithmSearchIndex index;
  const auto add = statewright::saa::make_searchable_algorithm(
      statewright::saa::canonicalize_mapping(add_algorithm()), "control",
      {"sum", "aggregation"}, {"bounded"}, {"e1"}, "QUALIFIED");
  const auto subtract = statewright::saa::make_searchable_algorithm(
      statewright::saa::canonicalize_mapping(add_algorithm("SUBTRACT")),
      "control", {"difference"}, {"bounded"}, {"e2"}, "CANDIDATE");
  const auto add_id = index.register_algorithm(add);
  const auto subtract_id = index.register_algorithm(subtract);
  REQUIRE(add_id != subtract_id);

  const auto first = index.search({.structural_hash = std::nullopt,
                                   .domain = "CONTROL",
                                   .required_primitives = {"+"},
                                   .semantic_terms = {"sum"},
                                   .required_invariants = {"bounded"},
                                   .require_qualified = true,
                                   .limit = 10U});
  const auto second = index.search({.structural_hash = std::nullopt,
                                    .domain = "control",
                                    .required_primitives = {"ADD"},
                                    .semantic_terms = {"SUM"},
                                    .required_invariants = {"bounded"},
                                    .require_qualified = true,
                                    .limit = 10U});
  REQUIRE(first.selected_algorithm_id == add_id);
  REQUIRE(first.result_signature == second.result_signature);
  REQUIRE(first.candidates.size() == 1U);
  REQUIRE(first.excluded.size() == 1U);
  REQUIRE(first.excluded.front().at("algorithm_id") == subtract_id);
  REQUIRE(first.excluded.front().at("reasons").get<std::vector<std::string>>() ==
          std::vector<std::string>{"missing_primitives",
                                   "no_semantic_term_match",
                                   "not_qualified"});
}

TEST_CASE("searchable SAA immutable identity collisions fail closed") {
  statewright::saa::AlgorithmSearchIndex index;
  auto algorithm = statewright::saa::make_searchable_algorithm(
      statewright::saa::canonicalize_mapping(add_algorithm()), "control",
      {"sum"});
  const auto identity = index.register_algorithm(algorithm);
  REQUIRE(index.register_algorithm(algorithm) == identity);
  algorithm.evidence_ids.push_back("different");
  algorithm.signature.clear();
  REQUIRE_THROWS_AS(index.register_algorithm(algorithm),
                    statewright::common::Error);
}

TEST_CASE("searchable SAA rejects unbounded result requests") {
  const statewright::saa::AlgorithmSearchIndex index;
  const statewright::saa::AlgorithmSearchQuery zero{
      .structural_hash = std::nullopt,
      .domain = std::nullopt,
      .required_primitives = {},
      .semantic_terms = {},
      .required_invariants = {},
      .require_qualified = true,
      .limit = 0U};
  const statewright::saa::AlgorithmSearchQuery excessive{
      .structural_hash = std::nullopt,
      .domain = std::nullopt,
      .required_primitives = {},
      .semantic_terms = {},
      .required_invariants = {},
      .require_qualified = true,
      .limit = 65U};
  REQUIRE_THROWS_AS(index.search(zero), statewright::common::Error);
  REQUIRE_THROWS_AS(index.search(excessive), statewright::common::Error);
}
