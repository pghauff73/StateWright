#include "statewright/egcf/internet_metrics.hpp"

#include <cstdint>
#include <set>
#include <string>

namespace statewright::egcf {
namespace {
using Json = contracts::Json;

void increment(Json &counts, const std::string &key) {
  counts[key] = counts.value(key, std::uint64_t{0}) + 1U;
}

void count_reasons(Json &counts, const Json &payload, const char *key) {
  if (!payload.contains(key))
    return;
  // One reason counts once per record even if repeated by its producer.
  const auto reasons = payload.at(key).get<std::set<std::string>>();
  for (const auto &reason : reasons)
    increment(counts, reason);
}
} // namespace

Json internet_improvement_metrics(const InternetImprovementState &state) {
  const std::set<std::string> active_candidates(
      state.active_candidate_ids.begin(), state.active_candidate_ids.end());
  Json candidate_states = Json::object();
  Json candidate_reasons = Json::object();
  Json fetch_states = Json::object();
  Json fetch_reasons = Json::object();
  Json source_reasons = Json::object();
  Json qualification_states = Json::object();
  Json qualification_reasons = Json::object();
  Json extraction_reasons = Json::object();
  Json novelty_states = Json::object();
  Json records_by_type = Json::object();
  std::set<std::string> admitted_algorithms;
  std::set<std::string> canonical_algorithms;
  std::set<std::string> probationary_algorithms;
  std::set<std::string> counted_records;
  std::uint64_t compressed_bytes = 0U;
  std::uint64_t decompressed_bytes = 0U;
  std::uint64_t elapsed_fetch_ms = 0U;
  std::uint64_t timed_receipts = 0U;
  std::uint64_t exact_duplicate_receipts = 0U;
  std::uint64_t equivalent_duplicate_receipts = 0U;
  std::uint64_t enabled_watches = 0U;
  const std::set<std::string> active_watches(state.active_watch_ids.begin(),
                                             state.active_watch_ids.end());
  for (const auto &record : state.internet_records) {
    if (!counted_records.insert(record.object_id).second)
      continue;
    increment(records_by_type, record.object_type);
    const auto &payload = record.payload;
    if (record.object_type == "internet-watch") {
      if (active_watches.contains(record.object_id) &&
          payload.at("enabled").get<bool>())
        ++enabled_watches;
    } else if (record.object_type == "internet-algorithm-candidate") {
      if (!active_candidates.contains(record.object_id))
        continue;
      const auto status = payload.at("status").get<std::string>();
      increment(candidate_states, status);
      if (status == "QUARANTINED" || status == "REJECTED" ||
          status == "RETRACTED")
        count_reasons(candidate_reasons, payload, "unresolved_assumptions");
      if (status == "CANONICAL" || status == "PROBATIONARY_CANONICAL") {
        auto &algorithms = status == "CANONICAL" ? canonical_algorithms
                                                 : probationary_algorithms;
        for (const auto &id : payload.at("canonical_algorithm_ids"))
          algorithms.insert(id.get<std::string>());
      }
    } else if (record.object_type == "internet-fetch-receipt") {
      increment(fetch_states, payload.at("status").get<std::string>());
      const auto reason = payload.value("failure_reason", std::string{});
      if (!reason.empty())
        increment(fetch_reasons, reason);
      compressed_bytes += payload.value("compressed_bytes", std::uint64_t{0});
      decompressed_bytes +=
          payload.value("decompressed_bytes", std::uint64_t{0});
      const auto duration =
          payload.value("total_time_milliseconds", std::int64_t{0});
      if (duration > 0) {
        elapsed_fetch_ms += static_cast<std::uint64_t>(duration);
        ++timed_receipts;
      }
    } else if (record.object_type == "internet-policy-assessment") {
      count_reasons(source_reasons, payload, "blocking_reasons");
    } else if (record.object_type == "internet-extraction-receipt") {
      count_reasons(extraction_reasons, payload, "rejected_fragments");
    } else if (record.object_type == "internet-retrieval-receipt") {
      const auto novelty = payload.at("novelty_status").get<std::string>();
      increment(novelty_states, novelty);
      if (!payload.at("exact_match_ids").empty())
        ++exact_duplicate_receipts;
      if (!payload.at("equivalent_match_ids").empty())
        ++equivalent_duplicate_receipts;
    } else if (record.object_type == "internet-experiment-qualification") {
      increment(qualification_states, payload.at("status").get<std::string>());
      count_reasons(qualification_reasons, payload, "blocking_reasons");
    } else if (record.object_type == "internet-probation-admission") {
      if (payload.at("admission_status") == "PROBATIONARY_CANONICAL")
        admitted_algorithms.insert(
            payload.at("canonical_algorithm_ref").get<std::string>());
    }
  }
  const auto accepted = admitted_algorithms.size();
  const auto per_accepted = [accepted](std::uint64_t total) -> Json {
    if (accepted == 0U)
      return nullptr;
    return static_cast<double>(total) / static_cast<double>(accepted);
  };
  return {
      {"schema_version", 1},
      {"event_head", state.event_head},
      {"projection_digest", state.projection_digest},
      {"records_by_type", records_by_type},
      {"watches",
       {{"current", active_watches.size()}, {"enabled", enabled_watches}}},
      {"candidates",
       {{"current_by_status", candidate_states},
        {"quarantine_reasons", candidate_reasons}}},
      {"fetches",
       {{"by_status", fetch_states},
        {"successful", fetch_states.value("FETCH_SUCCEEDED", std::uint64_t{0})},
        {"cache_hits", fetch_states.value("NOT_MODIFIED", std::uint64_t{0})},
        {"failure_reasons", fetch_reasons},
        {"compressed_bytes", compressed_bytes},
        {"decompressed_bytes", decompressed_bytes},
        {"elapsed_milliseconds", elapsed_fetch_ms},
        {"receipts_with_positive_timing", timed_receipts}}},
      {"discovery",
       {{"retrievals_by_novelty", novelty_states},
        {"retrievals_with_exact_matches", exact_duplicate_receipts},
        {"retrievals_with_equivalent_matches", equivalent_duplicate_receipts}}},
      {"source_blocking_reasons", source_reasons},
      {"extraction_rejections", extraction_reasons},
      {"qualifications",
       {{"by_status", qualification_states},
        {"passed",
         qualification_states.value("EXPERIMENT_QUALIFIED", std::uint64_t{0})},
        {"blocking_reasons", qualification_reasons}}},
      {"algorithms",
       {{"accepted_ever", accepted},
        {"currently_canonical", canonical_algorithms.size()},
        {"currently_probationary", probationary_algorithms.size()}}},
      {"cost_per_accepted_algorithm",
       {{"compressed_bytes", per_accepted(compressed_bytes)},
        {"decompressed_bytes", per_accepted(decompressed_bytes)},
        {"fetch_milliseconds", per_accepted(elapsed_fetch_ms)}}},
      {"measurement_scope",
       "Cumulative immutable receipt costs per distinct algorithm ever "
       "admitted; current candidate states follow supersedence. Fetch time "
       "excludes unrecorded failure time and is not total CPU or wall-clock "
       "cost."}};
}

Json internet_improvement_metrics(EgcfStore &store) {
  // The reader validates the authoritative object/event projection. The time
  // fields are unused by aggregation; no clock or polling action is required.
  return internet_improvement_metrics(
      InternetImprovementStateReader(store).read("1970-01-01T00:00:00Z",
                                                 "metrics-v1"));
}

} // namespace statewright::egcf
