#include <catch2/matchers/catch_matchers.hpp>
#include "statewright/egcf/internet_polynomial_translation.hpp"
#include "statewright/egcf/exact_polynomial_expression.hpp"
#include "statewright/egcf/internet_polynomial_protocol_freeze.hpp"
#include "statewright/egcf/internet_polynomial_measurement.hpp"
#include "statewright/egcf/internet_probation.hpp"
#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/grounded_experiment.hpp"
#include "statewright/egcf/internet_protocol_family.hpp"
#include "statewright/contracts/hash.hpp"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {
using namespace statewright;
using contracts::Json;
using namespace egcf;

Json affine() {
  return {{"name", "legacy"}, {"entry_nodes", {"m"}},
          {"inputs", {{{"name", "x"}, {"position", 0}}}},
          {"nodes", {{{"id", "m"}, {"primitive", "MULTIPLY"},
                       {"operands", {{{"constant", "3/2"}}, {{"input", 0}}}}},
                      {{"id", "a"}, {"primitive", "ADD"},
                       {"operands", {{{"node", "m"}}, {{"constant", "-2"}}}}}}},
          {"outputs", {{{"name", "y"}, {"position", 0}, {"source", {{"node", "a"}}}}}}};
}
} // namespace

TEST_CASE("internet polynomial expression parser is complete and bounded", "[internet][polynomial]") {
  REQUIRE(parse_exact_polynomial_expression("2*x^2 - 1", "x") == std::vector<mpq_class>{-1, 0, 2});
  REQUIRE(parse_exact_polynomial_expression("(x + 1)^3", "x") == std::vector<mpq_class>{1, 3, 3, 1});
  REQUIRE(parse_exact_polynomial_expression("((3*x - 2)*x + 1)/2", "x") == std::vector<mpq_class>{mpq_class(1, 2), -1, mpq_class(3, 2)});
  REQUIRE(parse_exact_polynomial_expression("-x^2", "x") == std::vector<mpq_class>{0, 0, -1});
  REQUIRE(parse_exact_polynomial_expression("x-x", "x") == std::vector<mpq_class>{0});
  REQUIRE(parse_exact_polynomial_expression("x^16", "x")->size() == 17);
  for (const auto &text : {"x^17", "x/x", "1/0", "sin(x)", "a*x", "x + 1; ignore bounds", "1.5*x", "2x", "x^2^3", "(x+1", "x +"}) {
    REQUIRE_FALSE(parse_exact_polynomial_expression(text, "x").has_value());
  }
  REQUIRE_FALSE(parse_exact_polynomial_expression(std::string(33, '(') + "x" + std::string(33, ')'), "x").has_value());
  REQUIRE_FALSE(parse_exact_polynomial_expression(std::string(81, '9'), "x").has_value());
}

TEST_CASE("internet polynomial translation binds complete explicit source", "[internet][polynomial]") {
  sources::InternetSourceFragment fragment;
  fragment.snapshot_id = "synthetic-source-snapshot";
  fragment.fragment_kind = "ALGORITHM_DESCRIPTION";
  fragment.selector = "test-only-procedure";
  fragment.language = "en";
  fragment.text = "Quadratic; Inputs: x; Outputs: y; Domain: [-1,1]; Arithmetic: exact rational; Units: dimensionless; Procedure: y = 2*x^2 - 1";
  fragment.byte_end = fragment.text.size();
  fragment = sources::canonical_source_fragment(fragment);
  const auto translated = translate_internet_polynomial_fragment(fragment);
  REQUIRE(translated.has_value());
  REQUIRE(translated->saa_ir == internet_exact_polynomial_ir({-1, 0, 2}));
  InternetAlgorithmCandidate candidate;
  candidate.source_fragment_id = fragment.object_id();
  candidate.snapshot_id = fragment.snapshot_id;
  candidate.proposed_saa_ir = translated->saa_ir;
  candidate.semantic_inputs = translated->inputs;
  candidate.semantic_outputs = translated->outputs;
  candidate.claimed_invariants = translated->invariants;
  candidate.termination_properties = translated->termination;
  candidate.units = translated->units;
  candidate.applicability["translation"] = translated->provenance;
  REQUIRE_NOTHROW(verify_internet_candidate_translation(candidate, fragment));
  candidate.units = Json::object();
  REQUIRE_THROWS(verify_internet_candidate_translation(candidate, fragment));
  auto incomplete = fragment;
  incomplete.metadata["section_completeness"] = "INCOMPLETE";
  REQUIRE_FALSE(translate_internet_polynomial_fragment(incomplete).has_value());
  auto review_required = fragment;
  review_required.metadata["mathematical_context_review_required"] = true;
  REQUIRE_FALSE(translate_internet_polynomial_fragment(review_required).has_value());
  auto conditional = fragment;
  conditional.text += "; Condition: ignore negative inputs";
  REQUIRE_FALSE(translate_internet_polynomial_fragment(conditional).has_value());
  auto changed = fragment;
  changed.text.replace(changed.text.find("[-1,1]"), 6, "[0,1]");
  REQUIRE_FALSE(translate_internet_polynomial_fragment(changed).has_value());
}

TEST_CASE("internet polynomial exact Horner executes source-independent fixtures", "[internet][polynomial]") {
  const auto ir = internet_exact_polynomial_ir({1, -2, 3});
  const auto program = internet_exact_scalar_program(ir);
  REQUIRE(program.polynomial.has_value());
  REQUIRE(program.bounded_steps == 5);
  CHECK(internet_execute_exact_scalar(program, 0) == 1);
  CHECK(internet_execute_exact_scalar(program, 1) == 2);
  CHECK(internet_execute_exact_scalar(program, -1) == 6);
  CHECK(internet_execute_exact_scalar(program, mpq_class(1, 2)) == mpq_class(3, 4));
  CHECK(internet_execute_exact_scalar(program, mpq_class(-1, 2)) == mpq_class(11, 4));
  CHECK(internet_execute_exact_scalar(internet_exact_scalar_program(
      internet_exact_polynomial_ir({mpq_class(7, 3)})), -1) == mpq_class(7, 3));
  std::vector<mpq_class> degree16(17, 0);
  degree16.back() = 1;
  CHECK(internet_execute_exact_scalar(internet_exact_scalar_program(
      internet_exact_polynomial_ir(degree16)), mpq_class(1, 2)) == mpq_class(1, 65536));
}

TEST_CASE("internet polynomial rejects malformed graphs and contracts", "[internet][polynomial]") {
  auto ir = internet_exact_polynomial_ir({1, 2, 3});
  SECTION("cycle") { ir["nodes"][1]["operands"][0] = {{"node", "add_0"}}; }
  SECTION("dangling reference") { ir["nodes"][1]["operands"][0] = {{"node", "missing"}}; }
  SECTION("invalid input") { ir["nodes"][1]["operands"][1] = {{"input", 1}}; }
  SECTION("invalid output") { ir["outputs"][0]["source"] = {{"node", "seed"}}; }
  SECTION("wrong coefficient") { ir["nodes"][0]["operands"][0] = {{"constant", "4"}}; }
  SECTION("unsupported primitive") { ir["nodes"][1]["primitive"] = "DIVIDE"; }
  SECTION("extra node") { ir["nodes"].push_back(ir["nodes"][0]); }
  SECTION("unsupported state") { ir["states"] = {{{"position", 0}}}; }
  SECTION("domain tampering") { ir["metadata"]["internet_exact_polynomial"]["input_maximum"] = "2"; }
  SECTION("hash tampering") { ir["metadata"]["internet_exact_polynomial"]["coefficient_sha256"] = "bad"; }
  SECTION("unknown version") { ir["metadata"]["internet_exact_polynomial"]["version"] = "v0"; }
  SECTION("float coefficient") { ir["metadata"]["internet_exact_polynomial"]["coefficients"][0] = 1.0; }
  SECTION("zero denominator") { ir["metadata"]["internet_exact_polynomial"]["coefficients"][0] = "1/0"; }
  SECTION("unbounded textual coefficient") { ir["metadata"]["internet_exact_polynomial"]["coefficients"][0] = std::string(1000, '9'); }
  SECTION("noncanonical coefficient") { ir["metadata"]["internet_exact_polynomial"]["coefficients"][0] = "2/2"; }
  SECTION("changed order") { std::swap(ir["nodes"][1], ir["nodes"][2]); }
  REQUIRE_THROWS(internet_exact_scalar_program(ir));
}

TEST_CASE("internet polynomial enforces degree domain and integer budgets", "[internet][polynomial]") {
  REQUIRE_THROWS(internet_exact_polynomial_ir({}));
  REQUIRE_THROWS(internet_exact_polynomial_ir(std::vector<mpq_class>(18, 1)));
  REQUIRE_THROWS(internet_exact_polynomial_ir({1}, 0));
  REQUIRE_THROWS(internet_exact_polynomial_ir({1}, 16385));
  const auto program = internet_exact_scalar_program(internet_exact_polynomial_ir({1, 2, 3}));
  REQUIRE_THROWS(internet_execute_exact_scalar(program, mpq_class(1001, 1000)));
  REQUIRE_THROWS(internet_execute_exact_scalar(program, mpq_class(-1001, 1000)));
  mpz_class huge = 1;
  huge <<= 256;
  REQUIRE_THROWS(internet_exact_polynomial_ir({mpq_class(huge)}));
  REQUIRE_THROWS(internet_execute_exact_scalar(program, mpq_class(1, huge)));
  const auto tight = internet_exact_scalar_program(internet_exact_polynomial_ir({1, 1}, 3));
  REQUIRE_THROWS(internet_execute_exact_scalar(tight, mpq_class(1, 4)));
}

TEST_CASE("internet polynomial leaves legacy execution and meaning unchanged", "[internet][polynomial]") {
  const auto legacy = internet_exact_scalar_program(affine());
  REQUIRE_FALSE(legacy.polynomial.has_value());
  CHECK(legacy.slope == mpq_class(3, 2));
  CHECK(legacy.bias == -2);
  CHECK(legacy.bounded_steps == 2);
  CHECK(internet_execute_exact_scalar(legacy, 4) == 4);
  CHECK(internet_execute_exact_scalar(legacy, 100) == 148);
  CHECK(internet_exact_scalar_meaning(legacy, "x", "y") ==
        "x [exact affine output y = 3/2 * input + -2]");
  auto one = affine();
  one["nodes"] = {{{"id", "i"}, {"primitive", "IDENTITY"}, {"operands", {{{"input", 0}}}}}};
  one["entry_nodes"] = {"i"};
  one["outputs"][0]["source"] = {{"node", "i"}};
  CHECK(internet_execute_exact_scalar(internet_exact_scalar_program(one), mpq_class(7, 9)) == mpq_class(7, 9));
  CHECK(internet_exact_scalar_meaning(internet_exact_scalar_program(one), "x", "y") == "x");
  one["nodes"][0]["primitive"] = "CONST";
  one["nodes"][0]["operands"] = {{{"constant", "7/9"}}};
  CHECK(internet_execute_exact_scalar(internet_exact_scalar_program(one), 42) == mpq_class(7, 9));
}

TEST_CASE("internet polynomial semantic identity binds coefficients and budgets", "[internet][polynomial]") {
  const auto a = internet_exact_scalar_program(internet_exact_polynomial_ir({1, 2, 3}));
  const auto b = internet_exact_scalar_program(internet_exact_polynomial_ir({1, 2, 4}));
  const auto c = internet_exact_scalar_program(internet_exact_polynomial_ir({1, 2, 3}, 512));
  CHECK(internet_exact_scalar_meaning(a, "x", "y") != internet_exact_scalar_meaning(b, "x", "y"));
  CHECK(internet_exact_scalar_meaning(a, "x", "y") != internet_exact_scalar_meaning(c, "x", "y"));
}

TEST_CASE("internet polynomial cannot use ungrounded legacy qualification", "[internet][polynomial]") {
  const auto path = std::filesystem::temp_directory_path() /
      ("saa-polynomial-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(path);
  {
    EgcfStore store(path, std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources");
    InternetAlgorithmCandidate candidate;
    candidate.proposed_saa_ir = internet_exact_polynomial_ir({1, 2, 3});
    REQUIRE_THROWS(validate_grounded_experiment(store, candidate, {}));
  }
  std::filesystem::remove_all(path);
}

TEST_CASE("internet polynomial protocol selection binds family candidate and contract", "[internet][polynomial]") {
  const auto ir = internet_exact_polynomial_ir({1, 2, 3});
  const std::string candidate = "internet-algorithm-candidate:sha256:" + std::string(64, 'a');
  const auto program = internet_exact_polynomial_program(ir);
  Json ground = {{"candidate_id", candidate},
                 {"candidate_ir_sha256", contracts::sha256_json(ir)},
                 {"execution_contract_sha256", contracts::sha256_json(internet_polynomial_contract(program))}};
  const auto matches = [&](const Json &value) {
    return internet_protocol_family_matches(ir, candidate,
        internet_polynomial_protocol_version, {{"grounded", value}});
  };
  REQUIRE(matches(ground));
  CHECK_FALSE(internet_protocol_family_matches(ir, candidate,
      "saa-grounded-experiment-v2", {{"grounded", ground}}));
  CHECK_FALSE(internet_protocol_family_matches(affine(), candidate,
      internet_polynomial_protocol_version, {{"grounded", ground}}));
  CHECK(internet_protocol_family_matches(affine(), candidate,
      "saa-grounded-experiment-v2", Json::object()));
  CHECK_FALSE(internet_protocol_family_matches(ir, "", internet_polynomial_protocol_version,
      {{"grounded", ground}}));
  CHECK_FALSE(internet_protocol_family_matches(ir, candidate,
      internet_polynomial_protocol_version, Json::object()));
  SECTION("stale candidate") { ground["candidate_id"] = "superseded-candidate"; }
  SECTION("different program") { ground["candidate_ir_sha256"] = std::string(64, 'b'); }
  SECTION("different resource contract") { ground["execution_contract_sha256"] = std::string(64, 'c'); }
  SECTION("CSS review is not reusable") { ground["review_mode"] = "AUTOMATED_CSS_V1"; }
  SECTION("malformed review mode") { ground["review_mode"] = 7; }
  SECTION("missing binding") { ground.erase("execution_contract_sha256"); }
  CHECK_FALSE(matches(ground));
}

TEST_CASE("internet polynomial direct reference agrees without Horner recurrence", "[internet][polynomial]") {
  const std::vector<mpq_class> coefficients{mpq_class(1, 6), -1, 1};
  const auto horner_ir = internet_exact_polynomial_ir(coefficients);
  const auto direct_ir = internet_exact_polynomial_reference_ir(coefficients);
  const auto horner = internet_exact_scalar_program(horner_ir);
  const auto direct = internet_exact_scalar_program(direct_ir);
  REQUIRE(direct.polynomial->direct_power_sum);
  CHECK(direct.bounded_steps == static_cast<int>(direct_ir.at("nodes").size()));
  for (int numerator = -7; numerator <= 7; ++numerator) {
    mpq_class x(numerator, 7);
    x.canonicalize();
    mpq_class expected = x * x - x + mpq_class(1, 6);
    expected.canonicalize();
    CHECK(internet_execute_exact_scalar(direct, x) == expected);
    CHECK(internet_execute_exact_scalar(horner, x) == expected);
  }
  auto broken = direct_ir;
  broken["nodes"][1]["operands"][0] = {{"constant", "2"}};
  CHECK_THROWS(internet_exact_scalar_program(broken));
  CHECK(internet_exact_scalar_meaning(horner, "x", "y") !=
        internet_exact_scalar_meaning(direct, "x", "y"));
  CHECK_FALSE(internet_protocol_family_matches(direct_ir, "candidate",
      internet_polynomial_protocol_version, Json::object()));
  std::vector<mpq_class> top(17, 0);
  top.back() = 1;
  CHECK(internet_execute_exact_scalar(internet_exact_scalar_program(
      internet_exact_polynomial_reference_ir(top)), mpq_class(1, 2)) == mpq_class(1, 65536));
}

TEST_CASE("internet polynomial raw measurements retain observations without gate scores", "[internet][polynomial]") {
  const auto horner = internet_exact_polynomial_ir({1, -2, 3});
  const auto direct = internet_exact_polynomial_reference_ir({1, -2, 3});
  const auto result = internet_measure_polynomial_pair(horner, direct, {-1, 0, 1}, 2);
  CHECK(result.at("simulated") == false);
  CHECK(result.at("qualification_claim") == "NONE");
  REQUIRE(result.at("samples").size() == 2);
  CHECK(result.at("samples")[0].at("candidate_first") == true);
  CHECK(result.at("samples")[1].at("candidate_first") == false);
  CHECK(result.at("reference_outputs") == Json::array({"6", "1", "2"}));
  CHECK_FALSE(result.contains("benchmark_track_scores"));
  CHECK_FALSE(result.contains("integrity_snapshots"));
  for (const auto &sample : result.at("samples")) {
    CHECK(sample.at("candidate").at("elapsed_nanoseconds").get<long long>() >= 0);
    CHECK(sample.at("baseline").at("elapsed_nanoseconds").get<long long>() >= 0);
    CHECK(sample.at("candidate").at("outputs_sha256") == sample.at("baseline").at("outputs_sha256"));
  }
  CHECK_THROWS(internet_measure_polynomial_pair(horner, horner, {0}, 2));
  CHECK_THROWS(internet_measure_polynomial_pair(horner,
      internet_exact_polynomial_reference_ir({1, -2, 4}), {0}, 2));
  CHECK_THROWS(internet_measure_polynomial_pair(horner, direct, {2}, 2));
  CHECK_THROWS(internet_measure_polynomial_pair(horner, direct, {}, 2));
  CHECK_THROWS(internet_measure_polynomial_pair(horner, direct, {0}, 101));
}

#include "statewright/egcf/internet_polynomial_form.hpp"

TEST_CASE("internet full polynomial form preserves high order terms", "[internet][polynomial]") {
  using namespace statewright::egcf;
  std::vector<mpq_class> coefficients(17, mpq_class(0));
  coefficients[0] = 1;
  coefficients[16] = 1;
  const auto form = make_internet_polynomial_form(
      internet_exact_polynomial_ir(coefficients), "exact input x", "1+x^16");
  REQUIRE(read_internet_polynomial_form(form) == form);
  REQUIRE(form.at("mathematical_contract").at("coefficients").size() == 17);
  REQUIRE(form.at("mathematical_contract").at("coefficients").at(16) == "1");
  REQUIRE_FALSE(form.contains("canonical_admission_eligible"));
  REQUIRE_FALSE(form.contains("independent_acquisition"));
  const auto other = make_internet_polynomial_form(
      internet_exact_polynomial_ir({1}), "exact input x", "1+x^16");
  REQUIRE(form.at("mathematical_signature") != other.at("mathematical_signature"));
}

TEST_CASE("internet polynomial form separates behavior from execution identity", "[internet][polynomial]") {
  using namespace statewright::egcf;
  const auto make = [](const std::vector<mpq_class> &coefficients, std::size_t budget) {
    return make_internet_polynomial_form(
        internet_exact_polynomial_ir(coefficients, budget), "x", "y");
  };
  const auto short_form = make({1, 2}, 1024);
  const auto padded_form = make({1, 2, 0}, 1024);
  const auto larger_budget = make({1, 2}, 2048);
  REQUIRE(short_form.at("mathematical_signature") == padded_form.at("mathematical_signature"));
  REQUIRE(short_form.at("execution_ir_sha256") != padded_form.at("execution_ir_sha256"));
  REQUIRE(short_form.at("representation_signature") != padded_form.at("representation_signature"));
  REQUIRE(short_form.at("mathematical_signature") == larger_budget.at("mathematical_signature"));
  REQUIRE(short_form.at("execution_contract_sha256") != larger_budget.at("execution_contract_sha256"));
  const auto zero = make({0, 0, 0}, 1024);
  REQUIRE(zero.at("mathematical_contract").at("coefficients").size() == 1);
  REQUIRE(read_internet_polynomial_form(zero) == zero);
}

TEST_CASE("internet polynomial form rejects tampering and unsupported claims", "[internet][polynomial]") {
  using namespace statewright::egcf;
  const auto form = make_internet_polynomial_form(
      internet_exact_polynomial_ir({1, 0, 1}), "x", "y");
  auto changed = form;
  changed["mathematical_contract"]["coefficients"][2] = "2";
  REQUIRE_THROWS(read_internet_polynomial_form(changed));
  changed = form;
  changed["mathematical_contract"]["input_minimum"] = "-2";
  REQUIRE_THROWS(read_internet_polynomial_form(changed));
  changed = form;
  changed["execution_contract"]["maximum_integer_bits"] = 32;
  REQUIRE_THROWS(read_internet_polynomial_form(changed));
  changed = form;
  changed["representation_signature"] = std::string(64, '0');
  REQUIRE_THROWS(read_internet_polynomial_form(changed));
  changed = form;
  changed["canonical_admission_eligible"] = true;
  REQUIRE_THROWS(read_internet_polynomial_form(changed));
  changed = form;
  changed["representation_version"] = "unknown";
  REQUIRE_THROWS(read_internet_polynomial_form(changed));
  REQUIRE_THROWS(read_internet_polynomial_form(statewright::contracts::Json::array()));
  REQUIRE_THROWS(make_internet_polynomial_form(
      internet_exact_polynomial_reference_ir({1, 0, 1}), "x", "y"));
  REQUIRE_THROWS(make_internet_polynomial_form(
      internet_exact_polynomial_ir({1}), " \t", "y"));
  REQUIRE_THROWS(make_internet_polynomial_form(
      internet_exact_polynomial_ir({1}), "x", std::string(4097, 'y')));
}

#include "statewright/egcf/internet_polynomial_artifact.hpp"
#include <chrono>

TEST_CASE("internet polynomial artifact survives native store reopen without duplicate events", "[internet][polynomial]") {
  using namespace statewright::egcf;
  const auto workspace = std::filesystem::temp_directory_path() /
      ("statewright-polynomial-artifact-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto resources = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources";
  const auto form = make_internet_polynomial_form(
      internet_exact_polynomial_ir({1, 0, 1}), "x", "x squared plus one");
  std::string artifact_id;
  std::string event_head;
  {
    EgcfStore store(workspace, resources);
    artifact_id = register_internet_polynomial_form_artifact(
        store, form, {}, "2026-09-07T00:00:00Z");
    event_head = store.event_head();
    REQUIRE(load_internet_polynomial_form_artifact(store, artifact_id) == form);
    REQUIRE(register_internet_polynomial_form_artifact(
        store, form, {}, "2026-09-07T00:00:00Z") == artifact_id);
    REQUIRE(store.event_head() == event_head);
    REQUIRE(store.get(artifact_id).object_type == "artifact");
    REQUIRE(store.get(artifact_id).payload.at("provenance").at("qualification_claim") == "NONE");
    REQUIRE(store.list("egcf-evidence").empty());
    REQUIRE(store.list("internet-probation-admission").empty());
    REQUIRE_THROWS(register_internet_candidate_polynomial_form(
        store, artifact_id, "2026-09-07T00:00:00Z"));
    REQUIRE_THROWS(register_internet_polynomial_form_artifact(store, form, {}, ""));
  }
  {
    EgcfStore store(workspace, resources);
    REQUIRE(load_internet_polynomial_form_artifact(store, artifact_id) == form);
    REQUIRE(register_internet_polynomial_form_artifact(
        store, form, {}, "2026-09-07T00:00:00Z") == artifact_id);
    REQUIRE(store.event_head() == event_head);
    REQUIRE(store.list("artifact").size() == 1);
    auto altered = store.get(artifact_id);
    altered.payload["provenance"]["qualification_claim"] = "APPROVED";
    const auto altered_id = store.register_record(altered);
    REQUIRE_THROWS(load_internet_polynomial_form_artifact(store, altered_id));
  }
  std::filesystem::remove_all(workspace);
}

#include "statewright/egcf/internet_polynomial_qualification_binding.hpp"

TEST_CASE("internet polynomial qualification binds the full execution contract", "[internet][polynomial]") {
  using namespace statewright::egcf;
  using statewright::contracts::Json;
  const auto ir = internet_exact_polynomial_ir({1, 0, 1});
  const auto form = make_internet_polynomial_form(ir, "x", "y");
  // Synthetic material for the pure binding validator, not a registered receipt
  // or a claim that the independent evidence/promotion gates have been passed.
  const Json material = {
      {"internal_ir_only", true}, {"downloaded_code_executed", false},
      {"identical_frozen_contexts", true}, {"invariants_passed", true},
      {"known_failure_retry_blocked", false}, {"experiment_qualified", true},
      {"benchmark_passed", true}, {"integrity_passed", true},
      {"blocking_reasons", Json::array()},
      {"canonical_candidate_ir", statewright::saa::to_json(statewright::saa::canonicalize_mapping(ir))},
      {"experiment_design", {{"execution_contract", form.at("execution_contract")}}},
      {"evidence_ids", Json::array({"unregistered-test-placeholder"})}};
  REQUIRE_NOTHROW(verify_internet_polynomial_qualification_form(form, material));
  for (const auto *field : {"internal_ir_only", "identical_frozen_contexts",
                            "invariants_passed", "experiment_qualified",
                            "benchmark_passed", "integrity_passed"}) {
    auto changed = material;
    changed[field] = false;
    REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
    changed.erase(field);
    REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  }
  for (const auto *field : {"downloaded_code_executed", "known_failure_retry_blocked"}) {
    auto changed = material;
    changed[field] = true;
    REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
    changed.erase(field);
    REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  }
  auto changed = material;
  changed["canonical_candidate_ir"] = statewright::saa::to_json(
      statewright::saa::canonicalize_mapping(internet_exact_polynomial_ir({1, 0, 2})));
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  changed = material;
  changed["canonical_candidate_ir"] = statewright::saa::to_json(
      statewright::saa::canonicalize_mapping(internet_exact_polynomial_ir({1, 0, 1}, 1024)));
  changed["experiment_design"]["execution_contract"] = internet_polynomial_contract(
      internet_exact_polynomial_program(internet_exact_polynomial_ir({1, 0, 1}, 1024)));
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  changed = material;
  changed["experiment_design"]["execution_contract"]["input_minimum"] = "-2";
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  changed = material;
  changed["experiment_design"].erase("execution_contract");
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  changed = material;
  changed["blocking_reasons"] = Json::array({"MISSING_REVIEW"});
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  changed = material;
  changed["evidence_ids"] = Json::array();
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
}

#include "statewright/egcf/internet_polynomial_store.hpp"

TEST_CASE("internet qualified polynomial native records cannot claim admission or use dangling proof", "[internet][polynomial]") {
  using namespace statewright::egcf;
  using statewright::contracts::Json;
  const auto resources = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources";
  const auto workspace = std::filesystem::temp_directory_path() /
      ("statewright-qualified-polynomial-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto form = make_internet_polynomial_form(
      internet_exact_polynomial_ir({1, 0, 1}), "x", "y");
  const Json payload = {
      {"schema_version", 1}, {"version", internet_polynomial_qualified_form_version},
      {"candidate_id", "internet-algorithm-candidate:sha256:" + std::string(64, '1')},
      {"qualification_id", "internet-experiment-qualification:sha256:" + std::string(64, '2')},
      {"snapshot_id", "internet-source-snapshot:sha256:" + std::string(64, '3')},
      {"source_fragment_id", "internet-source-fragment:sha256:" + std::string(64, '4')},
      {"source_policy_assessment_id", "internet-policy-assessment:sha256:" + std::string(64, '5')},
      {"form", form}, {"recorded_at", "2026-09-07T00:00:00Z"},
      {"lifecycle_claim", "QUALIFICATION_BOUND_NOT_ADMITTED"}};
  const RecordSchemaRegistry schemas(resources);
  REQUIRE_NOTHROW(schemas.validate_record_payload("internet-polynomial-qualified-form", payload));
  auto changed = payload;
  changed["lifecycle_claim"] = "CANONICAL";
  REQUIRE_THROWS(schemas.validate_record_payload("internet-polynomial-qualified-form", changed));
  changed = payload;
  changed["approved"] = true;
  REQUIRE_THROWS(schemas.validate_record_payload("internet-polynomial-qualified-form", changed));
  changed = payload;
  changed.erase("qualification_id");
  REQUIRE_THROWS(schemas.validate_record_payload("internet-polynomial-qualified-form", changed));
  {
    EgcfStore store(workspace, resources);
    // The low-level record schema checks shape, not evidence truth. A structurally
    // valid fixture with dangling references must fail the guarded loader.
    const auto id = store.register_record({.object_type = "internet-polynomial-qualified-form", .payload = payload});
    REQUIRE_THROWS(load_qualified_internet_polynomial_form(store, id));
    REQUIRE_THROWS(register_qualified_internet_polynomial_form(
        store, payload.at("candidate_id").get<std::string>(),
        payload.at("qualification_id").get<std::string>(), "2026-09-07T00:00:00Z"));
    REQUIRE(store.list("internet-probation-admission").empty());
  }
  std::filesystem::remove_all(workspace);
}

#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/internet_polynomial_recovery.hpp"
#include "statewright/egcf/internet_polynomial_canonical.hpp"
#include "statewright/egcf/internet_improvement_orchestrator.hpp"
#include <sys/wait.h>
#include <unistd.h>

TEST_CASE("internet qualified polynomial checks native source linkage", "[internet][polynomial]") {
  using namespace statewright::egcf;
  using statewright::contracts::Json;
  bool mismatched_fragment = false;
  bool mismatched_assessment = false;
  int checkpoint_phase = 0;
  bool reconciliation_case = false;
  bool competing_successor = false;
  bool orchestrator_reconciliation = false;
  bool expired_recovery_lease = false;
  bool canonical_admission_case = false;
  std::string recovery_candidate_id;
  std::string recovery_qualification_id;
  SECTION("valid synthetic linkage can be stored and loaded") {}
  SECTION("fragment from another snapshot is rejected") { mismatched_fragment = true; }
  SECTION("assessment of another snapshot is rejected") { mismatched_assessment = true; }
  SECTION("resume after abrupt exit following form persistence") { checkpoint_phase = 1; }
  SECTION("resume after abrupt exit following candidate supersession") { checkpoint_phase = 2; }
  SECTION("reconcile qualification before candidate update without rerunning experiments") { reconciliation_case = true; }
  SECTION("reconciliation rejects a competing candidate successor") {
    reconciliation_case = true;
    competing_successor = true;
  }
  SECTION("orchestrator reconciles a polynomial qualification under its active lease") {
    reconciliation_case = true;
    orchestrator_reconciliation = true;
  }
  SECTION("orchestrator reconciles a polynomial qualification after lease expiry") {
    reconciliation_case = true;
    orchestrator_reconciliation = true;
    expired_recovery_lease = true;
  }
  SECTION("policy-bound polynomial canonical admission is idempotent and freshness gated") {
    canonical_admission_case = true;
  }
  const auto resources = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources";
  const auto workspace = std::filesystem::temp_directory_path() /
      ("statewright-polynomial-linkage-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  {
    EgcfStore store(workspace, resources);
    // Deliberately synthetic native records test storage/linkage only. They are
    // not collected internet evidence and do not exercise qualification review.
    const auto register_record = [&](const char *type, const Json &payload) {
      return store.register_record({.object_type = type, .payload = payload});
    };
    statewright::sources::InternetSourceSnapshot source_snapshot;
    source_snapshot.artifact_id = "synthetic-fixture";
    source_snapshot.body_sha256 = std::string(64, 'a');
    source_snapshot.body_size = 1;
    source_snapshot.canonical_url = "https://example.invalid/polynomial-fixture";
    source_snapshot.final_url = source_snapshot.canonical_url;
    source_snapshot.content_type = "text/plain";
    source_snapshot.source_group = "TEST_ONLY";
    const auto snapshot_payload = statewright::sources::to_json(
        statewright::sources::canonical_source_snapshot(source_snapshot));
    const auto snapshot_id = register_record("internet-source-snapshot", snapshot_payload);
    source_snapshot.body_sha256 = std::string(64, 'b');
    const auto other_snapshot = statewright::sources::to_json(
        statewright::sources::canonical_source_snapshot(source_snapshot));
    const auto other_snapshot_id = register_record("internet-source-snapshot", other_snapshot);
    const auto fragment_id = register_record("internet-source-fragment", {
        {"schema_version", 1}, {"byte_start", 0}, {"byte_end", 1},
        {"fragment_kind", "MATH_EXPRESSION"}, {"fragment_signature", "synthetic"},
        {"language", "en"}, {"metadata", {{"test_only", true}}},
        {"selector", "fixture"}, {"text", "synthetic polynomial"},
        {"snapshot_id", mismatched_fragment ? other_snapshot_id : snapshot_id}});
    std::string fetch_receipt_id = "synthetic";
    if (canonical_admission_case) {
      statewright::sources::InternetFetchLease source_lease;
      source_lease.job_id = "synthetic-fetch-job";
      source_lease.worker_id = "synthetic-worker";
      source_lease.acquired_at = "2026-09-07T00:00:00Z";
      source_lease.expires_at = "2026-09-07T00:01:00Z";
      source_lease.state = "COMPLETED";
      const auto fetch_lease_id = register_record("internet-fetch-lease",
          statewright::sources::to_json(statewright::sources::canonical_fetch_lease(source_lease)));
      statewright::sources::InternetFetchReceipt source_receipt;
      source_receipt.job_id = source_lease.job_id;
      source_receipt.lease_id = fetch_lease_id;
      source_receipt.snapshot_id = snapshot_id;
      source_receipt.compressed_bytes = 1;
      source_receipt.decompressed_bytes = 1;
      source_receipt.final_url = source_snapshot.final_url;
      source_receipt.requested_url = source_snapshot.canonical_url;
      source_receipt.http_status = 200;
      source_receipt.provider_identity = "synthetic-test-provider";
      source_receipt.resolved_addresses = {"93.184.216.34"};
      source_receipt.status = "FETCH_SUCCEEDED";
      source_receipt.tls_verified = true;
      source_receipt.total_time_milliseconds = 1;
      fetch_receipt_id = register_record("internet-fetch-receipt",
          statewright::sources::to_json(statewright::sources::canonical_fetch_receipt(source_receipt)));
    }
    statewright::sources::InternetPolicyAssessment source_assessment;
    source_assessment.credential_free = true;
    source_assessment.encoding_valid = true;
    source_assessment.fetch_receipt_id = fetch_receipt_id;
    source_assessment.license_classification = "TEST_ONLY";
    source_assessment.mime_valid = true;
    source_assessment.public_address_valid = true;
    source_assessment.redirects_valid = true;
    source_assessment.robots_allowed = true;
    source_assessment.size_valid = true;
    source_assessment.source_policy_id = "synthetic";
    source_assessment.snapshot_id = mismatched_assessment ? other_snapshot_id : snapshot_id;
    const auto assessment_id = register_record("internet-policy-assessment",
        statewright::sources::to_json(statewright::sources::canonical_policy_assessment(source_assessment)));
    InternetAlgorithmCandidate candidate;
    candidate.source_fragment_id = fragment_id;
    candidate.snapshot_id = snapshot_id;
    candidate.source_policy_assessment_id = assessment_id;
    candidate.proposed_saa_ir = internet_exact_polynomial_ir({1, 0, 1});
    candidate.semantic_inputs = {"x"};
    candidate.semantic_outputs = {"y"};
    InternetKnowledgeSearchReceipt retrieval;
    retrieval.source_fragment_id = fragment_id;
    retrieval.snapshot_id = snapshot_id;
    retrieval.source_policy_assessment_id = assessment_id;
    retrieval.search_complete = true;
    retrieval.novelty_status = "NOVEL_CANDIDATE";
    candidate.retrieval_receipt_id = register_record("internet-retrieval-receipt", to_json(retrieval));
    candidate.status = "VALIDATION_READY";
    candidate = canonical_internet_algorithm_candidate(candidate);
    const auto origin_id = register_record("internet-algorithm-candidate", to_json(candidate));
    EvidenceArtifact evidence;
    evidence.subject_id = origin_id;
    evidence.category = "TEST_ONLY";
    evidence.producer = "synthetic-test-fixture";
    evidence.method = "storage-linkage-test";
    evidence.created_at = "2026-09-07T00:00:00Z";
    evidence.content = {{"test_only", true}};
    evidence.sha256 = statewright::contracts::sha256_json(evidence.content);
    evidence.simulated = true;
    evidence.limitations = {"Not qualification or approval evidence"};
    const auto evidence_id = register_record("egcf-evidence", to_json(evidence));
    InternetExperimentProtocol protocol;
    std::string protocol_id;
    if (reconciliation_case) {
      protocol.protocol_version = std::string(internet_polynomial_protocol_version);
      protocol.baseline_ref = "synthetic";
      protocol.baseline_saa_ir = internet_exact_polynomial_reference_ir({1, 0, 1});
      protocol.dataset_snapshot_ids = {snapshot_id};
      protocol.applicable_candidate_statuses = {"VALIDATION_READY"};
      protocol.trial_groups = Json::array({{
          {"independence_group", "synthetic-not-independent"},
          {"deterministic_seed", 1}, {"inputs", Json::array({"0"})},
          {"expected_outputs", Json::array({"1"})}}});
      protocol.minimum_trials_per_group = 1;
      protocol.minimum_experiments = 1;
      protocol.minimum_independence_groups = 1;
      protocol.maximum_total_trials = 4;
      protocol.valid_from = "2026-09-07T00:00:00Z";
      protocol.source_provenance = {{"test_only", true}};
      protocol = canonical_internet_experiment_protocol(protocol);
      protocol_id = register_record("internet-experiment-protocol", to_json(protocol));
    }
    InternetExperimentQualification qualification;
    qualification.candidate_id = origin_id;
    qualification.baseline_ref = "synthetic";
    qualification.dataset_snapshot_ids = {snapshot_id};
    qualification.context_signature = std::string(64, 'c');
    qualification.canonical_candidate_ir = statewright::saa::to_json(
        statewright::saa::canonicalize_mapping(candidate.proposed_saa_ir));
    qualification.canonical_baseline_ir = statewright::saa::to_json(
        statewright::saa::canonicalize_mapping(internet_exact_polynomial_reference_ir({1, 0, 1})));
    qualification.experiment_design = {{"execution_contract", internet_polynomial_contract(
        internet_exact_polynomial_program(candidate.proposed_saa_ir))}};
    if (reconciliation_case) {
      qualification.experiment_design["grounded_protocol_id"] = protocol_id;
    }
    qualification.evidence_ids = {evidence_id};
    qualification.identical_frozen_contexts = true;
    qualification.invariants_passed = true;
    qualification.experiment_qualified = true;
    qualification.benchmark_passed = true;
    qualification.integrity_passed = true;
    qualification.status = "EXPERIMENT_QUALIFIED";
    qualification = canonical_internet_experiment_qualification(qualification);
    const auto qualification_id = register_record("internet-experiment-qualification", to_json(qualification));
    candidate.experiment_qualification_ids = {qualification_id};
    candidate.status = "EXPERIMENT_QUALIFIED";
    candidate = canonical_internet_algorithm_candidate(candidate);
    const auto candidate_id = reconciliation_case ? candidate.object_id()
        : register_record("internet-algorithm-candidate", to_json(candidate));
    recovery_candidate_id = candidate_id;
    recovery_qualification_id = qualification_id;
    if (mismatched_fragment || mismatched_assessment) {
      REQUIRE_THROWS(register_qualified_internet_polynomial_form(
          store, candidate_id, qualification_id, "2026-09-07T00:00:00Z"));
    } else if (canonical_admission_case) {
      InternetImprovementStore internet(store);
      const auto checkpoint = checkpoint_qualified_internet_polynomial_candidate(
          store, candidate_id, qualification_id, "2026-09-07T00:00:00Z");
      auto checkpointed = internet_algorithm_candidate_from_json(store.get(checkpoint.candidate_id).payload);
      statewright::saa::AutonomousPromotionPolicy policy;
      policy.domain_scopes = {"*"};
      policy.allowed_candidate_classes = {"NOVEL_CANDIDATE"};
      policy.prohibited_primitives = {"INVOKE"};
      policy.prohibited_capability_classes = {"COMMAND_EXECUTION", "FILESYSTEM_MUTATION", "NETWORK_IO", "PROCESS_EXECUTION"};
      policy.automatic_demotion_predicates = {"BENCHMARK_REGRESSION", "INVARIANT_FAILURE", "KNOWLEDGE_INTEGRITY_REGRESSION", "SOURCE_RETRACTION"};
      policy = statewright::saa::canonical_autonomous_promotion_policy(policy);
      const auto policy_id = internet.register_promotion_policy(policy);
      // Synthetic policy inputs exercise native evaluation and storage. They do
      // not represent collected measurements, independent review or acceptance.
      statewright::saa::AutonomousPromotionInputs inputs;
      inputs.candidate_ref = checkpoint.candidate_id;
      inputs.candidate_status = "EXPERIMENT_QUALIFIED";
      inputs.candidate_class = "NOVEL_CANDIDATE";
      inputs.candidate_domain = "example.invalid";
      inputs.candidate_primitives = {"CONST", "ADD", "MULTIPLY"};
      inputs.source_policy_assessment_ref = assessment_id;
      inputs.snapshot_ref = snapshot_id;
      inputs.retrieval_receipt_ref = checkpointed.retrieval_receipt_id;
      inputs.experiment_qualification_ref = qualification_id;
      inputs.source_policy_passed = true;
      inputs.snapshot_integrity_passed = true;
      inputs.independent_source_groups = 1;
      inputs.semantic_strength = "DETERMINISTIC_SOURCE_BOUND";
      inputs.mathematical_strength = "EXACT_STRUCTURAL";
      inputs.existing_knowledge_search_complete = true;
      inputs.experiment_qualified = true;
      inputs.independent_experiment_groups = 2;
      inputs.benchmark_gate.candidate_ref = checkpoint.candidate_id;
      inputs.benchmark_gate.profile_signature = std::string(64, '1');
      inputs.benchmark_gate.policy_signature = std::string(64, '2');
      inputs.benchmark_gate.evidence_requirement_coverage_bp = 10000;
      inputs.benchmark_gate.independence_groups = {"synthetic-a", "synthetic-b"};
      inputs.benchmark_gate.independent_review = true;
      inputs.benchmark_gate.status = "OIEC_BENCH_PROMOTION_GATE_PASSED";
      inputs.benchmark_gate.canonical_promotion_eligible = true;
      inputs.benchmark_gate.assessment_signature = std::string(64, '3');
      inputs.integrity_trajectory.snapshot_signatures = {std::string(64, '4')};
      inputs.integrity_trajectory.latest_generation = 1;
      inputs.integrity_trajectory.status = "KNOWLEDGE_INTEGRITY_QUALIFIED_STABLE";
      inputs.integrity_trajectory.knowledge_integrity_qualified = true;
      inputs.integrity_trajectory.trajectory_signature = std::string(64, '5');
      inputs.invariants_passed = true;
      inputs.probation_plan_valid = true;
      inputs.demotion_path_valid = true;
      const auto promotion = statewright::saa::evaluate_autonomous_promotion(policy, inputs);
      REQUIRE(promotion.promotion_allowed);
      const auto promotion_id = internet.register_promotion_assessment(
          policy_id, promotion, "2026-09-07T00:00:10Z", "2026-09-07T00:00:00Z");
      checkpointed.status = "POLICY_QUALIFIED";
      checkpointed.promotion_assessment_ids = {promotion_id};
      checkpointed = canonical_internet_algorithm_candidate(checkpointed);
      const auto policy_candidate_id = internet.supersede_algorithm_candidate(
          checkpoint.candidate_id, checkpointed, "synthetic policy fixture");
      const auto admitted = admit_internet_polynomial_canonical(
          store, policy_candidate_id, "2026-09-07T00:00:20Z");
      REQUIRE(admitted.created);
      REQUIRE(admitted.native_generation == store.projection_checkpoint().object_count);
      const auto canonical = read_internet_polynomial_canonical(store, admitted.canonical_id);
      REQUIRE(canonical.at("probation_required") == true);
      REQUIRE(canonical.at("form").at("execution_ir") == candidate.proposed_saa_ir);
      const auto catalogue_match = exact_capability_search(store, checkpointed);
      REQUIRE(catalogue_match.at("candidates").size() == 1);
      REQUIRE(catalogue_match.at("candidates").at(0).at("canonical_id") == admitted.canonical_id);
      REQUIRE(catalogue_match.at("selected_canonical_id").is_null());
      auto padded_candidate = checkpointed;
      padded_candidate.proposed_saa_ir = internet_exact_polynomial_ir({1, 0, 1, 0});
      REQUIRE(exact_capability_search(store, padded_candidate).at("candidates").size() == 1);
      auto different_meaning = checkpointed;
      different_meaning.semantic_outputs = {"different physical quantity"};
      REQUIRE(exact_capability_search(store, different_meaning).at("candidates").empty());
      auto scalar_polynomial = checkpointed;
      scalar_polynomial.proposed_saa_ir = internet_exact_polynomial_ir({0, 1});
      REQUIRE(exact_capability_search(store, scalar_polynomial).contains("legacy_scalar_search"));
      const auto head = store.event_head();
      const auto reused = admit_internet_polynomial_canonical(
          store, policy_candidate_id, "2026-09-07T00:00:21Z");
      REQUIRE_FALSE(reused.created);
      REQUIRE(reused.canonical_id == admitted.canonical_id);
      REQUIRE(reused.native_generation == admitted.native_generation);
      REQUIRE(store.event_head() == head);
      InternetProbationController controller(store);
      const auto probation = controller.admit(checkpointed, {}, "2026-09-07T00:00:21Z");
      REQUIRE(probation.polynomial_admission.has_value());
      REQUIRE(probation.polynomial_admission->canonical_id == admitted.canonical_id);
      REQUIRE(probation.canonical_admission.canonical_id.empty());
      REQUIRE(probation.updated_candidate.status == "PROBATIONARY_CANONICAL");
      REQUIRE(probation.updated_candidate.promotion_decision_ids.empty());
      const auto probation_record = store.get(probation.admission_id);
      REQUIRE(probation_record.payload.at("generation_basis") == "EGCF_OBJECT_COUNT_V1");
      REQUIRE(probation_record.payload.at("canonical_store_generation") == admitted.native_generation);
      REQUIRE(to_json(probation).at("canonical_admission").is_null());
      std::string canary_query;
      std::string fallback_query;
      for (int index = 0; index < 10000 && (canary_query.empty() || fallback_query.empty()); ++index) {
        const auto query = statewright::contracts::sha256_json(Json(index));
        if (statewright::saa::probation_canary_selected(probation.plan, query)) canary_query = query;
        else fallback_query = query;
      }
      REQUIRE_FALSE(canary_query.empty());
      REQUIRE_FALSE(fallback_query.empty());
      const auto execution_head = store.event_head();
      REQUIRE(controller.select(probation.updated_candidate, canary_query).selected_canonical_ref == admitted.canonical_id);
      REQUIRE(controller.execute_selected_polynomial(probation.updated_candidate, canary_query,
          mpq_class(1, 2), "2026-09-07T00:00:22Z") == mpq_class(5, 4));
      REQUIRE_FALSE(controller.execute_selected_polynomial(probation.updated_candidate, fallback_query,
          mpq_class(1, 2), "2026-09-07T00:00:22Z").has_value());
      REQUIRE_THROWS(controller.execute_selected_polynomial(probation.updated_candidate, canary_query,
          mpq_class(2), "2026-09-07T00:00:22Z"));
      REQUIRE_THROWS(controller.execute_selected_polynomial(probation.updated_candidate, canary_query,
          mpq_class(0), "2026-09-09T00:00:01Z"));
      REQUIRE(store.event_head() == execution_head);
      const auto measurement_design = freeze_internet_polynomial_measurement(
          store, policy_candidate_id, {mpq_class(-1), mpq_class(0), mpq_class(1, 2)}, 2);
      InternetExperimentProtocol frozen_protocol;
      frozen_protocol.protocol_version = internet_polynomial_protocol_version;
      frozen_protocol.applicable_candidate_statuses = {"VALIDATION_READY"};
      frozen_protocol.baseline_ref = "synthetic-baseline-not-qualified";
      frozen_protocol.baseline_saa_ir = internet_exact_polynomial_reference_ir({1, 0, 1});
      frozen_protocol.dataset_snapshot_ids = {snapshot_id};
      frozen_protocol.trial_groups = Json::array({
          {{"group_id", "synthetic-anchors"}, {"inputs", {"-1", "0"}}, {"expected_outputs", {"2", "1"}}},
          {{"group_id", "synthetic-fractions"}, {"inputs", {"1/2", "-1/2"}}, {"expected_outputs", {"5/4", "5/4"}}}});
      frozen_protocol.valid_from = "2026-09-07T00:00:00Z";
      frozen_protocol.source_provenance = {{"grounded", {
          {"candidate_id", policy_candidate_id},
          {"candidate_ir_sha256", statewright::contracts::sha256_json(candidate.proposed_saa_ir)},
          {"source_fragment_id", fragment_id},
          {"source_body_sha256", snapshot_payload.at("body_sha256")},
          {"author_identity", "synthetic-fixture"},
          {"measurement_evidence_id", ""}, {"review_evidence_ids", Json::array()}}}};
      const auto protocol_freeze = freeze_internet_polynomial_protocol(store, frozen_protocol);
      REQUIRE_NOTHROW(verify_internet_polynomial_protocol_freeze(store, frozen_protocol, protocol_freeze));
      InternetExperimentRequest freeze_request;
      freeze_request.recorded_at = "2099-01-01T00:00:00Z";
      freeze_request.protocol_id = register_record("internet-experiment-protocol",
          to_json(canonical_internet_experiment_protocol(frozen_protocol)));
      const auto freeze_error = [&]() {
        try {
          static_cast<void>(validate_grounded_experiment(store, checkpointed, freeze_request));
        } catch (const std::exception &error) {
          return std::string(error.what());
        }
        return std::string{};
      };
      REQUIRE(freeze_error().find("POLYNOMIAL_PROTOCOL_FREEZE_REQUIRED") != std::string::npos);
      auto protocol_with_freeze = frozen_protocol;
      protocol_with_freeze.source_provenance["grounded"]["freeze_evidence_id"] = protocol_freeze;
      REQUIRE_NOTHROW(verify_internet_polynomial_protocol_freeze(store, protocol_with_freeze, protocol_freeze));
      protocol_with_freeze.minimum_output = "-999";
      freeze_request.protocol_id = register_record("internet-experiment-protocol",
          to_json(canonical_internet_experiment_protocol(protocol_with_freeze)));
      REQUIRE(freeze_error().find("POLYNOMIAL_PROTOCOL_FROZEN_DESIGN_CHANGED") != std::string::npos);
      auto changed_protocol = frozen_protocol;
      changed_protocol.minimum_output = "-999";
      REQUIRE_THROWS(verify_internet_polynomial_protocol_freeze(store, changed_protocol, protocol_freeze));
      changed_protocol = frozen_protocol;
      changed_protocol.trial_groups.at(0)["expected_outputs"] = Json::array({"3", "1"});
      REQUIRE_THROWS(verify_internet_polynomial_protocol_freeze(store, changed_protocol, protocol_freeze));
      changed_protocol = frozen_protocol;
      changed_protocol.protocol_version = "saa-grounded-experiment-v2";
      REQUIRE_THROWS(freeze_internet_polynomial_protocol(store, changed_protocol));
      changed_protocol = frozen_protocol;
      changed_protocol.source_provenance["grounded"]["review_evidence_ids"] = Json::array({"not-a-review"});
      REQUIRE_NOTHROW(verify_internet_polynomial_protocol_freeze(store, changed_protocol, protocol_freeze));
      changed_protocol.source_provenance["grounded"]["reference_comparison_evidence_id"] = "outcome-collected-later";
      REQUIRE_NOTHROW(verify_internet_polynomial_protocol_freeze(store, changed_protocol, protocol_freeze));
      changed_protocol.source_provenance["grounded"]["reference_design_evidence_id"] = "changed-design";
      REQUIRE_THROWS(verify_internet_polynomial_protocol_freeze(store, changed_protocol, protocol_freeze));
      REQUIRE_FALSE(evidence_artifact_from_json(store.get(protocol_freeze).payload).success.has_value());
      auto timed_protocol = frozen_protocol;
      timed_protocol.source_provenance["grounded"]["measurement_repetitions"] = 2;
      const auto timed_freeze = freeze_internet_polynomial_protocol(store, timed_protocol);
      const std::vector<mpq_class> timed_inputs = {-1, 0, mpq_class(1, 2), mpq_class(-1, 2)};
      const auto timed_design = freeze_internet_polynomial_measurement(
          store, policy_candidate_id, timed_inputs, 2, timed_freeze);
      REQUIRE_THROWS(freeze_internet_polynomial_measurement(
          store, policy_candidate_id, {mpq_class(0)}, 2, timed_freeze));
      REQUIRE_THROWS(freeze_internet_polynomial_measurement(
          store, policy_candidate_id, timed_inputs, 3, timed_freeze));
      const auto timed_result_id = collect_internet_polynomial_measurement(store, timed_design);
      const auto timed_result = evidence_artifact_from_json(store.get(timed_result_id).payload);
      const auto timed_start = evidence_artifact_from_json(
          store.get(timed_result.content.at("start_evidence_id").get<std::string>()).payload);
      REQUIRE(timed_start.category == "POLYNOMIAL_MEASUREMENT_START");
      REQUIRE(timed_start.created_at >= store.get(timed_freeze).payload.at("created_at").get<std::string>());
      REQUIRE(timed_result.created_at >= timed_start.created_at);
      REQUIRE(timed_start.content.at("design_id") == timed_design);
      REQUIRE(timed_result.content.at("polynomial_protocol_freeze_id") == timed_freeze);
      REQUIRE_FALSE(timed_start.success.has_value());
      const auto timing_design_payload = store.get(timed_design).payload.at("content");
      const auto performance = summarize_internet_polynomial_performance(
          timed_result.content.at("measurement"), timing_design_payload);
      REQUIRE(performance.at("pair_count") == 2);
      REQUIRE(performance.at("scope") == "DESCRIPTIVE_SAME_PROCESS_ONLY");
      REQUIRE(performance.at("performance_superiority").is_null());
      REQUIRE(performance.at("benchmark_score_claim") == "NONE");
      const auto coverage = internet_polynomial_measurement_coverage(timed_result.content);
      REQUIRE(coverage.at("missing_or_invalid_benchmark_tracks").size() == 7);
      REQUIRE_FALSE(coverage.at("integrity_snapshots_present").get<bool>());
      REQUIRE(coverage.at("status") == "MEASUREMENT_FIELDS_INCOMPLETE");
      Json reported_scores = Json::object();
      for (const auto track : statewright::saa::oiec_bench_tracks) reported_scores[std::string(track)] = 10000;
      const Json unverified_summary = {{"benchmark_track_scores", reported_scores},
          {"integrity_snapshots", Json::array({Json::object()})}};
      const auto unverified_coverage = internet_polynomial_measurement_coverage(unverified_summary);
      REQUIRE(unverified_coverage.at("status") == "REQUIRES_EVIDENCE_VALIDATION");
      REQUIRE(unverified_coverage.at("qualification_claim") == "NONE");
      auto malformed_timing = timed_result.content.at("measurement");
      malformed_timing["samples"][0]["candidate"]["elapsed_nanoseconds"] = -1;
      REQUIRE_THROWS(summarize_internet_polynomial_performance(malformed_timing, timing_design_payload));
      malformed_timing = timed_result.content.at("measurement");
      malformed_timing["samples"][0]["candidate"]["outputs_sha256"] = std::string(64, '0');
      REQUIRE_THROWS(summarize_internet_polynomial_performance(malformed_timing, timing_design_payload));
      malformed_timing = timed_result.content.at("measurement");
      malformed_timing["samples"][0]["candidate_first"] = false;
      REQUIRE_THROWS(summarize_internet_polynomial_performance(malformed_timing, timing_design_payload));
      // Synthetic zero-duration inputs test clock-resolution handling only.
      auto zero_timing = timed_result.content.at("measurement");
      for (auto &sample : zero_timing["samples"]) {
        sample["candidate"]["elapsed_nanoseconds"] = 0;
        sample["baseline"]["elapsed_nanoseconds"] = 0;
      }
      const auto zero_summary = summarize_internet_polynomial_performance(zero_timing, timing_design_payload);
      REQUIRE(zero_summary.at("baseline_over_candidate_total_ratio").is_null());
      REQUIRE(zero_summary.at("zero_duration_sample_count") == 4);
      REQUIRE_NOTHROW(verify_internet_polynomial_measurement_chain(
          store, timed_result_id, timed_freeze, timed_result.created_at));
      REQUIRE_THROWS(verify_internet_polynomial_measurement_chain(
          store, timed_result_id, protocol_freeze, timed_result.created_at));
      REQUIRE_THROWS(verify_internet_polynomial_measurement_chain(
          store, timed_result_id, timed_freeze, "2000-01-01T00:00:00Z"));
      REQUIRE_THROWS(verify_internet_polynomial_measurement_chain(
          store, timed_design, timed_freeze, timed_result.created_at));
      const auto separate_design = freeze_internet_polynomial_measurement(
          store, policy_candidate_id, {mpq_class(-1), mpq_class(0), mpq_class(1, 2)}, 2);
      static_cast<void>(separate_design);
      const auto measurement_id = collect_internet_polynomial_measurement(store, measurement_design);
      const auto measured = evidence_artifact_from_json(store.get(measurement_id).payload);
      REQUIRE_FALSE(measured.simulated);
      REQUIRE_FALSE(measured.success.has_value());
      REQUIRE(measured.content.at("qualification_claim") == "NONE");
      REQUIRE(measured.content.at("design_id") == measurement_design);
      REQUIRE(measured.created_at >= store.get(measurement_design).payload.at("created_at").get<std::string>());
      const auto measured_head = store.event_head();
      REQUIRE(collect_internet_polynomial_measurement(store, measurement_design) == measurement_id);
      REQUIRE(store.event_head() == measured_head);
      REQUIRE_THROWS(freeze_internet_polynomial_measurement(store, policy_candidate_id, {mpq_class(2)}, 2));
      REQUIRE_THROWS(freeze_internet_polynomial_measurement(store, policy_candidate_id, {mpq_class(0)}, 101));
      REQUIRE_THROWS(collect_internet_polynomial_measurement(store, measurement_id));
      const auto scheduled_design = freeze_internet_polynomial_measurement(
          store, policy_candidate_id, {mpq_class(0), mpq_class(1)}, 2);
      InternetImprovementRunRequest measurement_request;
      measurement_request.cycle_key = "polynomial-measurement-fixture";
      measurement_request.worker_id = "synthetic-measurement-worker";
      measurement_request.current_timestamp = "2026-09-07T00:00:30Z";
      measurement_request.action_lease_expires_at = "2026-09-07T00:02:00Z";
      measurement_request.fetch_lease_expires_at = "2026-09-07T00:02:00Z";
      measurement_request.policy.action_deadline = "2026-09-07T00:01:00Z";
      measurement_request.policy.enable_acquisition = false;
      measurement_request.policy.require_reasoning = false;
      measurement_request.policy.enabled_action_kinds = {"COLLECT_POLYNOMIAL_MEASUREMENT"};
      measurement_request.policy.polynomial_measurement_design_ids = {scheduled_design};
      measurement_request.policy.candidate_scope_id = policy_candidate_id;
      InternetImprovementOrchestrator measurement_orchestrator(store, nullptr, nullptr,
          "none", "none", [](std::string_view timestamp) { return std::string(timestamp); });
      const auto measurement_run = measurement_orchestrator.run_once(measurement_request);
      REQUIRE(measurement_run.status == "COMPLETED");
      REQUIRE(measurement_run.output_ids.size() == 1);
      REQUIRE_FALSE(measurement_run.action_lease_id.empty());
      REQUIRE_FALSE(measurement_run.action_receipt_id.empty());
      const auto scheduled_measurement = evidence_artifact_from_json(
          store.get(measurement_run.output_ids.front()).payload);
      REQUIRE(scheduled_measurement.content.at("design_id") == scheduled_design);
      REQUIRE_FALSE(scheduled_measurement.success.has_value());
      const auto evidence_count = store.list("egcf-evidence").size();
      REQUIRE_THROWS_WITH(measurement_orchestrator.resume(measurement_run.run_id, measurement_request),
          "resume target is already terminal");
      REQUIRE(store.list("egcf-evidence").size() == evidence_count);
      for (const bool expired : {false, true}) {
        const auto interrupted_design = freeze_internet_polynomial_measurement(
            store, policy_candidate_id, {mpq_class(1, expired ? 4 : 3)}, 2);
        const auto discovery_head = store.event_head();
        REQUIRE_FALSE(find_internet_polynomial_measurement(store, interrupted_design).has_value());
        REQUIRE(store.event_head() == discovery_head);
        auto recovery_request = measurement_request;
        recovery_request.cycle_key = expired ? "measurement-expired-lease" : "measurement-active-lease";
        recovery_request.policy.polynomial_measurement_design_ids = {interrupted_design};
        const auto interrupted_plan = measurement_orchestrator.plan(recovery_request);
        REQUIRE(interrupted_plan.actions.size() == 1);
        const auto interrupted_plan_id = internet.register_improvement_plan(interrupted_plan);
        InternetImprovementRun interrupted_run;
        interrupted_run.plan_id = interrupted_plan_id;
        interrupted_run.worker_id = recovery_request.worker_id;
        interrupted_run.started_at = "2026-09-07T00:00:20Z";
        interrupted_run.requested_budgets = to_json(recovery_request.policy);
        interrupted_run = canonical_internet_improvement_run(interrupted_run);
        const auto interrupted_run_id = internet.register_improvement_run(interrupted_run);
        InternetImprovementActionLease interrupted_lease;
        interrupted_lease.action_key = interrupted_plan.actions.front().action_key;
        interrupted_lease.run_id = interrupted_run_id;
        interrupted_lease.worker_id = recovery_request.worker_id;
        interrupted_lease.acquired_at = interrupted_run.started_at;
        interrupted_lease.expires_at = expired ? "2026-09-07T00:00:25Z" : "2026-09-07T00:00:40Z";
        interrupted_lease = canonical_internet_improvement_action_lease(interrupted_lease);
        static_cast<void>(internet.register_improvement_action_lease(interrupted_lease));
        // Simulate interruption after actual timings are persisted, before the
        // worker writes its action receipt. No completion record is fabricated.
        const auto interrupted_measurement = collect_internet_polynomial_measurement(store, interrupted_design);
        const auto recovery_evidence_count = store.list("egcf-evidence").size();
        auto mismatched_plan = interrupted_plan;
        mismatched_plan.actions.front().parameters["candidate_scope_id"] = origin_id;
        mismatched_plan.actions.front() = canonical_internet_directed_action(mismatched_plan.actions.front());
        mismatched_plan = canonical_internet_improvement_plan(mismatched_plan);
        const auto mismatched_plan_id = internet.register_improvement_plan(mismatched_plan);
        auto mismatched_run = interrupted_run;
        mismatched_run.plan_id = mismatched_plan_id;
        mismatched_run = canonical_internet_improvement_run(mismatched_run);
        const auto mismatched_run_id = internet.register_improvement_run(mismatched_run);
        const auto mismatch_head = store.event_head();
        REQUIRE_THROWS_WITH(measurement_orchestrator.resume(mismatched_run_id, recovery_request),
            "POLYNOMIAL_MEASUREMENT_CANDIDATE_SCOPE_MISMATCH");
        REQUIRE(store.event_head() == mismatch_head);
        REQUIRE_FALSE(internet.terminal_action_receipt(mismatched_plan.actions.front().action_key).has_value());
        const auto recovered = measurement_orchestrator.resume(interrupted_run_id, recovery_request);
        REQUIRE(recovered.output_ids == std::vector<std::string>{interrupted_measurement});
        REQUIRE_FALSE(recovered.action_receipt_id.empty());
        REQUIRE(store.list("egcf-evidence").size() == recovery_evidence_count);
        REQUIRE(internet.terminal_action_receipt(interrupted_lease.action_key).has_value());
      }
      REQUIRE_FALSE(to_json(InternetDirectorPolicy{}).contains("polynomial_measurement_design_ids"));
      InternetProbationObservationRequest observation_request;
      observation_request.query_signature = canary_query;
      observation_request.context_signature = std::string(64, 'c');
      observation_request.observed_at = "2026-09-07T00:00:23Z";
      observation_request.window_index = 0;
      observation_request.candidate_correct = true;
      observation_request.baseline_correct = true;
      observation_request.invariant_passed = true;
      observation_request.benchmark_passed = true;
      observation_request.integrity_passed = true;
      observation_request.source_valid = true;
      observation_request.reproduction_passed = true;
      observation_request.evidence_ids = store.get(qualification_id).payload.at("evidence_ids").get<std::vector<std::string>>();
      // Deliberately synthetic observation: validates lifecycle plumbing only.
      const auto observed = controller.observe(probation.updated_candidate, observation_request);
      REQUIRE(observed.updated_candidate.status == "PROBATIONARY_CANONICAL");
      REQUIRE_FALSE(observed.promotion_decision.has_value());
      REQUIRE_FALSE(observed.demotion_decision.has_value());
      REQUIRE(store.get(observed.observation_id).object_type == "internet-probation-observation");
      REQUIRE_THROWS(controller.select(probation.updated_candidate, canary_query));
      REQUIRE_THROWS(controller.observe(probation.updated_candidate, observation_request));
      REQUIRE(controller.execute_selected_polynomial(observed.updated_candidate, canary_query,
          mpq_class(1, 2), "2026-09-07T00:00:24Z") == mpq_class(5, 4));
      REQUIRE_THROWS(controller.admit(probation.updated_candidate, {}, "2026-09-07T00:00:22Z"));
      REQUIRE_THROWS(internet.register_probation_admission(
          probation.plan, admitted.canonical_id, "wrong-form", "synthetic", 1,
          "PROBATIONARY_CANONICAL"));
      REQUIRE_THROWS(admit_internet_polynomial_canonical(
          store, policy_candidate_id, "2026-09-09T00:00:01Z"));
      auto denial = store.get(assessment_id);
      source_assessment.blocking_reasons = {"synthetic source denial"};
      denial.payload = statewright::sources::to_json(
          statewright::sources::canonical_policy_assessment(source_assessment));
      static_cast<void>(store.register_record(denial));
      REQUIRE_THROWS(controller.execute_selected_polynomial(observed.updated_candidate, canary_query,
          mpq_class(0), "2026-09-07T00:00:25Z"));
      observation_request.observed_at = "2026-09-07T00:00:25Z";
      observation_request.query_signature = fallback_query;
      REQUIRE_THROWS_WITH(controller.observe(observed.updated_candidate, observation_request),
          "knowledge governance store: SAA-12.1 failure evidence must be successful and non-simulated");
      // The synthetic source denial requires demotion, but cannot back a durable
      // governance failure record. Do not relabel this fixture as real evidence.
      auto denied_observation = observed.observation;
      denied_observation.source_valid = false;
      denied_observation = statewright::saa::make_probation_observation(
          probation.plan, denied_observation.baseline_ref,
          observation_request.query_signature, observation_request.context_signature,
          observation_request.observed_at, observation_request.window_index,
          true, true, true, true, true, false, true,
          observation_request.evidence_ids, observation_request.regression_signals);
      const auto denial_assessment = statewright::saa::assess_probation(
          probation.plan, {observed.observation, denied_observation});
      REQUIRE(denial_assessment.demotion_required);
      REQUIRE_FALSE(denial_assessment.promotion_ready);
      REQUIRE(denial_assessment.regression_reasons == std::vector<std::string>{"SOURCE_RETRACTION"});
      REQUIRE(internet_algorithm_candidate_from_json(
          store.get(observed.updated_candidate_id).payload).status == "PROBATIONARY_CANONICAL");
      REQUIRE_THROWS(controller.execute_selected_polynomial(observed.updated_candidate, canary_query,
          mpq_class(0), "2026-09-07T00:00:26Z"));
      REQUIRE_THROWS(controller.execute_selected_polynomial(probation.updated_candidate, canary_query,
          mpq_class(0), "2026-09-07T00:00:22Z"));
      REQUIRE_THROWS(admit_internet_polynomial_canonical(
          store, policy_candidate_id, "2026-09-07T00:00:22Z"));
      REQUIRE(store.list("internet-polynomial-canonical").size() == 1);
      REQUIRE(store.list("internet-probation-admission").size() == 1);
    } else if (reconciliation_case) {
      REQUIRE_FALSE(store.objects().contains(candidate_id));
      const auto discovery_head = store.event_head();
      const auto recovery = find_internet_polynomial_recovery(store, origin_id, protocol_id);
      REQUIRE(recovery.has_value());
      REQUIRE(recovery->qualification_id == qualification_id);
      REQUIRE(recovery->qualified_candidate.object_id() == candidate_id);
      REQUIRE(store.event_head() == discovery_head);
      InternetImprovementActionLease lease;
      lease.action_key = "synthetic-polynomial-reconciliation";
      lease.run_id = "synthetic-run";
      lease.worker_id = "synthetic-worker";
      lease.acquired_at = "2026-09-07T00:00:00Z";
      lease.expires_at = "2026-09-07T00:01:00Z";
      lease.attempt_number = 1;
      lease.state = "ACTIVE";
      InternetImprovementRunRequest scheduler_request;
      std::string scheduler_run_id;
      if (orchestrator_reconciliation) {
        scheduler_request.cycle_key = "2026-09-07T00:00:00Z";
        scheduler_request.worker_id = lease.worker_id;
        scheduler_request.current_timestamp = "2026-09-07T00:00:30Z";
        scheduler_request.action_lease_expires_at = "2026-09-07T00:02:00Z";
        scheduler_request.fetch_lease_expires_at = "2026-09-07T00:02:00Z";
        scheduler_request.policy.action_deadline = "2026-09-07T00:05:00Z";
        InternetDirectedAction action;
        action.kind = InternetDirectedActionKind::qualify_candidate;
        action.subject_id = origin_id;
        action.subject_type = "internet-algorithm-candidate";
        action.expected_status = "VALIDATION_READY";
        action.input_ids = {origin_id};
        action.protocol_ids = {protocol_id};
        action.not_before = lease.acquired_at;
        action.deadline = scheduler_request.policy.action_deadline;
        action = canonical_internet_directed_action(action);
        InternetImprovementPlan plan;
        plan.cycle_key = scheduler_request.cycle_key;
        plan.baseline_event_head = store.event_head();
        plan.projection_digest = store.projection_checkpoint().authoritative_digest;
        plan.director_policy = to_json(scheduler_request.policy);
        plan.planned_at = lease.acquired_at;
        plan.director_version = "synthetic-director-fixture";
        plan.actions = {action};
        plan = canonical_internet_improvement_plan(plan);
        InternetImprovementStore internet(store);
        const auto plan_id = internet.register_improvement_plan(plan);
        InternetImprovementRun run;
        run.plan_id = plan_id;
        run.worker_id = scheduler_request.worker_id;
        run.started_at = lease.acquired_at;
        run.requested_budgets = to_json(scheduler_request.policy);
        run = canonical_internet_improvement_run(run);
        scheduler_run_id = internet.register_improvement_run(run);
        lease.run_id = scheduler_run_id;
        lease.action_key = action.action_key;
        if (expired_recovery_lease) lease.expires_at = "2026-09-07T00:00:10Z";
      }
      lease = canonical_internet_improvement_action_lease(lease);
      const auto lease_id = register_record("internet-improvement-action-lease", to_json(lease));
      if (competing_successor) {
        auto conflict = candidate;
        conflict.semantic_outputs = {"another semantic contract"};
        conflict = canonical_internet_algorithm_candidate(conflict);
        const auto conflict_id = register_record("internet-algorithm-candidate", to_json(conflict));
        static_cast<void>(store.supersede(origin_id, conflict_id, "synthetic conflict",
                                         "synthetic-test", lease.acquired_at));
        const auto head = store.event_head();
        REQUIRE_THROWS(resume_internet_polynomial_qualification(
            store, origin_id, protocol_id, lease_id, lease.action_key));
        REQUIRE(store.event_head() == head);
        REQUIRE_FALSE(store.objects().contains(candidate_id));
      } else if (orchestrator_reconciliation) {
        InternetImprovementOrchestrator orchestrator(store);
        if (!expired_recovery_lease) {
          auto wrong_worker = scheduler_request;
          wrong_worker.worker_id = "another-worker";
          const auto head = store.event_head();
          REQUIRE_THROWS(orchestrator.resume(scheduler_run_id, wrong_worker));
          REQUIRE(store.event_head() == head);
        }
        const auto evidence_count = store.list("egcf-evidence").size();
        const auto qualification_count = store.list("internet-experiment-qualification").size();
        const auto resumed = orchestrator.resume(scheduler_run_id, scheduler_request);
        REQUIRE(resumed.status == "RECONCILED");
        REQUIRE(resumed.output_ids.size() == 3);
        REQUIRE(resumed.output_ids.front() == qualification_id);
        REQUIRE(store.list("egcf-evidence").size() == evidence_count);
        REQUIRE(store.list("internet-experiment-qualification").size() == qualification_count);
        const auto receipt = internet_improvement_action_receipt_from_json(
            store.get(resumed.action_receipt_id).payload);
        REQUIRE(receipt.disposition == "RECONCILED");
        REQUIRE(receipt.terminal_state == "COMPLETED");
        REQUIRE(load_qualified_internet_polynomial_form(
            store, resumed.output_ids.back()).at("recorded_at") == lease.acquired_at);
        REQUIRE(orchestrator.run_status({}, scheduler_request.worker_id, true).at("runs").empty());
      } else {
        const auto evidence_count = store.list("egcf-evidence").size();
        const auto qualification_count = store.list("internet-experiment-qualification").size();
        const auto outputs = resume_internet_polynomial_qualification(
            store, origin_id, protocol_id, lease_id, lease.action_key);
        REQUIRE(outputs.size() == 3);
        REQUIRE(outputs.front() == qualification_id);
        REQUIRE(store.objects().contains(candidate_id));
        REQUIRE(load_qualified_internet_polynomial_form(store, outputs.at(2)).at("qualification_id") == qualification_id);
        REQUIRE(store.list("egcf-evidence").size() == evidence_count);
        REQUIRE(store.list("internet-experiment-qualification").size() == qualification_count);
        const auto head = store.event_head();
        REQUIRE(resume_internet_polynomial_qualification(
            store, origin_id, protocol_id, lease_id, lease.action_key) == outputs);
        REQUIRE(store.event_head() == head);
        auto other_protocol = protocol;
        other_protocol.valid_from = "2026-09-07T00:02:00Z";
        other_protocol = canonical_internet_experiment_protocol(other_protocol);
        const auto other_protocol_id = register_record("internet-experiment-protocol", to_json(other_protocol));
        REQUIRE_FALSE(find_internet_polynomial_recovery(store, origin_id, other_protocol_id).has_value());
        auto ambiguous = qualification;
        ambiguous.context_signature = std::string(64, 'd');
        ambiguous = canonical_internet_experiment_qualification(ambiguous);
        static_cast<void>(register_record("internet-experiment-qualification", to_json(ambiguous)));
        const auto ambiguous_head = store.event_head();
        REQUIRE_THROWS(find_internet_polynomial_recovery(store, origin_id, protocol_id));
        REQUIRE(store.event_head() == ambiguous_head);
      }
      REQUIRE(store.list("internet-probation-admission").empty());
    } else {
      const auto id = register_qualified_internet_polynomial_form(
          store, candidate_id, qualification_id, "2026-09-07T00:00:00Z");
      const auto loaded = load_qualified_internet_polynomial_form(store, id);
      REQUIRE(loaded.at("form").at("execution_ir") == candidate.proposed_saa_ir);
      const auto head = store.event_head();
      REQUIRE(register_qualified_internet_polynomial_form(
          store, candidate_id, qualification_id, "2026-09-07T00:00:00Z") == id);
      REQUIRE(store.event_head() == head);
      REQUIRE(store.list("internet-probation-admission").empty());
    }
  }
  if (checkpoint_phase != 0) {
    const auto child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
      try {
        EgcfStore store(workspace, resources);
        if (checkpoint_phase == 1) {
          static_cast<void>(register_qualified_internet_polynomial_form(
              store, recovery_candidate_id, recovery_qualification_id, "2026-09-07T00:01:00Z"));
        } else {
          static_cast<void>(checkpoint_qualified_internet_polynomial_candidate(
              store, recovery_candidate_id, recovery_qualification_id, "2026-09-07T00:01:00Z"));
        }
        ::_exit(0); // Deliberately skip store destruction and acknowledgement.
      } catch (...) {
        ::_exit(3);
      }
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    EgcfStore store(workspace, resources);
    const auto before = store.event_head();
    const auto resumed = checkpoint_qualified_internet_polynomial_candidate(
        store, recovery_candidate_id, recovery_qualification_id, "2026-09-07T00:01:00Z");
    const auto after = store.event_head();
    if (checkpoint_phase == 2) REQUIRE(before == after);
    const auto replay = checkpoint_qualified_internet_polynomial_candidate(
        store, recovery_candidate_id, recovery_qualification_id, "2026-09-07T00:01:00Z");
    REQUIRE(replay.candidate_id == resumed.candidate_id);
    REQUIRE(replay.form_id == resumed.form_id);
    REQUIRE(store.event_head() == after);
    const auto linked = checkpoint_qualified_internet_polynomial_candidate(
        store, resumed.candidate_id, recovery_qualification_id, "2026-09-07T00:02:00Z");
    REQUIRE(linked.candidate_id == resumed.candidate_id);
    REQUIRE(linked.form_id == resumed.form_id);
    REQUIRE(store.event_head() == after);
    REQUIRE(store.list("internet-polynomial-qualified-form").size() == 2);
    REQUIRE(store.list("internet-probation-admission").empty());
  }
  std::filesystem::remove_all(workspace);
}

TEST_CASE("internet polynomial candidate checkpoint extension preserves legacy serialization", "[internet][polynomial]") {
  using namespace statewright::egcf;
  InternetAlgorithmCandidate candidate;
  candidate.source_fragment_id = "fixture-fragment";
  candidate.snapshot_id = "fixture-snapshot";
  candidate.source_policy_assessment_id = "fixture-assessment";
  candidate.retrieval_receipt_id = "fixture-retrieval";
  candidate.semantic_inputs = {"x"};
  candidate.semantic_outputs = {"y"};
  candidate.status = "VALIDATION_READY";
  candidate = canonical_internet_algorithm_candidate(candidate);
  const auto old_payload = to_json(candidate);
  const auto old_id = candidate.object_id();
  REQUIRE_FALSE(old_payload.contains("polynomial_form_ids"));
  REQUIRE(internet_algorithm_candidate_from_json(old_payload).object_id() == old_id);
  auto extended = candidate;
  extended.polynomial_form_ids = {"form-b", "form-a", "form-b"};
  extended = canonical_internet_algorithm_candidate(extended);
  const auto payload = to_json(extended);
  REQUIRE(payload.at("polynomial_form_ids") == statewright::contracts::Json::array({"form-a", "form-b"}));
  REQUIRE(extended.object_id() != old_id);
  REQUIRE(internet_algorithm_candidate_from_json(payload).object_id() == extended.object_id());
  auto tampered = payload;
  tampered["polynomial_form_ids"] = statewright::contracts::Json::array({"form-c"});
  REQUIRE_THROWS(internet_algorithm_candidate_from_json(tampered));
  extended.polynomial_form_ids.clear();
  extended = canonical_internet_algorithm_candidate(extended);
  REQUIRE(to_json(extended) == old_payload);
  REQUIRE(extended.object_id() == old_id);
}

#include "statewright/saa/algorithm_ir.hpp"

TEST_CASE("internet polynomial binding accepts coordinator canonical IR envelope", "[internet][polynomial]") {
  using namespace statewright::egcf;
  using statewright::contracts::Json;
  const auto ir = internet_exact_polynomial_ir({1, 0, 1});
  const auto form = make_internet_polynomial_form(ir, "x", "y");
  // Match the actual coordinator's representation, not a raw-IR stand-in.
  const auto canonical_ir = statewright::saa::to_json(
      statewright::saa::canonicalize_mapping(ir));
  REQUIRE(canonical_ir.contains("canonical_payload"));
  REQUIRE(canonical_ir != ir);
  const Json qualification = {
      {"internal_ir_only", true}, {"downloaded_code_executed", false},
      {"identical_frozen_contexts", true}, {"invariants_passed", true},
      {"known_failure_retry_blocked", false}, {"experiment_qualified", true},
      {"benchmark_passed", true}, {"integrity_passed", true},
      {"blocking_reasons", Json::array()}, {"canonical_candidate_ir", canonical_ir},
      {"experiment_design", {{"execution_contract", form.at("execution_contract")}}},
      {"evidence_ids", Json::array({"unregistered-test-placeholder"})}};
  REQUIRE_NOTHROW(verify_internet_polynomial_qualification_form(form, qualification));
  auto changed = qualification;
  changed["canonical_candidate_ir"]["structural_hash"] = std::string(64, '0');
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  changed = qualification;
  changed["canonical_candidate_ir"]["source_node_map"] = Json::array();
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
  changed = qualification;
  changed["canonical_candidate_ir"] = ir;
  REQUIRE_THROWS(verify_internet_polynomial_qualification_form(form, changed));
}

#include "statewright/egcf/internet_polynomial_operation.hpp"

TEST_CASE("internet polynomial operation time follows bounded native lease ancestry", "[internet][polynomial]") {
  using namespace statewright::egcf;
  const auto resources = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources";
  const auto workspace = std::filesystem::temp_directory_path() /
      ("statewright-polynomial-operation-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  {
    EgcfStore store(workspace, resources);
    const auto save = [&](InternetImprovementActionLease lease) {
      lease = canonical_internet_improvement_action_lease(std::move(lease));
      return store.register_record({.object_type = "internet-improvement-action-lease", .payload = to_json(lease)});
    };
    InternetImprovementActionLease root;
    root.action_key = "polynomial-operation-test";
    root.run_id = "synthetic-run-1";
    root.worker_id = "synthetic-worker";
    root.acquired_at = "2026-09-07T00:00:00Z";
    root.expires_at = "2026-09-07T00:01:00Z";
    root.attempt_number = 1;
    root.state = "ACTIVE";
    const auto root_id = save(root);
    auto closed = root;
    closed.state = "EXPIRED";
    closed.predecessor_lease_id = root_id;
    const auto closed_id = save(closed);
    auto retry = root;
    retry.run_id = "synthetic-run-2";
    retry.acquired_at = "2026-09-07T00:02:00Z";
    retry.expires_at = "2026-09-07T00:03:00Z";
    retry.attempt_number = 2;
    retry.predecessor_lease_id = closed_id;
    const auto retry_id = save(retry);
    REQUIRE(internet_polynomial_operation_time(store, root_id, root.action_key) == root.acquired_at);
    REQUIRE(internet_polynomial_operation_time(store, retry_id, root.action_key) == root.acquired_at);
    REQUIRE(internet_polynomial_operation_time(store, retry_id, root.action_key, 3) == root.acquired_at);
    REQUIRE_THROWS(internet_polynomial_operation_time(store, retry_id, root.action_key, 2));
    REQUIRE_THROWS(internet_polynomial_operation_time(store, retry_id, root.action_key, 0));
    REQUIRE_THROWS(internet_polynomial_operation_time(store, retry_id, root.action_key, 129));
    REQUIRE_THROWS(internet_polynomial_operation_time(store, retry_id, "another-action"));
    auto missing = retry;
    missing.predecessor_lease_id.clear();
    REQUIRE_THROWS(internet_polynomial_operation_time(store, save(missing), root.action_key));
    auto reversed = retry;
    reversed.acquired_at = "2026-09-06T00:00:00Z";
    REQUIRE_THROWS(internet_polynomial_operation_time(store, save(reversed), root.action_key));
    auto tampered = store.get(retry_id);
    tampered.payload["lease_signature"] = "tampered";
    const auto tampered_id = store.register_record(tampered);
    REQUIRE_THROWS(internet_polynomial_operation_time(store, tampered_id, root.action_key));
  }
  std::filesystem::remove_all(workspace);
}

#include "statewright/egcf/internet_polynomial_promotion.hpp"

TEST_CASE("internet polynomial promotion references cannot cross candidate contracts", "[internet][polynomial]") {
  using namespace statewright::egcf;
  InternetAlgorithmCandidate candidate;
  candidate.status = "POLICY_QUALIFIED";
  candidate.snapshot_id = "snapshot-A";
  candidate.retrieval_receipt_id = "retrieval-A";
  candidate.experiment_qualification_ids = {"qualification-A"};
  statewright::saa::AutonomousPromotionAssessment assessment;
  assessment.candidate_ref = "qualified-predecessor-A";
  assessment.snapshot_ref = candidate.snapshot_id;
  assessment.retrieval_receipt_ref = candidate.retrieval_receipt_id;
  assessment.experiment_qualification_ref = "qualification-A";
  assessment.promotion_allowed = true;
  // These are reference-only fixtures, not registered policy/review evidence.
  REQUIRE_NOTHROW(verify_internet_polynomial_promotion_references(candidate, assessment));
  auto wrong = assessment;
  wrong.experiment_qualification_ref = "qualification-B";
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, wrong));
  wrong = assessment;
  wrong.snapshot_ref = "snapshot-B";
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, wrong));
  wrong = assessment;
  wrong.retrieval_receipt_ref = "retrieval-B";
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, wrong));
  wrong = assessment;
  wrong.human_approval_required = true;
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, wrong));
  wrong = assessment;
  wrong.promotion_allowed = false;
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, wrong));
  wrong = assessment;
  wrong.blocking_reasons = {"missing independent evidence"};
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, wrong));
  candidate.unresolved_assumptions = {"unresolved source context"};
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, assessment));
  candidate.unresolved_assumptions.clear();
  candidate.status = "EXPERIMENT_QUALIFIED";
  REQUIRE_THROWS(verify_internet_polynomial_promotion_references(candidate, assessment));
}

#include "statewright/egcf/internet_polynomial_canonical.hpp"

TEST_CASE("internet polynomial canonical schema keeps probation mandatory", "[internet][polynomial]") {
  using namespace statewright::egcf;
  using statewright::contracts::Json;
  const auto resources = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources";
  const RecordSchemaRegistry schemas(resources);
  const Json payload = {
      {"schema_version", 1}, {"canonical_version", internet_polynomial_canonical_version},
      {"candidate_id", "synthetic-unregistered-candidate"},
      {"qualified_form_id", "synthetic-unregistered-form"},
      {"promotion_assessment_id", "synthetic-unregistered-assessment"},
      {"form", make_internet_polynomial_form(internet_exact_polynomial_ir({1, 0, 1}), "x", "y")},
      {"generation_basis", "EGCF_OBJECT_COUNT_V1"}, {"native_generation", 1},
      {"created_at", "2026-09-07T00:00:00Z"}, {"probation_required", true}};
  REQUIRE_NOTHROW(schemas.validate_record_payload("internet-polynomial-canonical", payload));
  auto changed = payload;
  changed["probation_required"] = false;
  REQUIRE_THROWS(schemas.validate_record_payload("internet-polynomial-canonical", changed));
  changed = payload;
  changed["native_generation"] = 0;
  REQUIRE_THROWS(schemas.validate_record_payload("internet-polynomial-canonical", changed));
  changed = payload;
  changed["approved"] = true;
  REQUIRE_THROWS(schemas.validate_record_payload("internet-polynomial-canonical", changed));
  const auto workspace = std::filesystem::temp_directory_path() /
      ("statewright-polynomial-canonical-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  {
    EgcfStore store(workspace, resources);
    const auto id = store.register_record({.object_type = "internet-polynomial-canonical", .payload = payload});
    REQUIRE_THROWS(read_internet_polynomial_canonical(store, id));
    REQUIRE_THROWS(admit_internet_polynomial_canonical(store, id, "2026-09-07T00:00:00Z"));
    REQUIRE(store.list("internet-probation-admission").empty());
  }
  std::filesystem::remove_all(workspace);
}
