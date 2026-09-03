#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/canonical_algorithm_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <map>

namespace {

using Json = statewright::contracts::Json;

std::filesystem::path canonical_store_temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-canonical-store-" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

Json canonical_store_mapping(std::size_t inputs, std::size_t outputs,
                             bool extra_node = false) {
  Json nodes = Json::array();
  Json input_specs = Json::array();
  Json output_specs = Json::array();
  Json entry_nodes = Json::array();
  for (std::size_t input = 0; input < inputs; ++input) {
    input_specs.push_back({{"data_type", "scalar"},
                           {"name", "u" + std::to_string(input)},
                           {"position", input}});
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    const std::string node = "out" + std::to_string(output);
    nodes.push_back({{"id", node},
                     {"operands", {{{"constant", 0}}}},
                     {"primitive", "CONST"}});
    output_specs.push_back({{"data_type", "scalar"},
                            {"name", "y" + std::to_string(output)},
                            {"position", output},
                            {"source", {{"node", node}}}});
    entry_nodes.push_back(node);
  }
  if (extra_node) {
    nodes.push_back({{"id", "extra"},
                     {"operands", {{{"constant", 1}}}},
                     {"primitive", "CONST"}});
    entry_nodes.push_back("extra");
  }
  return {{"entry_nodes", entry_nodes},
          {"inputs", input_specs},
          {"name", "canonical-store-fixture"},
          {"nodes", nodes},
          {"outputs", output_specs}};
}

statewright::saa::NormalizationContract canonical_store_normalization(
    std::size_t inputs, std::size_t outputs, bool extra_node = false) {
  statewright::saa::BoundMap input_bounds;
  statewright::saa::BoundMap output_bounds;
  for (std::size_t input = 0; input < inputs; ++input) {
    input_bounds.emplace(static_cast<int>(input),
                         statewright::saa::NumericBound(0.0, 1.0,
                                                       "EXACT_BOUND"));
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    output_bounds.emplace(static_cast<int>(output),
                          statewright::saa::NumericBound(0.0, 1.0,
                                                        "EXACT_BOUND"));
  }
  return statewright::saa::build_normalization_contract(
      statewright::saa::structure_from_mapping(
          canonical_store_mapping(inputs, outputs, extra_node)),
      input_bounds, {}, {}, output_bounds,
      statewright::saa::TimeNormalization(1.0));
}

statewright::saa::LinearTransferFunction store_constant(std::string value) {
  return {"CONTINUOUS", {std::move(value)}, {1}};
}

statewright::egcf::EgcfRecord canonical_semantic_evidence(
    const statewright::saa::SemanticRepresentationIssue &issue,
    const statewright::saa::SemanticCandidateMeaning &candidate,
    std::string suffix) {
  const Json content =
      {{"candidate_id", candidate.candidate_id},
       {"excluded_output_indices", candidate.excluded_output_indices},
       {"expected_output_indices", candidate.expected_output_indices},
       {"issue_id", issue.issue_id},
       {"meaning", candidate.meaning},
       {"review", suffix}};
  return {.object_type = "egcf-evidence",
          .payload =
              {{"algorithm_id", "saa-canonical-store-test@1"},
               {"category", "semantic-grounding"},
               {"claim_ids", Json::array({issue.issue_id})},
               {"command_id", "algorithm.qualify@1"},
               {"content", content},
               {"created_at", "2026-09-02T00:00:00Z"},
               {"environment", {{"suite", "saa-6"}}},
               {"independence_group", "semantic-review:" + suffix},
               {"limitations", Json::array()},
               {"method", "independent-semantic-review"},
               {"oracle", "meaning-output-footprint-review"},
               {"path", ""},
               {"producer", "human-canonical-store-test"},
               {"requirement_ids", Json::array()},
               {"sha256", statewright::contracts::sha256_json(content)},
               {"simulated", false},
               {"source_snapshot_hash",
                statewright::contracts::sha256_json({{"source", content}})},
               {"subject_id", candidate.candidate_id},
               {"success", true},
               {"target", issue.coordinate_label}}};
}

struct StoreFormFixture final {
  statewright::saa::CanonicalRepresentativeAlgorithmForm form;
  std::vector<statewright::saa::SemanticRepresentationIssue> issues;
  std::vector<statewright::saa::SemanticCandidateMeaning> candidates;
  std::vector<statewright::saa::SemanticResolution> resolutions;
};

StoreFormFixture build_store_form(
    statewright::egcf::EgcfStore &store,
    statewright::saa::TransferFunctionMatrix rows,
    std::map<int, std::string> meanings, bool structural_extra = false,
    bool grounded = true, std::string suffix = "a") {
  const std::size_t outputs = rows.size();
  const std::size_t inputs = rows.front().size();
  auto normalization =
      canonical_store_normalization(inputs, outputs, structural_extra);
  auto mimo = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS", std::move(rows)}, normalization);
  auto search = statewright::saa::discover_representative_inputs(mimo);
  auto issues =
      statewright::saa::assess_representative_candidate_semantics(mimo, search);
  std::vector<statewright::saa::SemanticCandidateMeaning> candidates;
  std::vector<statewright::saa::SemanticResolution> resolutions;
  for (const auto &issue : issues) {
    std::vector<int> expected;
    std::vector<int> excluded;
    for (std::size_t output = 0; output < outputs; ++output) {
      const bool affected = std::find(issue.affected_output_indices.begin(),
                                      issue.affected_output_indices.end(),
                                      output) !=
                            issue.affected_output_indices.end();
      (affected ? expected : excluded).push_back(static_cast<int>(output));
    }
    const int output_key = expected.empty() ? -1 : expected.front();
    const std::string meaning = meanings.at(output_key);
    const std::string falsifier =
        "coordinate " + std::to_string(issue.coordinate_index) +
        " changes an excluded output";
    candidates.push_back(statewright::saa::make_semantic_candidate(
        issue, meaning, expected, excluded, {}, {falsifier}));
    std::vector<std::string> evidence_ids;
    if (grounded) {
      evidence_ids.push_back(store.register_record(canonical_semantic_evidence(
          issue, candidates.back(),
          suffix + "-" + std::to_string(issue.coordinate_index))));
    } else {
      evidence_ids.push_back("missing:evidence:" +
                             std::to_string(issue.coordinate_index));
    }
    resolutions.push_back(statewright::saa::evaluate_semantic_candidate(
        issue, candidates.back(), evidence_ids,
        {{falsifier, "SURVIVED",
          evidence_ids.empty()
              ? std::optional<std::string>{}
              : std::optional<std::string>{evidence_ids.front()}}},
        true));
  }
  auto form = statewright::saa::canonicalize_representative_algorithm(
      statewright::saa::canonicalize_mapping(
          canonical_store_mapping(inputs, outputs, structural_extra)),
      normalization, mimo, search, issues, candidates, resolutions);
  return {.form = std::move(form),
          .issues = std::move(issues),
          .candidates = std::move(candidates),
          .resolutions = std::move(resolutions)};
}

} // namespace

TEST_CASE("SAA canonical algorithm store persists and rebuilds provenance") {
  using namespace statewright;
  const auto root = canonical_store_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::CanonicalAlgorithmStore store(egcf_store);
  const auto fixture = build_store_form(
      egcf_store,
      {{store_constant("2"), store_constant("0")},
       {store_constant("0"), store_constant("3")}},
      {{0, "temperature deviation"}, {1, "pressure deviation"}});
  const auto admitted = store.admit(fixture.form, fixture.issues,
                                    fixture.candidates, fixture.resolutions);
  REQUIRE(admitted.status == "ADMITTED_NEW_CANONICAL");
  REQUIRE(admitted.store_generation == 1);
  REQUIRE(admitted.canonical_id ==
          "canonical-algorithm:sha256:" +
              fixture.form.representative_behavior_signature);
  REQUIRE(store.list().size() == 1U);
  REQUIRE(store.sources(admitted.canonical_id).size() == 1U);
  REQUIRE(store.get(admitted.canonical_id)
              .at("payload")
              .at("representative_behavior_signature") ==
          fixture.form.representative_behavior_signature);
  REQUIRE_FALSE(
      store.get(admitted.canonical_id).at("payload").contains(
          "source_structural_hash"));
  REQUIRE(store.relations(admitted.canonical_id).front().relation_type ==
          "DERIVED_FROM");

  std::filesystem::remove(egcf_store.projection_path());
  egcf_store.rebuild_projection();
  store.rebuild_projection();
  REQUIRE(store.lookup(fixture.form).status ==
          "REPRESENTATIVE_EQUIVALENT_ALREADY_STORED");
  REQUIRE(store.current_generation() == 1);
  std::filesystem::remove_all(root);
}

TEST_CASE("SAA canonical algorithm store separates meaning and mathematics") {
  using namespace statewright;
  const auto root = canonical_store_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::CanonicalAlgorithmStore store(egcf_store);
  const auto first = build_store_form(
      egcf_store,
      {{store_constant("2"), store_constant("0")},
       {store_constant("0"), store_constant("3")}},
      {{0, "temperature deviation"}, {1, "pressure deviation"}}, false,
      true, "first");
  const auto first_admission = store.admit(
      first.form, first.issues, first.candidates, first.resolutions);
  const auto semantic_variant = build_store_form(
      egcf_store,
      {{store_constant("2"), store_constant("0")},
       {store_constant("0"), store_constant("3")}},
      {{0, "thermal demand"}, {1, "pressure deviation"}}, false, true,
      "semantic");
  const auto semantic_lookup = store.lookup(semantic_variant.form);
  REQUIRE(semantic_lookup.status ==
          "MATHEMATICAL_MATCH_SEMANTIC_DIFFERENCE");
  REQUIRE(semantic_lookup.mathematical_match_ids ==
          std::vector<std::string>{first_admission.canonical_id});
  const auto second_admission =
      store.admit(semantic_variant.form, semantic_variant.issues,
                  semantic_variant.candidates, semantic_variant.resolutions);
  REQUIRE(second_admission.store_generation == 2);
  REQUIRE(store.relations(second_admission.canonical_id,
                          std::string("NEAR_VARIANT_OF"))
              .size() == 1U);

  const auto structural_variant = build_store_form(
      egcf_store,
      {{store_constant("2"), store_constant("0")},
       {store_constant("0"), store_constant("3")}},
      {{0, "temperature deviation"}, {1, "pressure deviation"}}, true,
      true, "structural");
  const auto reused = store.admit(structural_variant.form,
                                  structural_variant.issues,
                                  structural_variant.candidates,
                                  structural_variant.resolutions);
  REQUIRE(reused.status == "REUSED_EQUIVALENT_CANONICAL");
  REQUIRE(reused.canonical_id == first_admission.canonical_id);
  REQUIRE(store.sources(first_admission.canonical_id).size() == 2U);
  REQUIRE(store.list().size() == 2U);
  std::filesystem::remove_all(root);
}

TEST_CASE("SAA canonical algorithm store rejects ungrounded semantics") {
  using namespace statewright;
  const auto root = canonical_store_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::CanonicalAlgorithmStore store(egcf_store);
  const auto fixture = build_store_form(
      egcf_store,
      {{store_constant("2"), store_constant("0")},
       {store_constant("0"), store_constant("3")}},
      {{0, "temperature deviation"}, {1, "pressure deviation"}}, false,
      false);
  REQUIRE_THROWS_AS(store.admit(fixture.form, fixture.issues,
                                fixture.candidates, fixture.resolutions),
                    common::Error);
  std::filesystem::remove_all(root);
}

TEST_CASE("SAA canonical algorithm search explains candidates and exclusions") {
  using namespace statewright;
  const auto root = canonical_store_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::CanonicalAlgorithmStore store(egcf_store);
  const auto first = build_store_form(
      egcf_store,
      {{store_constant("2"), store_constant("0")},
       {store_constant("0"), store_constant("3")}},
      {{0, "temperature deviation"}, {1, "pressure deviation"}}, false,
      true, "search-first");
  const auto first_admission = store.admit(
      first.form, first.issues, first.candidates, first.resolutions);
  const auto second = build_store_form(
      egcf_store,
      {{store_constant("4"), store_constant("0")},
       {store_constant("0"), store_constant("5")}},
      {{0, "thermal demand"}, {1, "pressure deviation"}}, false, true,
      "search-second");
  static_cast<void>(store.admit(second.form, second.issues,
                                second.candidates, second.resolutions));
  egcf::CanonicalAlgorithmQuery query;
  query.mathematical_signature =
      first.form.mathematical_representative_signature;
  query.domain = "CONTINUOUS";
  query.output_count = 2;
  query.semantic_meanings = {"temperature deviation"};
  query.lexical_terms = {"pressure"};
  query.limit = 10;
  const auto result = store.search(query);
  REQUIRE(result.status == "CANONICAL_ALGORITHM_SELECTED");
  REQUIRE(result.selected_canonical_id == first_admission.canonical_id);
  REQUIRE(result.candidates.size() == 1U);
  REQUIRE(result.excluded.size() == 1U);
  REQUIRE(result.excluded.front().at("reasons").get<std::vector<std::string>>() ==
          std::vector<std::string>{"mathematical_signature_mismatch",
                                   "missing_semantic_meanings"});
  REQUIRE(store.search_text("temperature") ==
          std::vector<std::string>{first_admission.canonical_id});
  const auto before = to_json(result);
  store.rebuild_projection();
  query.domain = "continuous";
  REQUIRE(to_json(store.search(query)) == before);
  std::filesystem::remove_all(root);
}
