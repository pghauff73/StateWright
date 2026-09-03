#include "statewright/egcf/brain_feed.hpp"

#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-brain-feed-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::filesystem::create_directories(root);
  return root;
}

} // namespace

TEST_CASE("brain feed grounds evidence and stages canonical candidates") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::BrainFeedProcessor processor(store);
  const auto source = contracts::sha256_text("thermal-lab");
  const auto measurement = egcf::make_brain_feed_item(
      "measurement-1", "MEASUREMENT",
      {{"content", {{"unit", "K"}, {"value", "300"}}},
       {"independence_group", "run-a"},
       {"method", "calibrated measurement"},
       {"oracle", "test thermocouple"},
       {"producer", "deterministic-test-sensor"},
       {"simulated", false},
       {"subject_id", "thermal-control"},
       {"success", true},
       {"target", "temperature"}});
  const auto semantic_concept = egcf::make_brain_feed_item(
      "concept-1", "SEMANTIC_CONCEPT",
      {{"canonical_unit", "K"},
       {"domain", "thermal control"},
       {"meaning", "thermodynamic temperature at the declared sensor"},
       {"name", "coolant temperature"},
       {"physical_dimension", {0, 0, 0, 0, 1, 0, 0}},
       {"quantity_kind", "temperature"},
       {"semantic_status", "SEMANTICALLY_RESOLVED"}},
      {}, {"measurement-1"});
  const auto algorithm = egcf::make_brain_feed_item(
      "algorithm-1", "ALGORITHM_CANDIDATE",
      {{"inputs", {"temperature"}},
       {"name", "temperature threshold"},
       {"outputs", {"flag"}},
       {"procedure", "compare temperature with threshold"}},
      {}, {"measurement-1"});
  const auto receipt = processor.feed(
      "thermal-batch", source, "thermal batch",
      {measurement, semantic_concept, algorithm});
  REQUIRE(receipt.status == "BRAIN_FEED_BATCH_ACCEPTED");
  REQUIRE(receipt.admitted_count == 2U);
  REQUIRE(receipt.staged_count == 1U);
  REQUIRE(receipt.canonical_algorithm_admissions == 0U);
  REQUIRE(receipt.dispositions.at(0).status == "REGISTERED_EVIDENCE");
  REQUIRE(receipt.dispositions.at(1).status == "ADMITTED_SEMANTIC_CONCEPT");
  REQUIRE(receipt.dispositions.at(2).status ==
          "STAGED_ALGORITHM_CANDIDATE_QUALIFICATION_REQUIRED");

  const auto duplicate = processor.feed(
      "thermal-batch-repeat", source, "thermal batch repeat",
      {measurement, semantic_concept, algorithm});
  REQUIRE(duplicate.duplicate_count == 3U);
  REQUIRE(store.list("algorithm-definition").empty());

  std::filesystem::remove(store.projection_path());
  store.rebuild_projection();
  REQUIRE(processor.dispositions().size() == 6U);
  REQUIRE(processor.batches().size() == 2U);
  std::filesystem::remove_all(root);
}

TEST_CASE("brain feed quarantines missing references and strict batches fail") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::BrainFeedProcessor processor(store);
  const auto item = egcf::make_brain_feed_item(
      "algorithm", "ALGORITHM_CANDIDATE",
      {{"inputs", contracts::Json::array()},
       {"name", "identity"},
       {"outputs", contracts::Json::array()},
       {"procedure", "return input"}},
      {}, {"missing"});
  const auto receipt = processor.feed(
      "strict", contracts::sha256_text("strict"), "strict", {item}, true);
  REQUIRE(receipt.status == "BRAIN_FEED_BATCH_STRICT_FAILURE");
  REQUIRE(receipt.quarantined_count == 1U);
  REQUIRE(receipt.canonical_algorithm_admissions == 0U);
  std::filesystem::remove_all(root);
}

TEST_CASE("repository feed is bounded stable and excludes agent state") {
  using namespace statewright;
  const auto root = temporary_root();
  std::filesystem::create_directories(root / "src");
  std::filesystem::create_directories(root / ".ourd-agent");
  std::ofstream(root / "src/example.cpp")
      << "int scaleDistance(int value) { return value * 2; }\n";
  std::ofstream(root / "README.md") << "# Example\n";
  std::ofstream(root / ".ourd-agent/ignored.json") << "{}\n";
  const auto first = egcf::scan_repository(root);
  REQUIRE(first.file_count == 2U);
  REQUIRE(first.symbol_count == 1U);
  REQUIRE(first.language_counts.at("C++") == 1U);
  const auto signature = first.repository_signature;

  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::BrainFeedProcessor processor(store);
  const auto receipt = egcf::feed_repository(processor, root);
  REQUIRE(receipt.status == "BRAIN_FEED_BATCH_ACCEPTED");
  REQUIRE(receipt.canonical_algorithm_admissions == 0U);
  REQUIRE(egcf::scan_repository(root).repository_signature == signature);
  std::filesystem::remove_all(root);
}
