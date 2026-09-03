#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/core/event_store.hpp"
#include "statewright/core/workspace.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::core {

enum class ChangeType { write, replace };

struct Change final {
  ChangeType type = ChangeType::write;
  std::string path;
  std::string content;
  std::string old_text;
  std::string new_text;
  int count = 1;
};

struct BackupMetadata final {
  bool existed = false;
  std::optional<unsigned int> mode;
  std::optional<std::string> sha256;
  std::string backup;
};

struct TransactionRecord final {
  std::string transaction_id;
  std::string operation;
  std::vector<std::string> targets;
  std::string source_snapshot_hash;
  std::string candidate_hash;
  std::map<std::string, std::string> candidate_files;
  std::map<std::string, std::optional<std::string>> original_hashes;
  std::string diff;
  std::string authority_hash;
  std::string status = "PREPARED";
  std::string action_id;
  std::map<std::string, std::string> applied_hashes;
  std::string applied_snapshot_hash;
  std::map<std::string, BackupMetadata> backup_manifest;
  std::vector<std::string> verification_evidence_ids;
};

[[nodiscard]] contracts::Json to_json(const BackupMetadata &metadata);
[[nodiscard]] contracts::Json to_json(const TransactionRecord &record);

class TransactionManager final {
public:
  TransactionManager(const Workspace &workspace, std::filesystem::path state_dir,
                     std::string authority_hash, EventStore &events);

  [[nodiscard]] TransactionRecord prepare_write(std::string_view path,
                                                std::string_view content);
  [[nodiscard]] TransactionRecord
  prepare_changes(const std::vector<Change> &changes);
  [[nodiscard]] TransactionRecord prepare_replace(std::string_view path,
                                                  std::string_view old_text,
                                                  std::string_view new_text,
                                                  int count = 1);
  [[nodiscard]] TransactionRecord load(std::string_view transaction_id) const;

  void verify_candidate(const TransactionRecord &record) const;
  void apply(TransactionRecord &record);
  void verify_applied(const TransactionRecord &record) const;
  void finalize(TransactionRecord &record,
                const std::vector<std::string> &evidence_ids);
  void rollback(TransactionRecord &record);
  void discard(TransactionRecord &record);

private:
  [[nodiscard]] TransactionRecord prepare(
      std::string operation,
      const std::map<std::string, std::vector<std::byte>> &candidates,
      const std::map<std::string, std::optional<std::vector<std::byte>>> &originals);
  void save(const TransactionRecord &record) const;
  void append_event(std::string_view event_type,
                    const TransactionRecord &record) const;
  void restore(const TransactionRecord &record,
               const std::vector<std::string> &targets) const;

  const Workspace &workspace_;
  std::filesystem::path state_dir_;
  std::filesystem::path root_;
  std::string authority_hash_;
  EventStore &events_;
};

} // namespace statewright::core
