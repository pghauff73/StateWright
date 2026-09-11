#pragma once

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include <gmpxx.h>
#include <cstdint>
#include <limits>

namespace statewright::egcf {

// Descriptive statistics only: repeated samples on one host are not independent
// experiment groups, a confidence bound or a benchmark promotion decision.
[[nodiscard]] inline contracts::Json summarize_internet_polynomial_performance(
    const contracts::Json &raw, const contracts::Json &design) {
  const auto fail = []() {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_RAW_TIMING_BINDING_INVALID");
  };
  if (raw.at("kind") != "POLYNOMIAL_PAIRED_RAW_MEASUREMENTS_V1" ||
      raw.at("simulated") != false || raw.at("qualification_claim") != "NONE" ||
      raw.at("candidate_ir_sha256") != contracts::sha256_json(design.at("candidate_ir")) ||
      raw.at("baseline_ir_sha256") != contracts::sha256_json(design.at("baseline_ir")) ||
      raw.at("inputs") != design.at("inputs") ||
      raw.at("inputs_sha256") != contracts::sha256_json(design.at("inputs"))) fail();
  const auto &samples = raw.at("samples");
  const auto &outputs = raw.at("reference_outputs");
  if (!samples.is_array() || samples.size() < 2U || samples.size() > 100U ||
      samples.size() != design.at("repetitions").get<std::size_t>() ||
      !outputs.is_array() || outputs.size() != design.at("inputs").size()) fail();
  const auto output_hash = contracts::sha256_json(outputs);
  mpz_class candidate_total = 0, baseline_total = 0;
  std::size_t zero_samples = 0;
  contracts::Json differences = contracts::Json::array();
  const auto duration = [&](const contracts::Json &sample) {
    const auto &value = sample.at("elapsed_nanoseconds");
    if (!value.is_number_integer() || value < 0 ||
        value > std::numeric_limits<std::int64_t>::max() ||
        sample.at("outputs_sha256") != output_hash) fail();
    if (value == 0) ++zero_samples;
    return mpz_class(std::to_string(value.get<std::int64_t>()));
  };
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto &sample = samples.at(index);
    if (sample.at("pair_index") != index ||
        sample.at("candidate_first") != (index % 2U == 0U)) fail();
    const auto candidate = duration(sample.at("candidate"));
    const auto baseline = duration(sample.at("baseline"));
    candidate_total += candidate;
    baseline_total += baseline;
    const mpz_class difference = baseline - candidate;
    differences.push_back(difference.get_str());
  }
  contracts::Json ratio = nullptr;
  if (candidate_total != 0) {
    mpq_class exact_ratio(baseline_total, candidate_total);
    exact_ratio.canonicalize();
    ratio = exact_ratio.get_str();
  }
  return {{"kind", "POLYNOMIAL_DESCRIPTIVE_PERFORMANCE_V1"},
      {"raw_measurement_sha256", contracts::sha256_json(raw)},
      {"pair_count", samples.size()}, {"workload_input_count", outputs.size()},
      {"candidate_total_nanoseconds", candidate_total.get_str()},
      {"baseline_total_nanoseconds", baseline_total.get_str()},
      {"paired_baseline_minus_candidate_nanoseconds", differences},
      {"baseline_over_candidate_total_ratio", ratio},
      {"zero_duration_sample_count", zero_samples},
      {"scope", "DESCRIPTIVE_SAME_PROCESS_ONLY"},
      {"performance_superiority", nullptr}, {"benchmark_score_claim", "NONE"},
      {"limitations", {"No independence or statistical confidence claim",
                        "No mapping to unrelated benchmark tracks",
                        "Zero-duration samples are retained, not discarded"}}};
}

} // namespace statewright::egcf
