#pragma once

#include "statewright/saa/search.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr int algorithm_projection_schema_version = 1;

class AlgorithmSearchProjection final {
public:
  explicit AlgorithmSearchProjection(std::filesystem::path database_path);

  [[nodiscard]] const std::filesystem::path &database_path() const noexcept;
  [[nodiscard]] std::string
  rebuild(const AlgorithmSearchIndex &authoritative_index) const;
  void verify(const AlgorithmSearchIndex &authoritative_index) const;
  [[nodiscard]] std::vector<std::string>
  search_text(std::string_view query, std::size_t limit = 10U) const;

private:
  std::filesystem::path database_path_;
};

} // namespace statewright::saa
