#include "statewright/saa/semantic_alignment.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::SemanticEvidenceResolver grounded_resolver() {
  return [](std::string_view evidence_id)
             -> std::optional<statewright::saa::SemanticGroundingEvidence> {
    if (!evidence_id.starts_with("ev:")) {
      return std::nullopt;
    }
    return statewright::saa::SemanticGroundingEvidence{
        .object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "human-semantic-test",
        .method = "independent-semantic-review"};
  };
}

statewright::saa::SemanticConcept aligned_concept(
    std::string evidence_id, std::string name, std::string meaning,
    std::string quantity,
    std::optional<statewright::saa::PhysicalDimensionVector> dimension,
    std::string domain, std::vector<std::string> aliases = {},
    std::optional<statewright::saa::PhysicalUnit> unit = std::nullopt) {
  return statewright::saa::make_semantic_concept(
      std::move(name), std::move(meaning), std::move(domain),
      std::move(quantity), std::move(aliases), std::move(dimension),
      std::move(unit), {std::move(evidence_id)});
}

} // namespace

TEST_CASE("SAA semantic alignment admits reviewed cross-domain equivalence") {
  using namespace statewright::saa;
  const auto velocity = LENGTH / TIME;
  const auto left = aligned_concept(
      "ev:left", "vehicle speed",
      "magnitude of vehicle translational velocity", "speed", velocity,
      "vehicle dynamics", {"road speed"});
  const auto right = aligned_concept(
      "ev:right", "translational speed",
      "magnitude of translational velocity", "speed", velocity,
      "mechanics");
  const std::string falsifier =
      "equal numeric values under the same frame predict different translational displacement rates";
  const auto proposal = propose_semantic_alignment(
      left, right, "EXACT_EQUIVALENT",
      "magnitude of translational velocity in a specified frame", true,
      {"ev:alignment"}, {falsifier}, true);
  REQUIRE(proposal.proposal_signature ==
          "9dbd5f1a9216d2683615053a32ce7815ab714ed00f4b5b1232869a650bd7201d");
  const auto assessment = assess_semantic_alignment(
      grounded_resolver(), left, right, proposal,
      {SemanticAlignmentFalsifierResult(falsifier, "SURVIVED",
                                        "ev:alignment")});
  REQUIRE(assessment.status ==
          "EXACT_CROSS_DOMAIN_SEMANTIC_EQUIVALENCE");
  REQUIRE(assessment.exact_substitution_eligible);
  REQUIRE(assessment.canonical_alignment_eligible);
  REQUIRE(assessment.alignment_signature ==
          "beef65327a6212b750b081568c1c64aae9454ab4bfe530f1ed1d3408a6f961c0");
}

TEST_CASE("SAA semantic alignment rejects dimensional contradiction") {
  using namespace statewright::saa;
  const auto length = aligned_concept("ev:left", "length", "spatial extent",
                                      "length", LENGTH, "mechanics", {},
                                      METRE);
  const auto duration = aligned_concept(
      "ev:right", "duration", "elapsed time", "duration", TIME,
      "mechanics", {}, SECOND);
  const auto proposal = propose_semantic_alignment(
      length, duration, "EXACT_EQUIVALENT", "same thing", true,
      {"ev:alignment"}, {}, true);
  const auto assessment = assess_semantic_alignment(
      grounded_resolver(), length, duration, proposal);
  REQUIRE(assessment.status == "SEMANTIC_ALIGNMENT_CONTRADICTED");
  REQUIRE_FALSE(assessment.exact_substitution_eligible);
  REQUIRE(assessment.alignment_signature ==
          "027d4915b558e5e459e71af8822c6225ed43b5f8b47bb93ca4b11b5222cd9620");
}

TEST_CASE("SAA semantic alignment fails closed on advisory evidence") {
  using namespace statewright::saa;
  const auto dimension = LENGTH / TIME;
  const auto left = aligned_concept("ev:left", "left speed", "speed", "speed",
                                    dimension, "left");
  const auto right = aligned_concept("ev:right", "right speed", "speed",
                                     "speed", dimension, "right");
  const auto proposal = propose_semantic_alignment(
      left, right, "EXACT_EQUIVALENT", "speed", true, {"ev:model"}, {},
      true);
  const SemanticEvidenceResolver model_resolver =
      [](std::string_view) -> std::optional<SemanticGroundingEvidence> {
    return SemanticGroundingEvidence{.object_type = "egcf-evidence",
                                     .success = true,
                                     .simulated = false,
                                     .producer = "model-agent",
                                     .method = "model-claimed"};
  };
  const auto assessment =
      assess_semantic_alignment(model_resolver, left, right, proposal);
  REQUIRE(assessment.status == "SEMANTIC_ALIGNMENT_UNRESOLVED");
  REQUIRE_FALSE(assessment.canonical_alignment_eligible);
  REQUIRE_FALSE(assessment.blocking_reasons.empty());
}
