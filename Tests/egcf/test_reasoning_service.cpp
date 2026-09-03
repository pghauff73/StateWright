#include "statewright/egcf/reasoning_service.hpp"

#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-oiec-sr-service-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::filesystem::create_directories(root);
  return root;
}

} // namespace

TEST_CASE("OIEC-SR service produces bounded advisory hypotheses and evidence requests") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::EvidenceManager evidence(store);
  egcf::Ieps ieps(evidence);
  egcf::OiecSrProposalService service(store, ieps);
  const auto result = service.propose(
      {{"assumptions", {"sensor calibration is current"}},
       {"goal", "determine the primary cause"},
       {"hypotheses", {"The sensor failed", "The wiring failed"}},
       {"mutually_exclusive", true},
       {"text", "Determine why the reading is absent."}},
      contracts::sha256_text("snapshot"), {"src/**"});
  REQUIRE(result.at("status") == "ADVISORY_PROPOSAL");
  REQUIRE_FALSE(result.at("authoritative").get<bool>());
  REQUIRE_FALSE(result.at("provider_authority").get<bool>());
  REQUIRE(result.at("claim_ids").size() == 2U);
  REQUIRE(result.at("evidence_requirement_ids").size() == 2U);
  REQUIRE(result.at("hypothesis_set").at("hypotheses").size() == 2U);
  REQUIRE(result.at("hypothesis_set").at("uncertainty_bp") == 5'000);
  REQUIRE(store.list("claim").size() == 2U);
  REQUIRE(store.list("evidence-requirement").size() == 2U);
  std::filesystem::remove_all(root);
}
