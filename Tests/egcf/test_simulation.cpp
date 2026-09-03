#include "statewright/egcf/simulation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/core/file_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class SimulationFixture final {
public:
  SimulationFixture() : root(make_root()) {}

  static std::filesystem::path make_root() {
    const auto result =
        std::filesystem::temp_directory_path() /
        ("statewright-simulation-test-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(result / "src");
    std::ofstream(result / "README.md") << "before\n";
    std::ofstream(result / "src/main.cpp") << "return 1;\n";
    return result;
  }

  ~SimulationFixture() { std::filesystem::remove_all(root); }

  std::filesystem::path root;
};

} // namespace

TEST_CASE("EGCF migration simulation emits an exact reversible diff") {
  const statewright::egcf::SimulationEngine engine;
  const statewright::contracts::Json before =
      {{"name", "old"}, {"remove_me", 4}, {"stable", true}};
  const statewright::contracts::Json operations = {
      {{"operation", "set"}, {"key", "name"}, {"value", "new"}},
      {{"operation", "add"}, {"key", "added"}, {"value", 9}},
      {{"operation", "remove"}, {"key", "remove_me"}},
      {{"operation", "rename"}, {"key", "stable"}, {"target", "kept"}}};

  const auto simulation = engine.migration(before, operations);
  REQUIRE(simulation.at("simulated") == true);
  REQUIRE(simulation.at("after") == statewright::contracts::Json(
                                        {{"added", 9},
                                         {"kept", true},
                                         {"name", "new"}}));
  REQUIRE(simulation.at("diff").size() == 5U);
  REQUIRE(simulation.at("rollback_operations").size() == 4U);

  const auto rollback = engine.rollback(simulation);
  REQUIRE(rollback.at("restored") == true);
  REQUIRE(rollback.at("observed") == before);
  REQUIRE_THROWS_AS(
      engine.migration(before,
                       {{{"operation", "add"}, {"key", "name"}}}),
      statewright::common::Error);
}

TEST_CASE("EGCF worktree simulation changes only a disposable copy") {
  SimulationFixture fixture;
  const statewright::egcf::SimulationEngine engine;
  const auto receipt = engine.worktree(
      fixture.root,
      {{{"type", "replace"},
        {"path", "src/main.cpp"},
        {"old", "1"},
        {"new", "2"}},
       {{"type", "write"}, {"path", "generated.txt"}, {"content", "new\n"}}});

  REQUIRE(receipt.at("simulated") == true);
  REQUIRE(receipt.at("disposed") == true);
  REQUIRE(receipt.at("changed") == true);
  REQUIRE(receipt.at("source_tree_hash") != receipt.at("simulated_tree_hash"));
  REQUIRE(receipt.at("changed_paths") ==
          statewright::contracts::Json({"generated.txt", "src/main.cpp"}));
  REQUIRE(statewright::core::read_text(fixture.root / "src/main.cpp") ==
          "return 1;\n");
  REQUIRE_FALSE(std::filesystem::exists(fixture.root / "generated.txt"));

  REQUIRE_THROWS_AS(
      engine.worktree(fixture.root,
                      {{{"type", "write"},
                        {"path", "../escape.txt"},
                        {"content", "forbidden"}}}),
      statewright::common::Error);
}
