#include "statewright/saa/semantic_revision.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::SemanticEvidenceResolver revision_evidence() {
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

statewright::saa::SemanticConcept pressure_concept() {
  using namespace statewright::saa;
  return make_semantic_concept(
      "pressure command", "absolute pressure command", "fluid control",
      "pressure", {}, MASS / LENGTH / (TIME ^ 2), PASCAL, {"ev:old"});
}

} // namespace

TEST_CASE("SAA semantic contradictions create revision hypotheses") {
  using namespace statewright::saa;
  const auto source = pressure_concept();
  REQUIRE(source.concept_signature ==
          "8e07d17a168798636f8804a3d7a263baaf340ccecc5cc6e2d9196c153b69887d");
  const auto contradiction = detect_semantic_contradiction(
      source,
      "Experiments show the coordinate is pressure deviation from ambient",
      {"ev:new"}, "pressure deviation from ambient");
  REQUIRE(contradiction.status == "SEMANTIC_CONTRADICTION_OPEN");
  REQUIRE(contradiction.contradiction_kind == "MEANING_CONTRADICTION");
  REQUIRE(contradiction.contradiction_signature ==
          "0a12393c6630753c7fc88c437c4e3785bb15e1a9816a08e0891c7b774f7d80dd");
  const auto directives = propagate_semantic_contradiction(contradiction);
  REQUIRE(directives.size() == 7U);
  REQUIRE(directives.back().subsystem == "ALGORITHM_STORE");
  REQUIRE(directives.back().blocking);
}

TEST_CASE("SAA semantic revisions require grounded review") {
  using namespace statewright::saa;
  const auto source = pressure_concept();
  const auto contradiction = detect_semantic_contradiction(
      source,
      "Experiments show the coordinate is pressure deviation from ambient",
      {"ev:new"}, "pressure deviation from ambient");
  const std::string falsifier =
      "coordinate remains unchanged when ambient pressure shifts";
  const auto proposal = propose_semantic_revision(
      source, {contradiction}, "pressure deviation from ambient",
      std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
      std::nullopt, {}, {falsifier});
  REQUIRE(proposal.proposal_signature ==
          "d237ed73c3e20b5c015e15b9049544a9f99bd374493268a4402c9d7a26300fed");

  const auto blocked = requalify_semantic_revision(
      revision_evidence(), source, proposal, {"ev:new"},
      {SemanticRevisionFalsifierResult(falsifier, "SURVIVED", "ev:new")},
      false);
  REQUIRE(blocked.status == "SEMANTIC_REQUALIFICATION_BLOCKED");
  REQUIRE_FALSE(blocked.canonical_replacement_eligible);
  REQUIRE(blocked.requalification_signature ==
          "23e52c8d223c648e2214a31c792edac39e45c556de69b2c616bb4e318df5cf45");

  const auto qualified = requalify_semantic_revision(
      revision_evidence(), source, proposal, {"ev:new"},
      {SemanticRevisionFalsifierResult(falsifier, "SURVIVED", "ev:new")},
      true);
  REQUIRE(qualified.status == "SEMANTIC_REQUALIFIED");
  REQUIRE(qualified.canonical_replacement_eligible);
  REQUIRE(qualified.replacement_concept.has_value());
  REQUIRE(qualified.replacement_concept->concept_signature ==
          "0a93d168c745dcb7d1ab64dd596324a1e003c2bdc704207e2d30032a0fa60974");
  REQUIRE(qualified.requalification_signature ==
          "a19841e0d17f0acb6cb5d601e776d02b2c84bfb2a4081012cbb812450a05797f");
}
