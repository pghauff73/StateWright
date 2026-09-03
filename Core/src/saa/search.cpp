#include "statewright/saa/search.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void search_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] std::string canonical_text(std::string value) {
  std::string output;
  bool pending_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !output.empty();
      continue;
    }
    if (pending_space) {
      output.push_back(' ');
      pending_space = false;
    }
    output.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return output;
}

[[nodiscard]] std::vector<std::string>
canonical_texts(std::vector<std::string> values) {
  std::set<std::string> unique;
  for (auto &value : values) {
    auto normalized = canonical_text(std::move(value));
    if (!normalized.empty()) {
      unique.insert(std::move(normalized));
    }
  }
  return {unique.begin(), unique.end()};
}

[[nodiscard]] std::vector<std::string>
canonical_ids(std::vector<std::string> values) {
  values.erase(std::remove(values.begin(), values.end(), ""), values.end());
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

[[nodiscard]] std::set<std::string>
algorithm_primitives(const CanonicalAlgorithmIR &ir) {
  std::set<std::string> result;
  const auto iterator = ir.canonical_payload.find("nodes");
  if (iterator == ir.canonical_payload.end() || !iterator->is_array()) {
    search_error("canonical algorithm IR lacks node payload");
  }
  for (const auto &node : *iterator) {
    if (!node.is_object() || !node.contains("primitive") ||
        !node.at("primitive").is_string()) {
      search_error("canonical algorithm IR contains malformed node payload");
    }
    result.insert(node.at("primitive").get<std::string>());
  }
  return result;
}

[[nodiscard]] bool exact_hash(std::string_view value) {
  return value.size() == 64U &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isdigit(character) != 0 ||
                  (character >= 'a' && character <= 'f');
         });
}

void finalize_algorithm(SearchableAlgorithm &algorithm) {
  if (!exact_hash(algorithm.structural_ir.structural_hash)) {
    search_error("searchable algorithm requires an exact structural hash");
  }
  algorithm.domain = canonical_text(std::move(algorithm.domain));
  if (algorithm.domain.empty()) {
    search_error("searchable algorithm domain must be non-empty");
  }
  algorithm.semantic_terms =
      canonical_texts(std::move(algorithm.semantic_terms));
  if (algorithm.semantic_terms.empty()) {
    search_error("searchable algorithm requires semantic terms");
  }
  algorithm.invariants = canonical_texts(std::move(algorithm.invariants));
  algorithm.evidence_ids = canonical_ids(std::move(algorithm.evidence_ids));
  if (algorithm.qualification_status != "PROPOSED" &&
      algorithm.qualification_status != "CANDIDATE" &&
      algorithm.qualification_status != "QUALIFIED" &&
      algorithm.qualification_status != "RETIRED") {
    search_error("searchable algorithm qualification status is invalid");
  }
  algorithm.semantic_signature = contracts::sha256_json(
      {{"domain", algorithm.domain},
       {"invariants", algorithm.invariants},
       {"semantic_terms", algorithm.semantic_terms}});
  const std::string expected_id = contracts::typed_id(
      "canonical-algorithm",
      {{"semantic_signature", algorithm.semantic_signature},
       {"structural_hash", algorithm.structural_ir.structural_hash}});
  if (!algorithm.canonical_algorithm_id.empty() &&
      algorithm.canonical_algorithm_id != expected_id) {
    search_error("searchable algorithm ID mismatch");
  }
  algorithm.canonical_algorithm_id = expected_id;
  auto material = to_json(algorithm);
  material.erase("signature");
  const std::string expected_signature = contracts::sha256_json(material);
  if (!algorithm.signature.empty() && algorithm.signature != expected_signature) {
    search_error("searchable algorithm signature mismatch");
  }
  algorithm.signature = expected_signature;
}

[[nodiscard]] std::vector<std::string>
missing_values(const std::vector<std::string> &required,
               const std::set<std::string> &available) {
  std::vector<std::string> missing;
  for (const auto &value : required) {
    if (!available.contains(value)) {
      missing.push_back(value);
    }
  }
  return missing;
}

} // namespace

SearchableAlgorithm make_searchable_algorithm(
    CanonicalAlgorithmIR structural_ir, std::string domain,
    std::vector<std::string> semantic_terms,
    std::vector<std::string> invariants, std::vector<std::string> evidence_ids,
    std::string qualification_status, std::string source_snapshot_hash) {
  SearchableAlgorithm algorithm{
      .canonical_algorithm_id = {},
      .structural_ir = std::move(structural_ir),
      .domain = std::move(domain),
      .semantic_terms = std::move(semantic_terms),
      .invariants = std::move(invariants),
      .evidence_ids = std::move(evidence_ids),
      .qualification_status = std::move(qualification_status),
      .source_snapshot_hash = std::move(source_snapshot_hash),
      .semantic_signature = {},
      .signature = {}};
  finalize_algorithm(algorithm);
  return algorithm;
}

Json to_json(const SearchableAlgorithm &algorithm) {
  return {{"canonical_algorithm_id", algorithm.canonical_algorithm_id},
          {"domain", algorithm.domain},
          {"evidence_ids", algorithm.evidence_ids},
          {"invariants", algorithm.invariants},
          {"qualification_status", algorithm.qualification_status},
          {"semantic_signature", algorithm.semantic_signature},
          {"semantic_terms", algorithm.semantic_terms},
          {"signature", algorithm.signature},
          {"source_snapshot_hash", algorithm.source_snapshot_hash},
          {"structural_ir", to_json(algorithm.structural_ir)}};
}

Json to_json(const AlgorithmSearchQuery &query) {
  return {{"domain", query.domain ? Json(*query.domain) : Json(nullptr)},
          {"limit", query.limit},
          {"require_qualified", query.require_qualified},
          {"required_invariants", query.required_invariants},
          {"required_primitives", query.required_primitives},
          {"semantic_terms", query.semantic_terms},
          {"structural_hash",
           query.structural_hash ? Json(*query.structural_hash) : Json(nullptr)}};
}

Json to_json(const AlgorithmSearchResult &result) {
  return {{"candidates", result.candidates},
          {"excluded", result.excluded},
          {"query_signature", result.query_signature},
          {"result_signature", result.result_signature},
          {"schema_version", result.schema_version},
          {"search_version", result.search_version_value},
          {"selected_algorithm_id",
           result.selected_algorithm_id ? Json(*result.selected_algorithm_id)
                                        : Json(nullptr)},
          {"status", result.status}};
}

std::string
AlgorithmSearchIndex::register_algorithm(SearchableAlgorithm algorithm) {
  finalize_algorithm(algorithm);
  const auto iterator = algorithms_.find(algorithm.canonical_algorithm_id);
  if (iterator != algorithms_.end()) {
    if (iterator->second.signature != algorithm.signature) {
      search_error("immutable searchable algorithm collision");
    }
    return algorithm.canonical_algorithm_id;
  }
  const std::string identity = algorithm.canonical_algorithm_id;
  algorithms_.emplace(identity, std::move(algorithm));
  return identity;
}

const std::map<std::string, SearchableAlgorithm> &
AlgorithmSearchIndex::algorithms() const noexcept {
  return algorithms_;
}

AlgorithmSearchResult AlgorithmSearchIndex::search(AlgorithmSearchQuery query) const {
  if (query.limit < 1U || query.limit > 64U) {
    search_error("SAA search limit outside supported range");
  }
  if (query.structural_hash && !exact_hash(*query.structural_hash)) {
    search_error("SAA search structural hash is invalid");
  }
  if (query.domain) {
    *query.domain = canonical_text(std::move(*query.domain));
    if (query.domain->empty()) {
      query.domain.reset();
    }
  }
  query.required_primitives = canonical_ids(std::move(query.required_primitives));
  for (auto &primitive : query.required_primitives) {
    primitive = normalize_primitive(primitive).name;
  }
  query.required_primitives = canonical_ids(std::move(query.required_primitives));
  query.semantic_terms = canonical_texts(std::move(query.semantic_terms));
  query.required_invariants =
      canonical_texts(std::move(query.required_invariants));
  const std::string query_signature = contracts::sha256_json(
      {{"query", to_json(query)}, {"version", search_version}});

  struct Ranked final {
    int score_bp = 0;
    std::string identity;
    Json receipt;
  };
  std::vector<Ranked> ranked;
  Json excluded = Json::array();
  for (const auto &[identity, algorithm] : algorithms_) {
    std::vector<std::string> reasons;
    int score = 0;
    const auto primitives = algorithm_primitives(algorithm.structural_ir);
    const std::set<std::string> invariants(algorithm.invariants.begin(),
                                          algorithm.invariants.end());
    const std::set<std::string> terms(algorithm.semantic_terms.begin(),
                                     algorithm.semantic_terms.end());
    const auto missing_primitives =
        missing_values(query.required_primitives, primitives);
    const auto missing_invariants =
        missing_values(query.required_invariants, invariants);
    std::vector<std::string> matched_terms;
    for (const auto &term : query.semantic_terms) {
      if (terms.contains(term)) {
        matched_terms.push_back(term);
      }
    }
    if (query.structural_hash) {
      if (algorithm.structural_ir.structural_hash != *query.structural_hash) {
        reasons.push_back("structural_hash_mismatch");
      } else {
        score += 5'000;
      }
    }
    if (query.domain) {
      if (algorithm.domain != *query.domain) {
        reasons.push_back("domain_mismatch");
      } else {
        score += 1'500;
      }
    }
    if (!missing_primitives.empty()) {
      reasons.push_back("missing_primitives");
    } else if (!query.required_primitives.empty()) {
      score += 1'500;
    }
    if (!missing_invariants.empty()) {
      reasons.push_back("missing_invariants");
    } else if (!query.required_invariants.empty()) {
      score += 1'000;
    }
    if (!query.semantic_terms.empty()) {
      if (matched_terms.empty()) {
        reasons.push_back("no_semantic_term_match");
      } else {
        score += static_cast<int>((1'000U * matched_terms.size()) /
                                  query.semantic_terms.size());
      }
    }
    if (query.require_qualified) {
      if (algorithm.qualification_status != "QUALIFIED") {
        reasons.push_back("not_qualified");
      } else {
        score += 1'000;
      }
    }
    const Json receipt = {
        {"algorithm_id", identity},
        {"domain", algorithm.domain},
        {"matched_semantic_terms", matched_terms},
        {"missing_invariants", missing_invariants},
        {"missing_primitives", missing_primitives},
        {"qualification_status", algorithm.qualification_status},
        {"score_bp", std::min(10'000, score)},
        {"semantic_signature", algorithm.semantic_signature},
        {"structural_hash", algorithm.structural_ir.structural_hash}};
    if (reasons.empty()) {
      ranked.push_back({.score_bp = std::min(10'000, score),
                        .identity = identity,
                        .receipt = receipt});
    } else {
      auto exclusion = receipt;
      exclusion["reasons"] = reasons;
      excluded.push_back(std::move(exclusion));
    }
  }
  std::ranges::sort(ranked, [](const Ranked &left, const Ranked &right) {
    if (left.score_bp != right.score_bp) {
      return left.score_bp > right.score_bp;
    }
    return left.identity < right.identity;
  });
  Json candidates = Json::array();
  for (std::size_t index = 0;
       index < std::min(query.limit, ranked.size()); ++index) {
    candidates.push_back(ranked[index].receipt);
  }
  AlgorithmSearchResult result;
  result.query_signature = query_signature;
  result.candidates = std::move(candidates);
  result.excluded = std::move(excluded);
  if (!ranked.empty()) {
    result.selected_algorithm_id = ranked.front().identity;
    result.status = "QUALIFIED_ALGORITHM_FOUND";
  } else {
    result.status = "NO_QUALIFIED_ALGORITHM_FIT";
  }
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

} // namespace statewright::saa
