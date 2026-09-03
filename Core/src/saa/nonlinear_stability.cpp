#include "statewright/saa/nonlinear_stability.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <deque>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void stability_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] Json rationals_json(const std::vector<mpq_class> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(rational_json(value));
  }
  return result;
}

[[nodiscard]] Json box_json(const ExactBox &box) {
  Json result = Json::array();
  for (const auto &[lower, upper] : box) {
    result.push_back(Json::array({rational_json(lower), rational_json(upper)}));
  }
  return result;
}

[[nodiscard]] Json boxes_json(const std::vector<ExactBox> &boxes) {
  Json result = Json::array();
  for (const auto &box : boxes) {
    result.push_back(box_json(box));
  }
  return result;
}

[[nodiscard]] Json transform_family_payload(
    const NonlinearRepresentativeSearch *search) {
  Json result = Json::array();
  if (search == nullptr || !search->best_candidate) {
    return result;
  }
  std::vector<std::pair<std::string, Json>> rows;
  for (const auto &transform : search->best_candidate->transforms) {
    Json row = {{"kind", "EXACT_TRIANGULAR_POLYNOMIAL_SHEAR"},
                {"monomial_powers", transform.monomial_powers},
                {"target_input_index", transform.target_input_index}};
    rows.emplace_back(contracts::sha256_json(row), std::move(row));
  }
  std::ranges::sort(rows, {}, &std::pair<std::string, Json>::first);
  for (auto &[signature, row] : rows) {
    static_cast<void>(signature);
    result.push_back(std::move(row));
  }
  return result;
}

[[nodiscard]] bool boxes_overlap(const NonlinearRegionalObservation &left,
                                 const NonlinearRegionalObservation &right) {
  if (left.center().size() != right.center().size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.center().size(); ++index) {
    const mpq_class lower =
        std::max(left.center()[index] - left.validity_radius()[index],
                 right.center()[index] - right.validity_radius()[index]);
    const mpq_class upper =
        std::min(left.center()[index] + left.validity_radius()[index],
                 right.center()[index] + right.validity_radius()[index]);
    if (lower > upper) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool connected(
    const std::vector<std::vector<std::size_t>> &adjacency) {
  if (adjacency.empty()) {
    return false;
  }
  std::set<std::size_t> visited = {0U};
  std::deque<std::size_t> queue = {0U};
  while (!queue.empty()) {
    const std::size_t current = queue.front();
    queue.pop_front();
    for (const auto neighbor : adjacency[current]) {
      if (visited.insert(neighbor).second) {
        queue.push_back(neighbor);
      }
    }
  }
  return visited.size() == adjacency.size();
}

} // namespace

const std::vector<mpq_class> &
NonlinearRegionalObservation::center() const noexcept {
  return local_form.transformed_jet.center;
}

const std::vector<mpq_class> &
NonlinearRegionalObservation::validity_radius() const noexcept {
  return local_form.transformed_jet.validity_radius;
}

NonlinearRegionalObservation make_regional_observation(
    const CanonicalNonlinearRepresentativeForm &local_form,
    const NonlinearRepresentativeSearch *search,
    std::string evidence_signature) {
  if (!local_form.local_canonical_eligible ||
      local_form.global_equivalence_eligible) {
    stability_error("SAA-7.3 requires qualified local-only nonlinear forms");
  }
  if (search != nullptr) {
    if (!search->best_candidate) {
      stability_error(
          "SAA-7.3 search observation has no representative candidate");
    }
    if (search->best_candidate->transformed_jet.local_behavior_signature !=
        local_form.transformed_jet.local_behavior_signature) {
      stability_error(
          "SAA-7.3 search does not correspond to supplied local form");
    }
  }
  const Json family = transform_family_payload(search);
  const std::string signature = contracts::sha256_json(
      {{"schema_version", 1},
       {"stability_version", nonlinear_stability_version},
       {"transform_family", family}});
  const auto first = evidence_signature.find_first_not_of(" \t\n\r");
  const auto last = evidence_signature.find_last_not_of(" \t\n\r");
  evidence_signature = first == std::string::npos
                           ? std::string{}
                           : evidence_signature.substr(first, last - first + 1U);
  return {.local_form = local_form,
          .transform_family_signature = signature,
          .evidence_signature = std::move(evidence_signature)};
}

SemanticStabilityAssessment assess_semantic_stability(
    const std::vector<NonlinearRegionalObservation> &observations) {
  if (observations.empty()) {
    stability_error(
        "SAA-7.3 requires at least one local nonlinear observation");
  }
  if (observations.size() > max_regional_observations) {
    stability_error("SAA-7.3 observation count exceeds bounded cap");
  }
  const std::string parent =
      observations.front().local_form.parent_representative_behavior_signature;
  const std::size_t dimension =
      observations.front().local_form.resolved_input_meanings.size();
  for (const auto &observation : observations) {
    if (observation.local_form.parent_representative_behavior_signature !=
        parent) {
      stability_error(
          "SAA-7.3 cannot compare local forms from different canonical parents");
    }
    if (observation.local_form.resolved_input_meanings.size() != dimension) {
      stability_error("SAA-7.3 semantic dimensions are inconsistent");
    }
    if (observation.center().size() != dimension ||
        observation.validity_radius().size() != dimension) {
      stability_error("SAA-7.3 local box dimension is inconsistent");
    }
  }

  std::vector<std::vector<std::size_t>> adjacency(observations.size());
  for (std::size_t left = 0; left < observations.size(); ++left) {
    for (std::size_t right = left + 1U; right < observations.size(); ++right) {
      if (boxes_overlap(observations[left], observations[right])) {
        adjacency[left].push_back(right);
        adjacency[right].push_back(left);
      }
    }
  }
  const bool is_connected = connected(adjacency);

  const auto stable_meanings =
      observations.front().local_form.resolved_input_meanings;
  std::vector<std::size_t> transition_coordinates;
  for (std::size_t coordinate = 0; coordinate < dimension; ++coordinate) {
    std::set<std::string> meanings;
    for (const auto &observation : observations) {
      meanings.insert(
          observation.local_form.resolved_input_meanings[coordinate]);
    }
    if (meanings.size() > 1U) {
      transition_coordinates.push_back(coordinate);
    }
  }
  const bool meanings_stable = transition_coordinates.empty();

  std::vector<std::string> family_signatures;
  std::set<std::string> family_set;
  std::vector<std::string> local_signatures;
  std::vector<std::string> evidence_signatures;
  std::vector<ExactBox> boxes;
  for (const auto &observation : observations) {
    family_signatures.push_back(observation.transform_family_signature);
    family_set.insert(observation.transform_family_signature);
    local_signatures.push_back(
        observation.local_form.local_representative_behavior_signature);
    if (!observation.evidence_signature.empty()) {
      evidence_signatures.push_back(observation.evidence_signature);
    }
    ExactBox box;
    for (std::size_t index = 0; index < dimension; ++index) {
      box.emplace_back(observation.center()[index] -
                           observation.validity_radius()[index],
                       observation.center()[index] +
                           observation.validity_radius()[index]);
    }
    boxes.push_back(std::move(box));
  }
  const bool family_stable = family_set.size() == 1U;

  std::string status;
  bool eligible = false;
  if (observations.size() == 1U) {
    status = "LOCALLY_STABLE_SEMANTICS";
  } else if (!is_connected) {
    status = "MULTI_REGION_SEMANTICS_UNRESOLVED";
  } else if (!meanings_stable) {
    status = "SEMANTIC_TRANSITION_DETECTED";
  } else if (!family_stable) {
    status = "REPRESENTATION_REGIME_CHANGE";
  } else {
    status = "REGIONALLY_STABLE_SEMANTICS";
    eligible = true;
  }

  const std::vector<std::string> admitted_meanings =
      meanings_stable ? stable_meanings : std::vector<std::string>{};
  const Json payload =
      {{"adjacency", adjacency},
       {"connected_region", is_connected},
       {"evidence_signatures", evidence_signatures},
       {"local_behavior_signatures", local_signatures},
       {"meanings_stable", meanings_stable},
       {"observation_count", observations.size()},
       {"parent_representative_behavior_signature", parent},
       {"regional_boxes", boxes_json(boxes)},
       {"regional_semantic_eligible", eligible},
       {"representation_family_stable", family_stable},
       {"schema_version", 1},
       {"stability_version", nonlinear_stability_version},
       {"stable_meanings", admitted_meanings},
       {"status", status},
       {"transform_family_signatures", family_signatures},
       {"transition_coordinates", transition_coordinates}};
  std::vector<std::string> warnings;
  if (status == "LOCALLY_STABLE_SEMANTICS") {
    warnings.push_back(
        "One local observation cannot establish semantic stability across an operating region.");
  }
  if (status == "MULTI_REGION_SEMANTICS_UNRESOLVED") {
    warnings.push_back(
        "Local semantic islands are disconnected; no continuous regional semantic claim is admitted.");
  }
  if (status == "REPRESENTATION_REGIME_CHANGE") {
    warnings.push_back(
        "Meaning labels agree but the required representative transform family changes across the region.");
  }
  if (eligible) {
    warnings.push_back(
        "Regional stability is limited to the connected union of the qualified local boxes and does not prove global semantic invariance.");
  }
  return {.schema_version = 1,
          .stability_version = std::string(nonlinear_stability_version),
          .parent_representative_behavior_signature = parent,
          .status = std::move(status),
          .observation_count = observations.size(),
          .connected_region = is_connected,
          .meanings_stable = meanings_stable,
          .representation_family_stable = family_stable,
          .stable_meanings = admitted_meanings,
          .transition_coordinates = std::move(transition_coordinates),
          .adjacency = std::move(adjacency),
          .regional_boxes = std::move(boxes),
          .local_behavior_signatures = std::move(local_signatures),
          .transform_family_signatures = std::move(family_signatures),
          .evidence_signatures = std::move(evidence_signatures),
          .regional_semantic_eligible = eligible,
          .assessment_signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

Json to_json(const NonlinearRegionalObservation &value) {
  return {{"center", rationals_json(value.center())},
          {"evidence_signature", value.evidence_signature},
          {"local_representative_behavior_signature",
           value.local_form.local_representative_behavior_signature},
          {"resolved_input_meanings",
           value.local_form.resolved_input_meanings},
          {"semantic_signature", value.local_form.semantic_signature},
          {"transform_family_signature", value.transform_family_signature},
          {"validity_radius", rationals_json(value.validity_radius())}};
}

Json to_json(const SemanticStabilityAssessment &value) {
  return {{"adjacency", value.adjacency},
          {"assessment_signature", value.assessment_signature},
          {"connected_region", value.connected_region},
          {"evidence_signatures", value.evidence_signatures},
          {"local_behavior_signatures", value.local_behavior_signatures},
          {"meanings_stable", value.meanings_stable},
          {"observation_count", value.observation_count},
          {"parent_representative_behavior_signature",
           value.parent_representative_behavior_signature},
          {"regional_boxes", boxes_json(value.regional_boxes)},
          {"regional_semantic_eligible", value.regional_semantic_eligible},
          {"representation_family_stable",
           value.representation_family_stable},
          {"schema_version", value.schema_version},
          {"stability_version", value.stability_version},
          {"stable_meanings", value.stable_meanings},
          {"status", value.status},
          {"transform_family_signatures", value.transform_family_signatures},
          {"transition_coordinates", value.transition_coordinates},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
