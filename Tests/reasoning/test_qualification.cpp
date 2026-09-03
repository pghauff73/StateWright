#include "statewright/reasoning/qualification.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path benchmark_root() {
  return std::filesystem::path(STATEWRIGHT_RESOURCE_ROOT) / "benchmarks" /
         "reasoning";
}

} // namespace

TEST_CASE("Wilson qualification intervals match the frozen oracle") {
  REQUIRE(statewright::reasoning::wilson_interval_bp(0, 1) ==
          std::pair{0, 7'935});
  REQUIRE(statewright::reasoning::wilson_interval_bp(1, 1) ==
          std::pair{2'065, 10'000});
  REQUIRE(statewright::reasoning::wilson_interval_bp(2, 8) ==
          std::pair{715, 5'907});
  REQUIRE_THROWS_AS(statewright::reasoning::wilson_interval_bp(2, 1),
                    statewright::common::Error);
}

TEST_CASE("development benchmark qualification matches frozen identity") {
  const auto run = statewright::reasoning::load_benchmark_run(
      benchmark_root() / "baseline-v1.json");
  const auto report = statewright::reasoning::qualify_reasoning_runs({run});

  REQUIRE(report.qualification_id ==
          "reasoning-qualification:"
          "cdb6118f9557d49046fc9301e26f55122f34ca3ba6a2f7c171071664ce08ef2f");
  REQUIRE(report.signature ==
          "df91f8b65827b81398aa9f3abb260e9c2635de29c9a229fd02e4fd7780c67e78");
  REQUIRE(report.provider_identity_signature ==
          "16f0378920c0c8878638f4d65714d09acb9375731925f2cf1006c2aba28b7849");
  REQUIRE(report.difficult_accuracy_gain_bp == 7'500);
  REQUIRE(report.certificate_reproducibility_bp == 0);
  REQUIRE_FALSE(report.performance_gate_passed);
  REQUIRE_FALSE(report.performance_claim_allowed);
  REQUIRE(report.human_review_required);
  REQUIRE(report.limitations ==
          std::vector<std::string>{
              "runs are not held-out provider qualification candidates",
              "fewer than two matched repeated runs",
              "logic has only 1 held-out tasks",
              "arithmetic has only 1 held-out tasks",
              "debugging has only 1 held-out tasks",
              "scientific_inference has only 1 held-out tasks",
              "causal_reasoning has only 1 held-out tasks",
              "adversarial has only 1 held-out tasks",
              "required ablation results are incomplete",
              "certificate reproducibility is below 100 percent"});
  REQUIRE_NOTHROW(
      statewright::reasoning::require_reasoning_qualification_integrity(
          report));
}

TEST_CASE("qualification assertions and source mismatches fail closed") {
  const auto run = statewright::reasoning::load_benchmark_run(
      benchmark_root() / "baseline-v1.json");
  REQUIRE_THROWS_AS(
      statewright::reasoning::qualify_reasoning_runs({}, {}, std::nullopt),
      statewright::common::Error);
  REQUIRE_THROWS_AS(
      statewright::reasoning::qualify_reasoning_runs({run}, {}, 10'000),
      statewright::common::Error);

  auto mismatched = run;
  mismatched.source_files.front().sha256.front() =
      mismatched.source_files.front().sha256.front() == '0' ? '1' : '0';
  REQUIRE_THROWS_AS(
      statewright::reasoning::qualify_reasoning_runs({run, mismatched}),
      statewright::common::Error);
}

TEST_CASE("single-run evidence cannot establish certificate reproducibility") {
  const auto run = statewright::reasoning::load_benchmark_run(
      benchmark_root() / "runs" /
      "model-bound-2026-08-28-qwen3.8-27b-sr2a-current-source.json");
  REQUIRE(statewright::reasoning::certificate_reproducibility_bp({run}) == 0);
}
