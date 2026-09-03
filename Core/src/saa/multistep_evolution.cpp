#include "statewright/saa/multistep_evolution.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <ranges>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void evolution_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string canonical_text(std::string value) {
  std::string result;
  bool pending_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      result.push_back(' ');
      pending_space = false;
    }
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] std::vector<std::string>
canonical_texts(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = canonical_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::vector<std::string>
canonical_dimensions(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = uppercase(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] Json boolean_values_json(
    const std::vector<std::pair<std::string, bool>> &values) {
  Json result = Json::object();
  for (const auto &[name, value] : values) {
    result[name] = value;
  }
  return result;
}

[[nodiscard]] std::pair<std::vector<std::string>, std::vector<std::string>>
grounded_evidence(const ReasoningEvidenceResolver &resolver,
                  std::vector<std::string> evidence_ids) {
  std::set<std::string> canonical_ids;
  for (auto &evidence_id : evidence_ids) {
    evidence_id = trimmed(std::move(evidence_id));
    if (!evidence_id.empty()) {
      canonical_ids.insert(std::move(evidence_id));
    }
  }
  std::vector<std::string> grounded;
  std::set<std::string> groups;
  for (const auto &evidence_id : canonical_ids) {
    std::optional<ReasoningGroundingEvidence> record;
    try {
      record = resolver(evidence_id);
    } catch (...) {
      record = std::nullopt;
    }
    if (!record) {
      evolution_error("SAA-11.3 evolution evidence is not registered: " +
                      evidence_id);
    }
    if (record->object_type != "egcf-evidence") {
      evolution_error(
          "SAA-11.3 evolution evidence must reference EvidenceArtifact");
    }
    if (record->success != true || record->simulated) {
      evolution_error("SAA-11.3 evolution evidence must be successful and "
                      "non-simulated");
    }
    if ((!record->producer.starts_with("deterministic-") &&
         !record->producer.starts_with("human-")) ||
        record->method == "reported") {
      evolution_error("SAA-11.3 evolution evidence must be "
                      "deterministic/human grounded");
    }
    grounded.push_back(evidence_id);
    if (!record->independence_group.empty()) {
      groups.insert(record->independence_group);
    }
  }
  if (grounded.empty()) {
    evolution_error(
        "SAA-11.3 evolution qualification requires grounded evidence");
  }
  if (groups.empty()) {
    evolution_error(
        "SAA-11.3 evolution evidence requires an independence group");
  }
  return {std::move(grounded),
          std::vector<std::string>(groups.begin(), groups.end())};
}

} // namespace

MultiStepEvolutionPlan make_multistep_evolution_plan(
    const AdaptationLineageCatalog &lineage_catalog,
    std::string final_candidate_ref, std::vector<std::string> frozen_invariants,
    std::vector<std::string> allowed_dimensions, std::size_t max_steps) {
  if (max_steps < 1U || max_steps > max_evolution_steps) {
    evolution_error("SAA-11.3 max evolution steps outside supported range");
  }
  final_candidate_ref = trimmed(std::move(final_candidate_ref));
  static_cast<void>(lineage_catalog.get_candidate(final_candidate_ref));
  const auto ancestors = lineage_catalog.ancestors(final_candidate_ref);
  std::vector<std::string> candidate_ancestors;
  for (const auto &reference : ancestors) {
    if (reference.starts_with("adapted-candidate:sha256:")) {
      candidate_ancestors.push_back(reference);
    }
  }
  std::ranges::reverse(candidate_ancestors);
  candidate_ancestors.push_back(final_candidate_ref);
  if (candidate_ancestors.size() > max_steps) {
    evolution_error("SAA-11.3 evolution path exceeds bounded step count");
  }
  std::string root_ref;
  for (auto iterator = ancestors.rbegin(); iterator != ancestors.rend();
       ++iterator) {
    if (!iterator->starts_with("adapted-candidate:sha256:")) {
      root_ref = *iterator;
      break;
    }
  }
  if (root_ref.empty()) {
    evolution_error("SAA-11.3 evolution path has no canonical root");
  }
  frozen_invariants = canonical_texts(std::move(frozen_invariants));
  if (frozen_invariants.empty()) {
    evolution_error("SAA-11.3 requires at least one frozen invariant");
  }
  allowed_dimensions =
      canonical_dimensions(std::move(allowed_dimensions));
  if (std::ranges::any_of(allowed_dimensions, [](const auto &dimension) {
        return !allowed_adaptation_dimension(dimension);
      })) {
    evolution_error("SAA-11.3 allowed dimensions contain an unsupported "
                    "adaptation dimension");
  }
  std::map<std::string, Json> edges_by_child;
  for (const auto &edge : lineage_catalog.lineage_edges()) {
    edges_by_child[edge.at("child_ref").get<std::string>()] = edge;
  }
  std::vector<EvolutionStepDescriptor> steps;
  std::string previous = root_ref;
  for (std::size_t index = 0; index < candidate_ancestors.size(); ++index) {
    const auto &candidate_ref = candidate_ancestors[index];
    const Json envelope = lineage_catalog.get_candidate(candidate_ref);
    const Json payload = envelope.at("payload");
    const std::string dimension =
        uppercase(payload.at("changed_dimension").get<std::string>());
    if (!allowed_adaptation_dimension(dimension)) {
      evolution_error(
          "SAA-11.3 stored candidate uses unsupported changed dimension");
    }
    if (!allowed_dimensions.empty() &&
        !std::ranges::binary_search(allowed_dimensions, dimension)) {
      evolution_error("SAA-11.3 candidate changes frozen-out dimension: " +
                      dimension);
    }
    const auto edge = edges_by_child.find(candidate_ref);
    if (edge == edges_by_child.end() ||
        edge->second.at("parent_ref") != previous) {
      evolution_error("SAA-11.3 lineage path is discontinuous");
    }
    const Json edge_payload = edge->second.at("payload");
    steps.push_back(
        {.index = static_cast<int>(index),
         .parent_ref = previous,
         .candidate_ref = candidate_ref,
         .changed_dimension = dimension,
         .edge_signature =
             edge_payload.at("edge_signature").get<std::string>(),
         .candidate_signature =
             payload.at("candidate_signature").get<std::string>()});
    previous = candidate_ref;
  }
  Json step_payload = Json::array();
  for (const auto &step : steps) {
    step_payload.push_back(to_json(step));
  }
  const Json material =
      {{"allowed_dimensions", allowed_dimensions},
       {"final_candidate_ref", final_candidate_ref},
       {"frozen_invariants", frozen_invariants},
       {"policy", "EACH_INTERMEDIATE_STEP_REQUALIFIES_FROZEN_INVARIANTS"},
       {"root_algorithm_ref", root_ref},
       {"steps", step_payload},
       {"version", multistep_evolution_version}};
  return {.schema_version = 1,
          .evolution_version = std::string(multistep_evolution_version),
          .root_algorithm_ref = std::move(root_ref),
          .final_candidate_ref = std::move(final_candidate_ref),
          .frozen_invariants = std::move(frozen_invariants),
          .allowed_dimensions = std::move(allowed_dimensions),
          .steps = std::move(steps),
          .one_dimension_per_step = true,
          .plan_signature = contracts::sha256_json(material)};
}

EvolutionStepQualification qualify_evolution_step(
    const ReasoningEvidenceResolver &evidence_resolver,
    const MultiStepEvolutionPlan &plan, std::string candidate_ref,
    std::vector<std::pair<std::string, bool>> invariant_results,
    std::vector<std::string> evidence_ids, bool independent_review) {
  candidate_ref = trimmed(std::move(candidate_ref));
  const auto descriptor = std::ranges::find_if(
      plan.steps, [&](const auto &step) {
        return step.candidate_ref == candidate_ref;
      });
  if (descriptor == plan.steps.end()) {
    evolution_error(
        "SAA-11.3 candidate is not part of the evolution plan");
  }
  std::map<std::string, bool> supplied;
  for (auto &[name, value] : invariant_results) {
    name = canonical_text(std::move(name));
    if (!supplied.emplace(std::move(name), value).second) {
      evolution_error("SAA-11.3 duplicate invariant result");
    }
  }
  std::set<std::string> supplied_names;
  for (const auto &[name, value] : supplied) {
    static_cast<void>(value);
    supplied_names.insert(name);
  }
  const std::set<std::string> frozen(plan.frozen_invariants.begin(),
                                      plan.frozen_invariants.end());
  if (supplied_names != frozen) {
    evolution_error("SAA-11.3 step must report every frozen invariant and no "
                    "extras");
  }
  auto [grounded, groups] =
      grounded_evidence(evidence_resolver, std::move(evidence_ids));
  const bool invariant_gate =
      std::ranges::all_of(plan.frozen_invariants,
                          [&](const auto &name) { return supplied.at(name); });
  std::string status;
  if (!invariant_gate) {
    status = "EVOLUTION_STEP_INVARIANT_VIOLATION";
  } else if (!independent_review) {
    status = "EVOLUTION_STEP_REVIEW_REQUIRED";
  } else {
    status = "EVOLUTION_STEP_QUALIFIED";
  }
  const std::vector<std::pair<std::string, bool>> canonical_results(
      supplied.begin(), supplied.end());
  const Json payload =
      {{"candidate_ref", candidate_ref},
       {"changed_dimension", descriptor->changed_dimension},
       {"grounded_evidence_ids", grounded},
       {"independence_groups", groups},
       {"independent_review", independent_review},
       {"invariant_results", boolean_values_json(canonical_results)},
       {"plan_signature", plan.plan_signature},
       {"status", status},
       {"version", multistep_evolution_version}};
  return {.schema_version = 1,
          .evolution_version = std::string(multistep_evolution_version),
          .plan_signature = plan.plan_signature,
          .candidate_ref = std::move(candidate_ref),
          .changed_dimension = descriptor->changed_dimension,
          .invariant_results = canonical_results,
          .grounded_evidence_ids = std::move(grounded),
          .independence_groups = std::move(groups),
          .independent_review = independent_review,
          .status = status,
          .step_qualified = status == "EVOLUTION_STEP_QUALIFIED",
          .qualification_signature = contracts::sha256_json(payload)};
}

MultiStepEvolutionAssessment assess_multistep_evolution(
    const MultiStepEvolutionPlan &plan,
    const std::vector<EvolutionStepQualification> &qualifications) {
  std::map<std::string, const EvolutionStepQualification *> by_ref;
  for (const auto &qualification : qualifications) {
    if (qualification.plan_signature != plan.plan_signature) {
      evolution_error(
          "SAA-11.3 step qualification belongs to a different plan");
    }
    if (!by_ref.emplace(qualification.candidate_ref, &qualification).second) {
      evolution_error("SAA-11.3 duplicate step qualification");
    }
  }
  std::vector<std::string> blocking;
  int qualified = 0;
  for (const auto &step : plan.steps) {
    const auto found = by_ref.find(step.candidate_ref);
    if (found == by_ref.end()) {
      blocking.push_back(step.candidate_ref +
                         ": MISSING_STEP_QUALIFICATION");
    } else if (!found->second->step_qualified) {
      blocking.push_back(step.candidate_ref + ": " +
                         found->second->status);
    } else {
      ++qualified;
    }
  }
  std::vector<std::string> qualification_signatures;
  for (const auto &qualification : qualifications) {
    qualification_signatures.push_back(qualification.qualification_signature);
  }
  std::ranges::sort(qualification_signatures);
  const bool complete =
      qualified == static_cast<int>(plan.steps.size()) && blocking.empty();
  const std::string status = complete ? "MULTISTEP_EVOLUTION_QUALIFIED"
                                      : "MULTISTEP_EVOLUTION_BLOCKED";
  const Json payload =
      {{"blocking_steps", blocking},
       {"final_candidate_ref", plan.final_candidate_ref},
       {"plan_signature", plan.plan_signature},
       {"qualification_signatures", qualification_signatures},
       {"qualified_step_count", qualified},
       {"status", status},
       {"total_step_count", plan.steps.size()},
       {"version", multistep_evolution_version}};
  return {.schema_version = 1,
          .evolution_version = std::string(multistep_evolution_version),
          .plan_signature = plan.plan_signature,
          .final_candidate_ref = plan.final_candidate_ref,
          .qualification_signatures = std::move(qualification_signatures),
          .qualified_step_count = qualified,
          .total_step_count = static_cast<int>(plan.steps.size()),
          .invariant_preservation_complete = complete,
          .status = status,
          .evolution_qualified = complete,
          .blocking_steps = std::move(blocking),
          .assessment_signature = contracts::sha256_json(payload)};
}

Json to_json(const EvolutionStepDescriptor &value) {
  return {{"candidate_ref", value.candidate_ref},
          {"candidate_signature", value.candidate_signature},
          {"changed_dimension", value.changed_dimension},
          {"edge_signature", value.edge_signature},
          {"index", value.index},
          {"parent_ref", value.parent_ref}};
}

Json to_json(const MultiStepEvolutionPlan &value) {
  Json steps = Json::array();
  for (const auto &step : value.steps) {
    steps.push_back(to_json(step));
  }
  return {{"allowed_dimensions", value.allowed_dimensions},
          {"evolution_version", value.evolution_version},
          {"final_candidate_ref", value.final_candidate_ref},
          {"frozen_invariants", value.frozen_invariants},
          {"one_dimension_per_step", value.one_dimension_per_step},
          {"plan_signature", value.plan_signature},
          {"root_algorithm_ref", value.root_algorithm_ref},
          {"schema_version", value.schema_version},
          {"steps", steps}};
}

Json to_json(const EvolutionStepQualification &value) {
  return {{"candidate_ref", value.candidate_ref},
          {"changed_dimension", value.changed_dimension},
          {"evolution_version", value.evolution_version},
          {"grounded_evidence_ids", value.grounded_evidence_ids},
          {"independence_groups", value.independence_groups},
          {"independent_review", value.independent_review},
          {"invariant_results", boolean_values_json(value.invariant_results)},
          {"plan_signature", value.plan_signature},
          {"qualification_signature", value.qualification_signature},
          {"schema_version", value.schema_version},
          {"status", value.status},
          {"step_qualified", value.step_qualified}};
}

Json to_json(const MultiStepEvolutionAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"blocking_steps", value.blocking_steps},
          {"evolution_qualified", value.evolution_qualified},
          {"evolution_version", value.evolution_version},
          {"final_candidate_ref", value.final_candidate_ref},
          {"invariant_preservation_complete",
           value.invariant_preservation_complete},
          {"plan_signature", value.plan_signature},
          {"qualification_signatures", value.qualification_signatures},
          {"qualified_step_count", value.qualified_step_count},
          {"schema_version", value.schema_version},
          {"status", value.status},
          {"total_step_count", value.total_step_count}};
}

} // namespace statewright::saa
