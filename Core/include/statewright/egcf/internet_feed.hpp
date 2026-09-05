#pragma once

#include "statewright/egcf/brain_feed.hpp"
#include "statewright/egcf/canonical_algorithm_store.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_feed_coordinator_version =
    "statewright-internet-feed-coordinator-v1";

struct InternetFeedResult final {
  BrainFeedBatchReceipt brain_feed_batch;
  std::vector<InternetKnowledgeSearchReceipt> retrieval_receipts;
  std::vector<InternetAlgorithmCandidate> candidates;
  std::string result_signature;
};

class InternetFeedCoordinator final {
public:
  explicit InternetFeedCoordinator(EgcfStore &store);

  [[nodiscard]] InternetFeedResult
  process(const sources::InternetPolicyAssessment &assessment,
          const sources::InternetExtractionResult &extraction,
          std::string source_label, bool strict = false);

private:
  EgcfStore &store_;
  InternetImprovementStore internet_;
  BrainFeedProcessor brain_feed_;
  CanonicalAlgorithmStore canonical_algorithms_;
};

[[nodiscard]] contracts::Json to_json(const InternetFeedResult &value);
void verify_internet_candidate_translation(
    const InternetAlgorithmCandidate &candidate,
    const sources::InternetSourceFragment &fragment);

// A retrieval receipt alone is not a completed algorithm fragment. Return the
// durable batch/retrieval/candidate outputs only when every fragment is fed.
[[nodiscard]] std::optional<std::vector<std::string>>
internet_feed_completion_outputs(
    const sources::InternetExtractionReceipt &extraction,
    const std::vector<StoredObject> &records);

} // namespace statewright::egcf
