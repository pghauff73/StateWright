#pragma once

#include "statewright/saa/algorithm_ir.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view search_version =
    "statewright-saa-search-v1";

struct SearchableAlgorithm final {
  std::string canonical_algorithm_id;
  CanonicalAlgorithmIR structural_ir;
  std::string domain;
  std::vector<std::string> semantic_terms;
  std::vector<std::string> invariants;
  std::vector<std::string> evidence_ids;
  std::string qualification_status = "CANDIDATE";
  std::string source_snapshot_hash;
  std::string semantic_signature;
  std::string signature;
};

struct AlgorithmSearchQuery final {
  std::optional<std::string> structural_hash;
  std::optional<std::string> domain;
  std::vector<std::string> required_primitives;
  std::vector<std::string> semantic_terms;
  std::vector<std::string> required_invariants;
  bool require_qualified = true;
  std::size_t limit = 10U;
};

struct AlgorithmSearchResult final {
  int schema_version = 1;
  std::string search_version_value = std::string(search_version);
  std::string query_signature;
  contracts::Json candidates = contracts::Json::array();
  contracts::Json excluded = contracts::Json::array();
  std::optional<std::string> selected_algorithm_id;
  std::string status;
  std::string result_signature;
};

[[nodiscard]] SearchableAlgorithm make_searchable_algorithm(
    CanonicalAlgorithmIR structural_ir, std::string domain,
    std::vector<std::string> semantic_terms,
    std::vector<std::string> invariants = {},
    std::vector<std::string> evidence_ids = {},
    std::string qualification_status = "CANDIDATE",
    std::string source_snapshot_hash = {});

[[nodiscard]] contracts::Json to_json(const SearchableAlgorithm &algorithm);
[[nodiscard]] contracts::Json to_json(const AlgorithmSearchQuery &query);
[[nodiscard]] contracts::Json to_json(const AlgorithmSearchResult &result);

class AlgorithmSearchIndex final {
public:
  [[nodiscard]] std::string register_algorithm(SearchableAlgorithm algorithm);
  [[nodiscard]] const std::map<std::string, SearchableAlgorithm> &
  algorithms() const noexcept;
  [[nodiscard]] AlgorithmSearchResult search(AlgorithmSearchQuery query) const;

private:
  std::map<std::string, SearchableAlgorithm> algorithms_;
};

} // namespace statewright::saa
