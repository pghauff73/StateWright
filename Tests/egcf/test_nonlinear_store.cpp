#include "../saa/nonlinear_fixture.hpp"

#include "statewright/common/error.hpp"
#include "statewright/egcf/nonlinear_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {

std::filesystem::path nonlinear_store_temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-nonlinear-store-" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

struct LocalFixture final {
  statewright::saa::GovernedJetEvidence evidence;
  statewright::saa::NonlinearRepresentativeSearch search;
  statewright::saa::CanonicalNonlinearRepresentativeForm local;
};

LocalFixture local_fixture(
    const statewright::saa::CanonicalRepresentativeAlgorithmForm &form,
    std::vector<statewright::saa::NumericCoefficient> center = {"1/2",
                                                                 "1/2"},
    std::vector<statewright::saa::NumericCoefficient> radius = {"1/4",
                                                                 "1/4"}) {
  using namespace statewright;
  const saa::ExactPolynomialSystem system =
      {.input_count = 2U,
       .output_count = 2U,
       .terms = {{.output_index = 0U,
                  .powers = {2, 0},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 1},
                  .coefficient = 1}}};
  auto evidence = saa::acquire_exact_polynomial_jet(
      form, system, std::move(center), std::move(radius), 2U);
  auto search = saa::search_nonlinear_representative_coordinates(
      form, *evidence.jet);
  auto local = saa::canonicalize_nonlinear_representative(form, search);
  return {.evidence = std::move(evidence),
          .search = std::move(search),
          .local = std::move(local)};
}

} // namespace

TEST_CASE("EGCF nonlinear store admits reuses and rebuilds local knowledge") {
  using namespace statewright;
  const auto root = nonlinear_store_temporary_directory();
  {
    egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
    egcf::NonlinearCanonicalStore store(egcf_store);
    const auto form = tests::nonlinear_parent_form("temperature command",
                                                    "pressure command");
    const auto fixture = local_fixture(form);
    const auto first = store.admit_local(fixture.local, fixture.evidence);
    const auto second = store.admit_local(fixture.local, fixture.evidence);

    REQUIRE(first.status == "ADMITTED_NEW_LOCAL_NONLINEAR_FORM");
    REQUIRE(second.status == "REUSED_LOCAL_NONLINEAR_FORM");
    REQUIRE(first.generation == 1);
    REQUIRE(second.generation == first.generation);
    REQUIRE(store.local_signatures() ==
            std::vector<std::string>{
                fixture.local.local_representative_behavior_signature});
    REQUIRE(store.evidence_for_local(
                fixture.local.local_representative_behavior_signature) ==
            std::vector<std::string>{fixture.evidence.evidence_signature});
    REQUIRE(store.list_local().size() == 1U);

    store.rebuild_projection();
    REQUIRE(store.local_signatures().size() == 1U);
    REQUIRE(store.evidence_for_local(
                fixture.local.local_representative_behavior_signature) ==
            std::vector<std::string>{fixture.evidence.evidence_signature});
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF nonlinear store rejects estimated evidence") {
  using namespace statewright;
  const auto root = nonlinear_store_temporary_directory();
  {
    egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
    egcf::NonlinearCanonicalStore store(egcf_store);
    const auto form = tests::nonlinear_parent_form("temperature command",
                                                    "pressure command");
    const auto fixture = local_fixture(form);
    const auto estimated = saa::acquire_bounded_estimated_derivatives(
        form, {"1/2", "1/2"}, {"1/4", "1/4"}, 1U,
        {{.output_index = 0U,
          .powers = {1, 0},
          .lower = "9/10",
          .upper = "11/10"}},
        std::string(64U, 'd'), "human-lab", "bounded-measurement");
    REQUIRE_THROWS_AS(store.admit_local(fixture.local, estimated),
                      common::Error);
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF nonlinear store admits regional stability and rebuilds") {
  using namespace statewright;
  const auto root = nonlinear_store_temporary_directory();
  {
    egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
    egcf::NonlinearCanonicalStore store(egcf_store);
    const auto form = tests::nonlinear_parent_form("temperature command",
                                                    "pressure command");
    const auto left =
        local_fixture(form, {"2/5", "1/2"}, {"1/5", "1/4"});
    const auto right =
        local_fixture(form, {"3/5", "1/2"}, {"1/5", "1/4"});
    const auto left_admission = store.admit_local(left.local, left.evidence);
    const auto right_admission =
        store.admit_local(right.local, right.evidence);
    REQUIRE(left_admission.generation == 1);
    REQUIRE(right_admission.generation == 2);
    const auto assessment = saa::assess_semantic_stability(
        {saa::make_regional_observation(
             left.local, &left.search, left.evidence.evidence_signature),
         saa::make_regional_observation(
             right.local, &right.search, right.evidence.evidence_signature)});
    const auto admission = store.admit_regional_stability(assessment);

    REQUIRE(admission.status == "ADMITTED_REGIONAL_SEMANTIC_ASSESSMENT");
    REQUIRE(admission.generation == 1);
    REQUIRE(store.local_signatures(
                form.representative_behavior_signature)
                .size() == 2U);
    REQUIRE(store.list_regional().size() == 1U);
    store.rebuild_projection();
    REQUIRE(store.local_signatures().size() == 2U);
    REQUIRE(store.list_regional().size() == 1U);
  }
  std::filesystem::remove_all(root);
}
