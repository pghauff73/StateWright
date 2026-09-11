#pragma once

#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <array>
#include <cstdint>
#include <numeric>

namespace statewright::egcf {

inline void automated_css_require(bool condition, std::string_view reason) {
  if (!condition)
    throw common::Error(common::ErrorCode::invalid_argument, std::string(reason));
}

// Independent reference arithmetic: small, bounded integers and Euclid's GCD,
// not the candidate translator, scalar interpreter, or GMP arithmetic.
struct CssReferenceFraction {
  std::int64_t numerator;
  std::int64_t denominator;
  [[nodiscard]] std::string text() const {
    const auto divisor = std::gcd(numerator, denominator);
    const auto n = numerator / divisor;
    const auto d = denominator / divisor;
    return std::to_string(n) + (d == 1 ? "" : "/" + std::to_string(d));
  }
};

// Closed experiment family. No downloaded code, caller-supplied fixtures,
// unbounded integers, network access, or caller-supplied passing scores.
[[nodiscard]] inline contracts::Json run_automated_css_experiment(
    EgcfStore &store, const InternetAlgorithmCandidate &candidate,
    bool design_only = false) {
  using contracts::Json;
  automated_css_require(candidate.proposed_saa_ir.dump().size() <= 4096,
                        "AUTOMATED_CSS_IR_BUDGET_EXCEEDED");
  const auto record = store.get(candidate.source_fragment_id);
  automated_css_require(record.object_type == "internet-source-fragment",
                        "AUTOMATED_CSS_SOURCE_FRAGMENT_REQUIRED");
  const auto fragment = sources::internet_source_fragment_from_json(record.payload);
  automated_css_require(fragment.metadata.value("section_completeness", std::string{}) == "COMPLETE" &&
      contracts::sha256_text(fragment.text) ==
          "cd24adf3f40876a83f6b179f3417317d0124e1dd475fe2abde7880b2785201ec",
      "AUTOMATED_CSS_SOURCE_REVISION_NOT_SUPPORTED");
  verify_internet_candidate_translation(candidate, fragment);
  const auto assessment = store.get(candidate.source_policy_assessment_id);
  automated_css_require(assessment.object_type == "internet-policy-assessment" &&
      sources::internet_policy_assessment_from_json(assessment.payload).admissible() &&
      assessment.payload.at("snapshot_id") == candidate.snapshot_id,
      "AUTOMATED_CSS_SOURCE_POLICY_DENIED");

  const auto unit = fragment.metadata.value("css_input_unit", std::string("in"));
  // Normative units per inch, rather than copying translator slopes:
  // 1in = 2.54cm = 25.4mm = 101.6Q = 6pc = 72pt = 96px.
  CssReferenceFraction units_per_inch{0, 1};
  if (unit == "in") units_per_inch = {1, 1};
  else if (unit == "cm") units_per_inch = {254, 100};
  else if (unit == "mm") units_per_inch = {254, 10};
  else if (unit == "Q") units_per_inch = {1016, 10};
  else if (unit == "pc") units_per_inch = {6, 1};
  else if (unit == "pt") units_per_inch = {72, 1};
  automated_css_require(units_per_inch.numerator > 0, "AUTOMATED_EXPERIMENT_FAMILY_UNSUPPORTED");
  const CssReferenceFraction reference_slope{
      96 * units_per_inch.denominator, units_per_inch.numerator};
  // A design request must not execute or assess the candidate before freezing.
  const auto program = design_only ? InternetExactScalarProgram{} :
      internet_exact_scalar_program(candidate.proposed_saa_ir);
  // The source verifier establishes the exact two-node graph. Equality of
  // normalized coefficients then proves equality for all rational inputs.
  const bool proof = !design_only && !program.polynomial && program.bounded_steps == 2 && program.bias == 0 &&
      program.slope.get_str() == reference_slope.text();
  // Independently expressed normative conversion table for the anchor group.
  // The fractional group instead derives results through units per inch.
  CssReferenceFraction anchor_scale{96, 1};
  if (unit == "cm") anchor_scale = {4800, 127};
  else if (unit == "mm") anchor_scale = {480, 127};
  else if (unit == "Q") anchor_scale = {120, 127};
  else if (unit == "pc") anchor_scale = {16, 1};
  else if (unit == "pt") anchor_scale = {4, 3};
  const std::array<std::array<CssReferenceFraction, 8>, 2> fixtures{{
      {{{-127,1}, {-12,1}, {-1,1}, {0,1}, {1,1}, {6,1}, {12,1}, {127,1}}},
      {{{-511,17}, {-23,11}, {-1,7}, {1,13}, {17,19}, {101,3}, {509,7}, {8191,31}}}
  }};
  Json groups = Json::array();
  Json observations = Json::array();
  bool all_correct = true;
  std::array<bool, 3> killed{false, false, false};
  for (std::size_t group_index = 0; group_index < fixtures.size(); ++group_index) {
    Json inputs = Json::array(), expected = Json::array(), outputs = Json::array();
    for (const auto input : fixtures[group_index]) {
      const auto input_text = input.text();
      const auto expected_text = group_index == 0 ? CssReferenceFraction{
          input.numerator * anchor_scale.numerator,
          input.denominator * anchor_scale.denominator}.text() : CssReferenceFraction{
          input.numerator * 96 * units_per_inch.denominator,
          input.denominator * units_per_inch.numerator}.text();
      inputs.push_back(input_text);
      expected.push_back(expected_text);
      if (design_only) continue;
      const mpq_class x(input_text);
      const mpq_class actual = program.slope * x + program.bias;
      all_correct = all_correct && actual.get_str() == expected_text;
      const std::array<mpq_class, 3> mutants{
          mpq_class((program.slope + 1) * x + program.bias),
          mpq_class(program.slope * x + program.bias + 1),
          mpq_class(-program.slope * x + program.bias)};
      for (std::size_t m = 0; m < mutants.size(); ++m)
        killed[m] = killed[m] || mutants[m].get_str() != expected_text;
      outputs.push_back(actual.get_str());
    }
    groups.push_back({{"independence_group", group_index == 0 ?
        "css-normative-integer-anchors" : "css-signed-fraction-cross-products"},
        {"deterministic_seed", 0}, {"inputs", inputs}, {"expected_outputs", expected}});
    observations.push_back(outputs);
  }
  if (design_only)
    return {{"kind", "AUTOMATED_CSS_DESIGN_V2"},
        {"candidate_id", candidate.object_id()},
        {"candidate_ir_sha256", contracts::sha256_json(candidate.proposed_saa_ir)},
        {"snapshot_id", candidate.snapshot_id},
        {"source_fragment_id", candidate.source_fragment_id},
        {"section_sha256", contracts::sha256_text(fragment.text)},
        {"reference_slope", reference_slope.text()}, {"reference_bias", "0"},
        {"trial_groups", groups},
        {"methods", {"normative rational conversion table", "units-per-inch cross-products"}},
        {"negative_controls", {"wrong scale", "wrong bias", "wrong sign"}},
        {"limits", {{"trials", 16}, {"steps_per_trial", 2}, {"ir_bytes", 4096}}},
        {"pass_rule", "exact coefficient proof AND all trials correct AND all three mutants detected"}};
  return {{"kind", "AUTOMATED_CSS_EXPERIMENT_V2"},
      {"candidate_id", candidate.object_id()},
      {"candidate_ir_sha256", contracts::sha256_json(candidate.proposed_saa_ir)},
      {"snapshot_id", candidate.snapshot_id}, {"source_fragment_id", candidate.source_fragment_id},
      {"section_sha256", contracts::sha256_text(fragment.text)},
      {"reference_derivation", "96 CSS px per inch divided by source-defined units per inch"},
      {"reference_units_per_inch", units_per_inch.text()},
      {"reference_slope", reference_slope.text()}, {"reference_bias", "0"},
      {"proof_method", "closed two-node affine graph and exact normalized coefficient equality"},
      {"experiment_method", "GMP scalar execution against normative table anchors and units-per-inch cross-products"},
      {"shared_dependencies", {"pinned W3C source", "host and C++ toolchain", "this compiled runner"}},
      {"independence_scope", "Different checking methods and disjoint inputs; not independent people, hosts, or organizations"},
      {"domain", "Exact rational numeric CSS absolute-unit conversion"},
      {"exclusions", {"device pixels", "physical screen dimensions", "CSS property range validation", "floating-point implementations"}},
      {"trial_groups", groups}, {"observed_outputs", observations},
      {"symbolic_proof_passed", proof}, {"all_trials_correct", all_correct},
      {"negative_controls", {{"wrong_scale_detected", killed[0]},
          {"wrong_bias_detected", killed[1]}, {"wrong_sign_detected", killed[2]}}},
      {"limits", {{"trials", 16}, {"steps_per_trial", 2}, {"ir_bytes", 4096}}},
      {"passed", proof && all_correct && killed[0] && killed[1] && killed[2]},
      {"operational_probation_uses", 0}, {"benchmark_track_scores", Json::object()}};
}

// Freeze intent, not outcomes. The full reviewed protocol binds the eventual
// measurement receipt, while this projection forbids post-result policy tuning.
inline contracts::Json automated_css_protocol_design(
    const InternetExperimentProtocol &protocol) {
  auto design = to_json(protocol);
  design.erase("protocol_signature");
  design.erase("benchmark_track_scores");
  design.erase("integrity_snapshots");
  auto &ground = design["source_provenance"]["grounded"];
  for (const auto *key : {"review_evidence_ids", "measurement_evidence_id",
                         "freeze_evidence_id", "experiment_evidence_id"})
    ground.erase(key);
  return design;
}

inline contracts::Json validate_automated_css_freeze(EgcfStore &store,
    const InternetAlgorithmCandidate &candidate, std::string_view freeze_id) {
  const auto record = store.get(freeze_id);
  automated_css_require(record.object_type == "egcf-evidence" &&
      !record.payload.at("simulated").get<bool>() &&
      record.payload.at("success").get<bool>() &&
      record.payload.at("subject_id") == candidate.object_id(),
      "AUTOMATED_CSS_FREEZE_RECEIPT_REQUIRED");
  const auto &content = record.payload.at("content");
  automated_css_require(content.at("kind") == "AUTOMATED_CSS_FREEZE_V2" &&
      content.at("design") == run_automated_css_experiment(store, candidate, true),
      "AUTOMATED_CSS_FROZEN_DESIGN_MISMATCH");
  return content;
}

// Re-execution is the authority, not a signature, producer label, or success
// field supplied in an evidence object. Called at qualification AND promotion.
inline void validate_automated_css_review(EgcfStore &store,
    const InternetExperimentProtocol &protocol, std::string_view binding,
    const contracts::Json &trust) {
  automated_css_require(trust.value("allow_automated_css_review", true),
                        "AUTOMATED_CSS_REVIEW_DISABLED_BY_POLICY");
  const auto &ground = protocol.source_provenance.at("grounded");
  automated_css_require(ground.at("adoption_mode") == "NEW_CAPABILITY",
                        "AUTOMATED_CSS_REPLACEMENT_BASELINE_NOT_SUPPORTED");
  const auto candidate_record = store.get(ground.at("candidate_id").get<std::string>());
  automated_css_require(candidate_record.object_type == "internet-algorithm-candidate",
                        "AUTOMATED_CSS_CANDIDATE_REQUIRED");
  const auto candidate = internet_algorithm_candidate_from_json(candidate_record.payload);
  const auto freeze_id = ground.at("freeze_evidence_id").get<std::string>();
  const auto frozen = validate_automated_css_freeze(store, candidate, freeze_id);
  automated_css_require(frozen.at("protocol_design") == automated_css_protocol_design(protocol),
                        "AUTOMATED_CSS_PROTOCOL_CHANGED_AFTER_FREEZE");
  const auto replay = run_automated_css_experiment(store, candidate);
  automated_css_require(replay.at("passed").get<bool>(), "AUTOMATED_CSS_EXPERIMENT_FAILED");
  const auto run = store.get(ground.at("experiment_evidence_id").get<std::string>());
  automated_css_require(run.object_type == "egcf-evidence" &&
      !run.payload.at("simulated").get<bool>() && run.payload.at("success").get<bool>() &&
      run.payload.at("subject_id") == candidate.object_id() &&
      run.payload.at("content").at("kind") == "AUTOMATED_CSS_RUN_V2" &&
      run.payload.at("content").at("freeze_evidence_id") == freeze_id &&
      run.payload.at("content").at("experiment") == replay,
      "AUTOMATED_CSS_RUN_RECEIPT_REPLAY_MISMATCH");
  automated_css_require(protocol.trial_groups.size() == 2,
                        "AUTOMATED_CSS_FROZEN_GROUPS_MISMATCH");
  for (const auto &required : replay.at("trial_groups")) {
    bool found = false;
    for (const auto &actual : protocol.trial_groups)
      if (actual.at("independence_group") == required.at("independence_group")) {
        found = actual.at("inputs") == required.at("inputs") &&
            actual.at("expected_outputs") == required.at("expected_outputs") &&
            actual.at("deterministic_seed") == required.at("deterministic_seed");
      }
    automated_css_require(found, "AUTOMATED_CSS_FROZEN_GROUPS_MISMATCH");
  }
  const auto ids = ground.at("review_evidence_ids").get<std::vector<std::string>>();
  automated_css_require(ids.size() == 1, "AUTOMATED_CSS_REPLAY_RECEIPT_REQUIRED");
  const auto evidence = store.get(ids.front());
  automated_css_require(evidence.object_type == "egcf-evidence" &&
      !evidence.payload.at("simulated").get<bool>() &&
      evidence.payload.at("success").get<bool>() &&
      evidence.payload.at("content").at("protocol_binding_sha256") == binding &&
      evidence.payload.at("content").at("experiment") == replay,
      "AUTOMATED_CSS_REVIEW_REPLAY_MISMATCH");
}
} // namespace statewright::egcf
