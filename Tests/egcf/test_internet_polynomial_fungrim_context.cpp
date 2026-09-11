#include "statewright/egcf/internet_polynomial_fungrim_context.hpp"
#include "statewright/egcf/internet_context_resolution.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace statewright;

TEST_CASE("internet Fungrim context inspection preserves review requirements") {
  // Synthetic syntax fixture only: rows are not claimed Chebyshev values.
  std::string entry = "make_entry(ID(\"85e42e\"), Description(\"Table of\", ChebyshevT(n,x), \"for\", LessEqual(0,n,15)),"
      "Table(TableRelation(Tuple(n,p),Equal(ChebyshevT(n,x),p)),"
      "TableHeadings(n,ChebyshevT(n,x)),TableSplit(1),List(";
  for (unsigned i = 0; i <= 15; ++i) {
    entry += "Tuple(" + std::to_string(i) + "," +
        (i == 0 ? std::string("1") : i == 2 ? std::string("2*x**2-1") : "x**" + std::to_string(i)) + ")";
    if (i != 15) entry += ",";
  }
  entry += ")),Variables(x),Assumptions(Element(x,CC)))";
  contracts::Json fragment = {{"fragment_kind", "ALGORITHM_DESCRIPTION"},
      {"text", entry}, {"snapshot_id", "synthetic-source"},
      {"fragment_signature", "synthetic-signature"}};
  const auto found = egcf::inspect_fungrim_chebyshev_quadratic_context(fragment);
  REQUIRE(found.has_value());
  CHECK(found->at("coefficients") == contracts::Json::array({"-1", "0", "2"}));
  CHECK(found->at("entry_text") == entry);
  CHECK(found->at("mathematical_review_required") == true);
  CHECK(found->at("qualification_claim") == "NONE");
  CHECK(found->at("source_executed") == false);
  SECTION("translation produces native execution without review or registration claims") {
    const auto proposal = egcf::preview_fungrim_chebyshev_quadratic_translation(fragment);
    REQUIRE(proposal.has_value());
    CHECK(proposal->at("source_context") == *found);
    CHECK(proposal->at("candidate_registered") == false);
    CHECK(proposal->at("admission") == false);
    CHECK(proposal->at("qualification_claim") == "NONE");
    CHECK(proposal->at("required_reviews").size() == 2U);
    const auto program = egcf::internet_exact_polynomial_program(proposal->at("proposed_saa_ir"));
    CHECK(program.coefficients == std::vector<mpq_class>{-1, 0, 2});
    CHECK(proposal->at("execution_contract") == egcf::internet_polynomial_contract(program));
    statewright::sources::InternetSourceFragment source_fragment;
    source_fragment.snapshot_id = "internet-source-snapshot:sha256:fixture";
    source_fragment.fragment_kind = "ALGORITHM_DESCRIPTION";
    source_fragment.byte_start = 0;
    source_fragment.byte_end = entry.size();
    source_fragment.selector = "fixture";
    source_fragment.text = entry;
    source_fragment.language = "python";
    source_fragment.metadata = contracts::Json::object();
    source_fragment =
        statewright::sources::canonical_source_fragment(std::move(source_fragment));
    auto candidate = egcf::fungrim_quadratic_candidate_template(source_fragment);
    candidate.source_policy_assessment_id =
        "internet-policy-assessment:sha256:fixture";
    candidate.retrieval_receipt_id =
        "internet-retrieval-receipt:sha256:fixture";
    candidate.unresolved_assumptions = {
        "DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED",
        "MATHEMATICAL_CONTEXT_REVIEW_REQUIRED",
        "REASONING_CONTEXT_CAPACITY_UNSUPPORTED"};
    candidate.status = "QUARANTINED";
    candidate =
        egcf::canonical_internet_algorithm_candidate(std::move(candidate));
    const contracts::Json snapshot = {
        {"body_sha256", contracts::sha256_text(entry)},
        {"body_size", entry.size()},
        {"final_url", "https://fungrim.example/chebyshev"}};
    const auto resolution =
        egcf::make_fungrim_chebyshev_quadratic_context_resolution(
            candidate, source_fragment, snapshot);
    CHECK(resolution.status == "CONTEXT_RESOLUTION_COMPLETE");
    CHECK(resolution.bundle_bytes <= resolution.reasoning_limit_bytes);
    CHECK(resolution.mathematical_context_review_status ==
          "MATHEMATICAL_CONTEXT_REVIEW_PASSED");
    CHECK(resolution.domain_branch_error_bound_status ==
          "DOMAIN_BRANCH_AND_ERROR_BOUNDS_QUALIFIED");
    CHECK(resolution.missing_items.empty());
    CHECK(resolution.conflicts.empty());
    CHECK(resolution.source_bundle.size() == 4U);
    const auto too_small =
        egcf::make_fungrim_chebyshev_quadratic_context_resolution(
            candidate, source_fragment, snapshot, 16U);
    CHECK(too_small.status == "CONTEXT_RESOLUTION_INCOMPLETE");
    CHECK(too_small.missing_items == std::vector<std::string>{
        "REASONING_CONTEXT_CAPACITY_UNSUPPORTED"});
    const auto blocked =
        egcf::candidate_after_context_resolution(candidate, too_small);
    CHECK(blocked.status == "QUARANTINED");
    CHECK(blocked.context_resolution_ids.empty());
    CHECK(blocked.unresolved_assumptions == std::vector<std::string>{
        "DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED",
        "MATHEMATICAL_CONTEXT_REVIEW_REQUIRED",
        "REASONING_CONTEXT_CAPACITY_UNSUPPORTED"});
    const auto advanced =
        egcf::candidate_after_context_resolution(candidate, resolution);
    CHECK(advanced.status == "VALIDATION_READY");
    CHECK(advanced.unresolved_assumptions.empty());
    CHECK(advanced.context_resolution_ids == std::vector<std::string>{
        resolution.object_id()});
  }
  SECTION("duplicate selected entries are ambiguous") {
    fragment["text"] = entry + "\n" + entry;
    CHECK_FALSE(egcf::inspect_fungrim_chebyshev_quadratic_context(fragment));
  }
  SECTION("whole source bytes must match the recorded snapshot") {
    fragment["byte_start"] = 0;
    fragment["byte_end"] = entry.size();
    contracts::Json snapshot = {{"body_size", entry.size()},
        {"body_sha256", contracts::sha256_text(entry)}};
    const auto bound = egcf::bind_fungrim_quadratic_translation_to_snapshot(fragment, snapshot);
    REQUIRE(bound.has_value());
    CHECK(bound->at("source_body_sha256") == snapshot.at("body_sha256"));
    CHECK(bound->at("candidate_registered") == false);
    snapshot["body_sha256"] = std::string(64U, '0');
    CHECK_FALSE(egcf::bind_fungrim_quadratic_translation_to_snapshot(fragment, snapshot));
    snapshot["body_sha256"] = contracts::sha256_text(entry);
    fragment["byte_start"] = 1;
    CHECK_FALSE(egcf::bind_fungrim_quadratic_translation_to_snapshot(fragment, snapshot));
    fragment["byte_start"] = 0;
    fragment["byte_end"] = entry.size() - 1U;
    CHECK_FALSE(egcf::bind_fungrim_quadratic_translation_to_snapshot(fragment, snapshot));
    fragment["byte_end"] = entry.size();
    snapshot["body_size"] = entry.size() + 1U;
    CHECK_FALSE(egcf::bind_fungrim_quadratic_translation_to_snapshot(fragment, snapshot));
  }
  SECTION("quoted entries are not top-level data declarations") {
    fragment["text"] = "\"\"\"\n" + entry + "\n\"\"\"";
    CHECK_FALSE(egcf::inspect_fungrim_chebyshev_quadratic_context(fragment));
  }
  SECTION("truncated context is rejected") {
    fragment["text"] = entry.substr(0, entry.size() - 1U);
    CHECK_FALSE(egcf::inspect_fungrim_chebyshev_quadratic_context(fragment));
  }
  SECTION("changed assumptions are unsupported") {
    auto changed = entry;
    changed.replace(changed.find("Element(x,CC)"), 13U, "Element(x,ZZ)");
    fragment["text"] = changed;
    CHECK_FALSE(egcf::inspect_fungrim_chebyshev_quadratic_context(fragment));
  }
  SECTION("source size is bounded") {
    fragment["text"] = std::string(65537U, ' ');
    CHECK_FALSE(egcf::inspect_fungrim_chebyshev_quadratic_context(fragment));
  }
}
