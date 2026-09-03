#include "statewright/saa/failure_algebra.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SAA canonical failure algebra blocks exact repeated retries") {
  using namespace statewright::saa;
  const auto observation = make_failure_observation(
      " Experiment ", " Mathematical Algorithm ", "experiment_regression",
      "Error increased after adaptation", {"output error", "output error"},
      {"bounded"}, std::string(64U, '1'), std::string(64U, '2'),
      {"evidence:z", "evidence:a", "evidence:a"},
      "experiment:fixture");
  REQUIRE(observation.observation_signature ==
          "1d07e1f1ef5af10d86ac12668bb33753733d801731707755d008f73b4e9d8f83");
  const auto pattern = canonicalize_failure(observation);
  REQUIRE(pattern.pattern_signature ==
          "ec36f674268dfb25f458ec610ae2ee4ab5eb076f017070de6d600e021a890dcb");
  const auto match = compare_failure_to_pattern(observation, pattern, 2);
  REQUIRE(match.status == "EXACT_CANONICAL_FAILURE_MATCH");
  REQUIRE(match.exact_match);
  REQUIRE(match.retry_blocked);
  REQUIRE(match.assessment_signature ==
          "613374095d00f39c06e33af920f98734c111a6a8668e18760215dd9c496de0c0");
}

TEST_CASE("SAA canonical failure algebra distinguishes changed scope") {
  using namespace statewright::saa;
  const auto baseline = make_failure_observation(
      "experiment", "mathematical algorithm", "EXPERIMENT_REGRESSION",
      "error increased after adaptation", {"output error"}, {"bounded"},
      std::string(64U, '1'), std::string(64U, '2'), {"evidence:a"},
      "experiment:fixture");
  const auto changed = make_failure_observation(
      "experiment", "mathematical algorithm", "EXPERIMENT_REGRESSION",
      "error increased after adaptation", {"output error"}, {"bounded"},
      std::string(64U, '3'), std::string(64U, '2'), {"evidence:b"},
      "experiment:second");
  REQUIRE(changed.observation_signature ==
          "8347cf45071cec24f8667339edaa923eebb903d35d913943c03a4aecc3d9f0cd");
  const auto match =
      compare_failure_to_pattern(changed, canonicalize_failure(baseline));
  REQUIRE(match.status == "SAME_FAILURE_MECHANISM_DIFFERENT_SCOPE");
  REQUIRE_FALSE(match.exact_match);
  REQUIRE_FALSE(match.retry_blocked);
  REQUIRE(match.differences ==
          std::vector<std::string>{"BOUNDARY_SIGNATURE"});
  REQUIRE(match.assessment_signature ==
          "e4dc35e3c5626f6b876bc7a3b0ea978f87af1cdeec59bed89b8302b3b3d7c39c");
}
