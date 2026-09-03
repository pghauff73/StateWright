#include "statewright/common/error.hpp"
#include "statewright/saa/algorithm_ir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::contracts::Json binary_algorithm(std::string node_id = "combine",
                                              std::string primitive = "ADD",
                                              bool reverse = false) {
  auto operands = statewright::contracts::Json::array(
      {{{"input", 0}}, {{"input", 1}}});
  if (reverse) {
    std::reverse(operands.begin(), operands.end());
  }
  return {{"entry_nodes", {node_id}},
          {"inputs",
           {{{"name", "left"}, {"position", 0}},
            {{"name", "right"}, {"position", 1}}}},
          {"name", "display-only-name"},
          {"nodes",
           {{{"attributes",
              {{"description", "ignored documentation"},
               {"display_name", "ignored source label"}}},
             {"id", node_id},
             {"operands", operands},
             {"primitive", primitive}}}},
          {"outputs",
           {{{"name", "answer"},
             {"position", 0},
             {"source", {{"node", node_id}}}}}}};
}

} // namespace

TEST_CASE("SAA primitive aliases normalize to a fixed vocabulary") {
  REQUIRE(statewright::saa::normalize_primitive("+").name == "ADD");
  REQUIRE(statewright::saa::normalize_primitive("mul").name == "MULTIPLY");
  REQUIRE(statewright::saa::normalize_primitive("ADD").commutative);
  REQUIRE_THROWS_AS(statewright::saa::normalize_primitive("invented-operation"),
                    statewright::common::Error);
}

TEST_CASE("SAA canonical IR matches the frozen Python oracle") {
  const auto fixture = load_fixtures().at("saa_ir_case");
  const auto value = statewright::saa::canonicalize_mapping(binary_algorithm());
  REQUIRE(statewright::saa::to_json(value) == fixture.at("add"));
}

TEST_CASE("SAA structural identity ignores names and commutative order") {
  auto renamed = binary_algorithm("node_947");
  renamed["name"] = "A";
  renamed["inputs"][0]["name"] = "temperature";
  renamed["metadata"] = {{"author", "different"}};
  const auto first = statewright::saa::canonicalize_mapping(binary_algorithm());
  const auto second = statewright::saa::canonicalize_mapping(renamed);
  const auto reversed =
      statewright::saa::canonicalize_mapping(binary_algorithm("sum", "ADD", true));
  REQUIRE(first.structural_hash == second.structural_hash);
  REQUIRE(first.structural_hash == reversed.structural_hash);
}

TEST_CASE("SAA noncommutative order and control semantics carry identity") {
  const auto forward = statewright::saa::canonicalize_mapping(
      binary_algorithm("subtract", "SUBTRACT", false));
  const auto reverse = statewright::saa::canonicalize_mapping(
      binary_algorithm("subtract", "SUBTRACT", true));
  REQUIRE(forward.structural_hash != reverse.structural_hash);
}

TEST_CASE("SAA strict structural validation fails closed") {
  auto unknown = binary_algorithm();
  unknown["mystery"] = true;
  REQUIRE_THROWS_AS(statewright::saa::canonicalize_mapping(unknown),
                    statewright::common::Error);
  auto positions = binary_algorithm();
  positions["inputs"][1]["position"] = 3;
  REQUIRE_THROWS_AS(statewright::saa::canonicalize_mapping(positions),
                    statewright::common::Error);
}

TEST_CASE("SAA high symmetry is explicitly downgraded") {
  auto nodes = statewright::contracts::Json::array();
  for (int index = 0; index < 5; ++index) {
    nodes.push_back({{"id", "c" + std::to_string(index)},
                     {"operands", {{{"constant", 1}}}},
                     {"primitive", "CONST"}});
  }
  nodes.push_back({{"id", "result"},
                   {"operands", {{{"node", "c0"}}, {{"node", "c1"}}}},
                   {"primitive", "ADD"}});
  const auto result = statewright::saa::canonicalize_mapping(
      {{"name", "high-symmetry"},
       {"nodes", nodes},
       {"outputs", {{{"position", 0},
                      {"source", {{"node", "result"}}}}}}},
      2);
  REQUIRE(result.canonicalization_strength == "REFINED_FINGERPRINT");
  REQUIRE(result.exact_permutations_considered == 0U);
  REQUIRE_FALSE(result.warnings.empty());
}
