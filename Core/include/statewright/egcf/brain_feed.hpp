#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/semantic_ontology.hpp"
#include "statewright/egcf/store.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view brain_feed_version =
    "saa-batch-brain-feed-v1";
inline constexpr std::size_t maximum_brain_feed_items = 4096U;

struct BrainFeedItem final {
  std::string item_id;
  std::string kind;
  contracts::Json payload = contracts::Json::object();
  std::vector<std::string> depends_on;
  std::vector<std::string> evidence_from;
  std::string source_path;
  std::string content_signature;
  std::string item_signature;

  [[nodiscard]] std::string object_id() const;
};

struct BrainFeedDisposition final {
  std::string item_id;
  std::string item_signature;
  std::string content_signature;
  std::string kind;
  std::string status;
  std::string route;
  std::vector<std::string> target_refs;
  std::vector<std::string> reasons;
  std::string duplicate_of_item_signature;
  bool canonical_algorithm_admission_attempted = false;
  std::string disposition_signature;

  [[nodiscard]] bool quarantined() const noexcept;
  [[nodiscard]] bool staged() const noexcept;
  [[nodiscard]] bool admitted() const noexcept;
  [[nodiscard]] bool duplicate() const noexcept;
  [[nodiscard]] std::string object_id() const;
};

struct BrainFeedBatchReceipt final {
  std::string batch_id;
  std::string source_signature;
  std::string source_label;
  bool strict = false;
  std::size_t item_count = 0;
  std::size_t admitted_count = 0;
  std::size_t staged_count = 0;
  std::size_t quarantined_count = 0;
  std::size_t duplicate_count = 0;
  std::vector<BrainFeedDisposition> dispositions;
  std::string status;
  std::size_t canonical_algorithm_admissions = 0;
  std::string batch_signature;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] contracts::Json to_json(const BrainFeedItem &item);
[[nodiscard]] contracts::Json to_json(const BrainFeedDisposition &disposition);
[[nodiscard]] contracts::Json to_json(const BrainFeedBatchReceipt &receipt);
[[nodiscard]] BrainFeedItem brain_feed_item_from_json(
    const contracts::Json &value);
[[nodiscard]] BrainFeedDisposition brain_feed_disposition_from_json(
    const contracts::Json &value);
[[nodiscard]] BrainFeedBatchReceipt brain_feed_batch_from_json(
    const contracts::Json &value);

[[nodiscard]] BrainFeedItem make_brain_feed_item(
    std::string item_id, std::string kind, contracts::Json payload,
    std::vector<std::string> depends_on = {},
    std::vector<std::string> evidence_from = {}, std::string source_path = {});
[[nodiscard]] BrainFeedDisposition make_brain_feed_disposition(
    const BrainFeedItem &item, std::string status, std::string route,
    std::vector<std::string> target_refs = {},
    std::vector<std::string> reasons = {},
    std::string duplicate_of_item_signature = {});
[[nodiscard]] BrainFeedBatchReceipt make_brain_feed_batch_receipt(
    std::string batch_id, std::string source_signature,
    std::string source_label, bool strict,
    std::vector<BrainFeedDisposition> dispositions);

class BrainFeedProcessor final {
public:
  explicit BrainFeedProcessor(EgcfStore &store);

  [[nodiscard]] BrainFeedBatchReceipt feed(
      std::string batch_id, std::string source_signature,
      std::string source_label, std::vector<BrainFeedItem> items,
      bool strict = false);
  [[nodiscard]] std::vector<BrainFeedDisposition> dispositions();
  [[nodiscard]] std::vector<BrainFeedBatchReceipt> batches();

private:
  [[nodiscard]] BrainFeedDisposition process_new(
      const BrainFeedItem &item, std::string_view item_ref,
      std::string_view source_signature,
      const std::vector<BrainFeedDisposition> &resolved);

  EgcfStore &store_;
  EvidenceManager evidence_;
  SemanticOntologyStore semantics_;
};

struct RepositoryScanPolicy final {
  std::size_t max_files = 1024U;
  std::size_t max_total_bytes = 64U * 1024U * 1024U;
  std::size_t max_file_bytes = 2U * 1024U * 1024U;
  std::size_t max_symbols = 8192U;
  bool include_tests = true;
  bool include_docs = true;
};

struct RepositoryFeedPlan final {
  std::filesystem::path source_root;
  std::string repository_name;
  std::string repository_signature;
  std::size_t file_count = 0;
  std::size_t symbol_count = 0;
  std::size_t total_bytes = 0;
  contracts::Json language_counts = contracts::Json::object();
  std::vector<contracts::Json> skipped;
  std::vector<BrainFeedItem> items;
};

[[nodiscard]] contracts::Json to_json(const RepositoryFeedPlan &plan);
[[nodiscard]] RepositoryFeedPlan scan_repository(
    const std::filesystem::path &source,
    const RepositoryScanPolicy &policy = RepositoryScanPolicy{});
[[nodiscard]] BrainFeedBatchReceipt feed_repository(
    BrainFeedProcessor &processor, const std::filesystem::path &source,
    const RepositoryScanPolicy &policy = RepositoryScanPolicy{},
    bool strict = false);

} // namespace statewright::egcf
