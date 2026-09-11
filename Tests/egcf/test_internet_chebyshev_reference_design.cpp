#include "statewright/egcf/internet_chebyshev_reference_design.hpp"
#include "statewright/egcf/internet_chebyshev_comparison.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace statewright;

TEST_CASE("internet Chebyshev reference design freezes disjoint bounded cases") {
  const auto ir = egcf::internet_exact_polynomial_ir({-1, 0, 2});
  contracts::Json proposal = {{"source_context", {{"operation", "ChebyshevT"}, {"degree", 2}}},
      {"proposed_saa_ir", ir}, {"candidate_ir_sha256", contracts::sha256_json(ir)}};
  contracts::Json groups = contracts::Json::array({
      {{"name", "anchors"}, {"cases", contracts::Json::array({{{"numerator", 0}, {"denominator", 1}}})}},
      {{"name", "fractions"}, {"cases", contracts::Json::array({{{"numerator", 1}, {"denominator", 2}}})}}});
  const auto make = [&] { return egcf::build_chebyshev_reference_design(
      "synthetic-proposal", std::string(64U, 'a'), proposal, groups); };
  const auto design = make();
  CHECK(design.at("groups")[0].at("expected_outputs") == contracts::Json::array({"-1"}));
  CHECK(design.at("groups")[1].at("expected_outputs") == contracts::Json::array({"-1/2"}));
  CHECK(design.at("candidate_evaluated") == false);
  CHECK(design.at("independence_status") == "NOT_ESTABLISHED");
  CHECK(design.at("qualification_claim") == "NONE");
  SECTION("comparison records actual outputs without qualification") {
    const auto result = egcf::compare_chebyshev_reference_design(
        "synthetic-proposal", std::string(64U, 'a'), proposal, design);
    CHECK(result.at("mismatch_count") == 0);
    CHECK(result.at("case_count") == 2);
    CHECK(result.at("candidate_evaluated") == true);
    CHECK(result.at("qualification_claim") == "NONE");
    CHECK(result.at("groups")[1].at("cases")[0].at("actual") == "-1/2");
  }
  SECTION("altered frozen expectations are rejected") {
    auto altered = design;
    altered["groups"][0]["expected_outputs"][0] = "0";
    REQUIRE_THROWS(egcf::compare_chebyshev_reference_design(
        "synthetic-proposal", std::string(64U, 'a'), proposal, altered));
  }
  SECTION("proposal substitution is rejected") {
    REQUIRE_THROWS(egcf::compare_chebyshev_reference_design(
        "different-proposal", std::string(64U, 'a'), proposal, design));
  }
  SECTION("oversized comparison input is rejected") {
    auto altered = design;
    altered["groups"][0]["inputs"][0] = std::string(100U, '1');
    REQUIRE_THROWS(egcf::compare_chebyshev_reference_design(
        "synthetic-proposal", std::string(64U, 'a'), proposal, altered));
  }
  SECTION("wrong polynomial produces a recorded mismatch") {
    proposal["proposed_saa_ir"] = egcf::internet_exact_polynomial_ir({-1, 0, 3});
    proposal["candidate_ir_sha256"] = contracts::sha256_json(proposal.at("proposed_saa_ir"));
    const auto result = egcf::compare_chebyshev_reference_design(
        "synthetic-proposal", std::string(64U, 'a'), proposal, make());
    CHECK(result.at("status") == "SAMPLED_MISMATCH");
    CHECK(result.at("mismatch_count") == 1);
    CHECK(result.at("qualification_claim") == "NONE");
  }
  SECTION("equivalent rational encodings cannot cross groups") {
    groups[0]["cases"] = contracts::Json::array({{{"numerator", 2}, {"denominator", 4}}});
    REQUIRE_THROWS(make());
  }
  SECTION("out of domain input is rejected") {
    groups[0]["cases"][0]["numerator"] = 2;
    REQUIRE_THROWS(make());
  }
  SECTION("fractional integer fields are rejected") {
    groups[0]["cases"][0]["numerator"] = 0.5;
    REQUIRE_THROWS(make());
  }
  SECTION("duplicate group names are rejected") {
    groups[1]["name"] = "anchors";
    REQUIRE_THROWS(make());
  }
  SECTION("unbound candidate mutation is rejected") {
    proposal["proposed_saa_ir"] = egcf::internet_exact_polynomial_ir({-1, 0, 3});
    REQUIRE_THROWS(make());
  }
  SECTION("expected values do not follow mutated candidate coefficients") {
    proposal["proposed_saa_ir"] = egcf::internet_exact_polynomial_ir({0, 0, 3});
    proposal["candidate_ir_sha256"] = contracts::sha256_json(proposal.at("proposed_saa_ir"));
    CHECK(make().at("groups") == design.at("groups"));
  }
}
