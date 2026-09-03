#include "statewright/common/error.hpp"
#include "statewright/core/operational_hypotheses.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::core::HypothesisProposal proposal(std::string proposition,
                                               int prior = 5'000) {
  return {.proposition = std::move(proposition),
          .model_prior_bp = prior,
          .assumptions = {" deterministic parser ", "deterministic parser"},
          .predictions = {"reproduction fails"},
          .falsifiers = {"clean parse succeeds"}};
}

statewright::core::EvidenceArtifact evidence(std::string artifact_id,
                                             char digest_character,
                                             int quality = 7'000) {
  return {.artifact_id = std::move(artifact_id),
          .kind = "test",
          .description = "grounded observation",
          .sha256 = std::string(64U, digest_character),
          .action_id = {},
          .source_snapshot_hash = "snapshot",
          .source_event_id = {},
          .path = {},
          .command_capability = {},
          .success = true,
          .requirement_ids = {"REQ-2", "REQ-1", "REQ-2"},
          .quality_bp = quality,
          .polarity = "support"};
}

} // namespace

TEST_CASE("operational hypothesis identity ignores whitespace and prior churn") {
  const auto first =
      statewright::core::make_operational_hypothesis(proposal(
          "Parser precedence is wrong", 3'000));
  const auto whitespace =
      statewright::core::make_operational_hypothesis(proposal(
          "Parser   precedence is   wrong", 3'000));
  const auto confidence =
      statewright::core::make_operational_hypothesis(proposal(
          "Parser precedence is wrong", 9'000));
  REQUIRE(first.hypothesis_id == whitespace.hypothesis_id);
  REQUIRE(first.hypothesis_id == confidence.hypothesis_id);
  REQUIRE(first.model_prior_bp != confidence.model_prior_bp);
  REQUIRE(first.verification_status == "UNVERIFIED_PROPOSITION");
}

TEST_CASE("operational hypotheses match frozen Python oracle fixtures") {
  const auto fixture = load_fixtures().at("operational_hypothesis_case");
  const auto first = statewright::core::make_operational_hypothesis(
      proposal("Parser   precedence is wrong", 3'000));
  REQUIRE(statewright::core::to_json(first) == fixture.at("hypothesis"));

  const auto initial = statewright::core::bounded_operational_hypothesis_set(
      std::nullopt,
      {proposal("Parser precedence is wrong", 3'000),
       proposal("Parser precedence is wrong", 9'000)},
      2);
  REQUIRE(statewright::core::to_json(initial.state) ==
          fixture.at("initial_state"));
  REQUIRE(initial.added_hypothesis_ids ==
          fixture.at("added_hypothesis_ids").get<std::vector<std::string>>());

  const auto artifact = evidence("e1", 'a');
  REQUIRE(statewright::core::evidence_fingerprint(artifact) ==
          fixture.at("evidence_fingerprint").get<std::string>());
  const statewright::core::EvidenceRegistry registry = {{"e1", artifact}};
  const auto linked = statewright::core::link_operational_hypothesis_evidence(
      initial.state, registry, initial.state.hypotheses.front().hypothesis_id,
      "e1", "supports");
  REQUIRE(linked.changed);
  REQUIRE(statewright::core::to_json(linked.state) ==
          fixture.at("supported_state"));
  REQUIRE(statewright::core::public_operational_hypothesis_projection(
              linked.state) == fixture.at("public_projection"));
}

TEST_CASE("operational hypothesis pools are bounded and preserve first prior") {
  const auto state = statewright::core::bounded_operational_hypothesis_set(
      std::nullopt, {proposal("A", 2'000), proposal("A", 8'000)}, 2);
  REQUIRE(state.state.hypotheses.size() == 1U);
  REQUIRE(state.added_hypothesis_ids.size() == 1U);
  REQUIRE(state.state.hypotheses.front().model_prior_bp == 2'000);

  REQUIRE_THROWS_AS(statewright::core::bounded_operational_hypothesis_set(
                        std::nullopt,
                        {proposal("A"), proposal("B"), proposal("C")}, 2),
                    statewright::common::Error);
}

TEST_CASE("grounded evidence updates bookkeeping without promoting truth") {
  const auto initial = statewright::core::bounded_operational_hypothesis_set(
      std::nullopt, {proposal("A")}, 2);
  const auto hypothesis_id = initial.state.hypotheses.front().hypothesis_id;
  const statewright::core::EvidenceRegistry registry = {
      {"e1", evidence("e1", 'b', 8'000)}};
  const auto updated = statewright::core::link_operational_hypothesis_evidence(
      initial.state, registry, hypothesis_id, "e1", "falsifies");
  REQUIRE(updated.changed);
  REQUIRE(updated.state.hypotheses.front().status ==
          "FALSIFIED_BY_LINKED_EVIDENCE");
  REQUIRE(updated.state.hypotheses.front().verification_status ==
          "UNVERIFIED_PROPOSITION");
}

TEST_CASE("grounded evidence content cannot be recycled or relabelled") {
  const auto initial = statewright::core::bounded_operational_hypothesis_set(
      std::nullopt, {proposal("A")}, 2);
  const auto hypothesis_id = initial.state.hypotheses.front().hypothesis_id;
  const statewright::core::EvidenceRegistry registry = {
      {"e1", evidence("e1", 'c')}, {"e2", evidence("e2", 'c')}};
  const auto first = statewright::core::link_operational_hypothesis_evidence(
      initial.state, registry, hypothesis_id, "e1", "supports");
  const auto duplicate =
      statewright::core::link_operational_hypothesis_evidence(
          first.state, registry, hypothesis_id, "e2", "supports");
  const auto relabelled =
      statewright::core::link_operational_hypothesis_evidence(
          first.state, registry, hypothesis_id, "e2", "falsifies");
  REQUIRE_FALSE(duplicate.changed);
  REQUIRE_FALSE(relabelled.changed);
  REQUIRE(duplicate.state.signature == first.state.signature);
  REQUIRE(relabelled.state.signature == first.state.signature);
}

TEST_CASE("unknown and zero-quality evidence fail closed") {
  const auto initial = statewright::core::bounded_operational_hypothesis_set(
      std::nullopt, {proposal("A")}, 2);
  const auto hypothesis_id = initial.state.hypotheses.front().hypothesis_id;
  REQUIRE_THROWS_AS(statewright::core::link_operational_hypothesis_evidence(
                        initial.state, {}, hypothesis_id, "missing", "supports"),
                    statewright::common::Error);
  const statewright::core::EvidenceRegistry registry = {
      {"e1", evidence("e1", 'd', 0)}};
  REQUIRE_THROWS_AS(statewright::core::link_operational_hypothesis_evidence(
                        initial.state, registry, hypothesis_id, "e1", "supports"),
                    statewright::common::Error);
}
