#include "statewright/core/transaction.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>

namespace statewright::core {
namespace {

[[nodiscard]] std::vector<std::byte> bytes_from_text(std::string_view text) {
  const auto *data = reinterpret_cast<const std::byte *>(text.data());
  return {data, data + text.size()};
}

[[nodiscard]] std::string hash_bytes(const std::vector<std::byte> &bytes) {
  return contracts::sha256_bytes(bytes);
}

[[nodiscard]] std::string replace_text(std::string original,
                                       std::string_view old_text,
                                       std::string_view new_text, int count) {
  if (old_text.empty()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "old text must be non-empty");
  }
  std::size_t position = 0;
  int replacements = 0;
  while ((position = original.find(old_text, position)) != std::string::npos &&
         (count < 0 || replacements < count)) {
    original.replace(position, old_text.size(), new_text);
    position += new_text.size();
    ++replacements;
  }
  if (replacements == 0) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "old text not found");
  }
  return original;
}

[[nodiscard]] std::string whole_file_diff(
    const std::map<std::string, std::optional<std::vector<std::byte>>> &originals,
    const std::map<std::string, std::vector<std::byte>> &candidates) {
  std::ostringstream output;
  for (const auto &[path, candidate] : candidates) {
    const auto original = originals.at(path).value_or(std::vector<std::byte>{});
    output << "--- a/" << path << '\n' << "+++ b/" << path << '\n';
    output << "@@ -1 +1 @@\n";
    const std::string before(reinterpret_cast<const char *>(original.data()),
                             original.size());
    const std::string after(reinterpret_cast<const char *>(candidate.data()),
                            candidate.size());
    if (!before.empty()) {
      output << '-' << before;
      if (!before.ends_with('\n')) {
        output << '\n';
      }
    }
    if (!after.empty()) {
      output << '+' << after;
      if (!after.ends_with('\n')) {
        output << '\n';
      }
    }
  }
  return output.str();
}

[[nodiscard]] std::filesystem::perms permissions_from_mode(unsigned int mode) {
  return static_cast<std::filesystem::perms>(mode & 0777U);
}

[[nodiscard]] bool is_sha256(std::string_view value) {
  return value.size() == 64U &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isdigit(character) != 0 ||
                  (character >= static_cast<unsigned char>('a') &&
                   character <= static_cast<unsigned char>('f'));
         });
}

[[nodiscard]] BackupMetadata
backup_metadata_from_json(const contracts::Json &value) {
  try {
    if (!value.is_object() || value.size() != 4U) {
      throw std::runtime_error("backup metadata has unexpected fields");
    }
    BackupMetadata result;
    result.existed = value.at("existed").get<bool>();
    if (!value.at("mode").is_null()) {
      result.mode = value.at("mode").get<unsigned int>();
    }
    if (!value.at("sha256").is_null()) {
      result.sha256 = value.at("sha256").get<std::string>();
    }
    result.backup = value.at("backup").get<std::string>();
    return result;
  } catch (const std::exception &exception) {
    throw common::Error(common::ErrorCode::json_contract,
                        "invalid transaction backup metadata: " +
                            std::string(exception.what()));
  }
}

[[nodiscard]] TransactionRecord
transaction_from_json(const contracts::Json &value) {
  try {
    if (!value.is_object() || value.size() != 15U) {
      throw std::runtime_error("transaction record has unexpected fields");
    }
    TransactionRecord result;
    result.action_id = value.at("action_id").get<std::string>();
    result.applied_hashes =
        value.at("applied_hashes").get<std::map<std::string, std::string>>();
    result.applied_snapshot_hash =
        value.at("applied_snapshot_hash").get<std::string>();
    result.authority_hash = value.at("authority_hash").get<std::string>();
    for (const auto &[path, metadata] : value.at("backup_manifest").items()) {
      result.backup_manifest.emplace(path, backup_metadata_from_json(metadata));
    }
    result.candidate_files =
        value.at("candidate_files").get<std::map<std::string, std::string>>();
    result.candidate_hash = value.at("candidate_hash").get<std::string>();
    result.diff = value.at("diff").get<std::string>();
    result.operation = value.at("operation").get<std::string>();
    for (const auto &[path, hash] : value.at("original_hashes").items()) {
      result.original_hashes[path] =
          hash.is_null() ? std::nullopt
                         : std::optional<std::string>(hash.get<std::string>());
    }
    result.source_snapshot_hash =
        value.at("source_snapshot_hash").get<std::string>();
    result.status = value.at("status").get<std::string>();
    result.targets = value.at("targets").get<std::vector<std::string>>();
    result.transaction_id = value.at("transaction_id").get<std::string>();
    result.verification_evidence_ids =
        value.at("verification_evidence_ids").get<std::vector<std::string>>();
    return result;
  } catch (const common::Error &) {
    throw;
  } catch (const std::exception &exception) {
    throw common::Error(common::ErrorCode::json_contract,
                        "invalid transaction record: " +
                            std::string(exception.what()));
  }
}

} // namespace

contracts::Json to_json(const BackupMetadata &metadata) {
  return {{"backup", metadata.backup},
          {"existed", metadata.existed},
          {"mode", metadata.mode ? contracts::Json(*metadata.mode)
                                  : contracts::Json(nullptr)},
          {"sha256", metadata.sha256 ? contracts::Json(*metadata.sha256)
                                      : contracts::Json(nullptr)}};
}

contracts::Json to_json(const TransactionRecord &record) {
  contracts::Json backups = contracts::Json::object();
  for (const auto &[path, metadata] : record.backup_manifest) {
    backups[path] = to_json(metadata);
  }
  return {
      {"action_id", record.action_id},
      {"applied_hashes", record.applied_hashes},
      {"applied_snapshot_hash", record.applied_snapshot_hash},
      {"authority_hash", record.authority_hash},
      {"backup_manifest", backups},
      {"candidate_files", record.candidate_files},
      {"candidate_hash", record.candidate_hash},
      {"diff", record.diff},
      {"operation", record.operation},
      {"original_hashes", record.original_hashes},
      {"source_snapshot_hash", record.source_snapshot_hash},
      {"status", record.status},
      {"targets", record.targets},
      {"transaction_id", record.transaction_id},
      {"verification_evidence_ids", record.verification_evidence_ids},
  };
}

TransactionManager::TransactionManager(const Workspace &workspace,
                                       std::filesystem::path state_dir,
                                       std::string authority_hash,
                                       EventStore &events)
    : workspace_(workspace), state_dir_(std::move(state_dir)),
      root_(state_dir_ / "transactions"),
      authority_hash_(std::move(authority_hash)), events_(events) {
  std::filesystem::create_directories(root_);
}

TransactionRecord TransactionManager::prepare_write(std::string_view path,
                                                    std::string_view content) {
  return prepare_changes(
      {{.type = ChangeType::write, .path = std::string(path),
        .content = std::string(content), .old_text = {}, .new_text = {},
        .count = 1}});
}

TransactionRecord
TransactionManager::prepare_changes(const std::vector<Change> &changes) {
  if (changes.empty()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "multi-file transaction requires at least one change");
  }
  std::map<std::string, std::vector<std::byte>> candidates;
  std::map<std::string, std::optional<std::vector<std::byte>>> originals;
  std::set<ChangeType> operations;
  for (const auto &change : changes) {
    const std::string canonical_path = workspace_.canonical(change.path);
    if (candidates.contains(canonical_path)) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "duplicate transaction target: " + canonical_path);
    }
    const auto target = workspace_.resolve(canonical_path);
    std::optional<std::vector<std::byte>> original;
    if (std::filesystem::exists(target)) {
      if (!std::filesystem::is_regular_file(target) ||
          std::filesystem::is_symlink(target)) {
        throw common::Error(common::ErrorCode::policy_denied,
                            "transaction target is not a regular file");
      }
      original = read_bytes(target);
    }

    if (change.type == ChangeType::write) {
      candidates.emplace(canonical_path, bytes_from_text(change.content));
    } else {
      if (!original) {
        throw common::Error(common::ErrorCode::policy_denied,
                            "replace target does not exist: " + canonical_path);
      }
      const std::string original_text(
          reinterpret_cast<const char *>(original->data()), original->size());
      candidates.emplace(
          canonical_path,
          bytes_from_text(replace_text(original_text, change.old_text,
                                       change.new_text, change.count)));
    }
    originals.emplace(canonical_path, std::move(original));
    operations.insert(change.type);
  }

  std::string operation = operations.size() == 1U && changes.size() == 1U
                              ? (changes.front().type == ChangeType::write
                                     ? "write_file"
                                     : "replace_text")
                              : "multi_file";
  if (changes.size() > 1U) {
    operation = "multi_file";
  }
  return prepare(std::move(operation), candidates, originals);
}

TransactionRecord TransactionManager::prepare_replace(
    std::string_view path, std::string_view old_text, std::string_view new_text,
    int count) {
  return prepare_changes({{.type = ChangeType::replace,
                           .path = std::string(path),
                           .content = {},
                           .old_text = std::string(old_text),
                           .new_text = std::string(new_text),
                           .count = count}});
}

TransactionRecord
TransactionManager::load(std::string_view transaction_id) const {
  if (!is_sha256(transaction_id)) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "invalid transaction identifier");
  }
  const auto record = transaction_from_json(contracts::parse_json(read_text(
      root_ / std::string(transaction_id) / "transaction.json")));
  if (record.transaction_id != transaction_id ||
      record.authority_hash != authority_hash_) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "transaction identity or authority mismatch");
  }
  const contracts::Json transaction_material = {
      {"candidate_hash", record.candidate_hash},
      {"operation", record.operation},
      {"source_snapshot_hash", record.source_snapshot_hash},
      {"targets", record.targets},
  };
  if (contracts::sha256_json(transaction_material) != record.transaction_id) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "transaction identity is not canonical");
  }
  std::set<std::string> targets;
  for (const auto &target : record.targets) {
    const auto canonical = workspace_.canonical(target);
    if (canonical != target || !targets.insert(target).second) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "transaction target set is invalid");
    }
    const auto expected_candidate =
        (std::filesystem::path("transactions") / record.transaction_id /
         "candidate" / contracts::sha256_text(target))
            .generic_string();
    const auto candidate = record.candidate_files.find(target);
    if (candidate == record.candidate_files.end() ||
        candidate->second != expected_candidate ||
        !record.original_hashes.contains(target)) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "transaction candidate metadata is invalid");
    }
    const auto backup = record.backup_manifest.find(target);
    if (backup != record.backup_manifest.end()) {
      const auto expected_backup =
          (std::filesystem::path("transactions") / record.transaction_id /
           "original" / contracts::sha256_text(target))
              .generic_string();
      if (backup->second.backup != expected_backup) {
        throw common::Error(common::ErrorCode::policy_denied,
                            "transaction backup metadata is invalid");
      }
    }
  }
  if (record.candidate_files.size() != targets.size() ||
      record.original_hashes.size() != targets.size() ||
      record.backup_manifest.size() > targets.size()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "transaction metadata target mismatch");
  }
  return record;
}

TransactionRecord TransactionManager::prepare(
    std::string operation,
    const std::map<std::string, std::vector<std::byte>> &candidates,
    const std::map<std::string, std::optional<std::vector<std::byte>>> &originals) {
  const std::string source_snapshot_hash = workspace_.snapshot_hash();
  contracts::Json candidate_hashes = contracts::Json::object();
  for (const auto &[path, content] : candidates) {
    candidate_hashes[path] = hash_bytes(content);
  }
  const std::string candidate_hash = contracts::sha256_json(candidate_hashes);
  std::vector<std::string> targets;
  for (const auto &[path, unused] : candidates) {
    static_cast<void>(unused);
    targets.push_back(path);
  }
  const contracts::Json transaction_material = {
      {"candidate_hash", candidate_hash},
      {"operation", operation},
      {"source_snapshot_hash", source_snapshot_hash},
      {"targets", targets},
  };
  const std::string transaction_id =
      contracts::sha256_json(transaction_material);
  const auto transaction_dir = root_ / transaction_id;
  const auto candidate_dir = transaction_dir / "candidate";
  std::filesystem::create_directories(candidate_dir);

  TransactionRecord record;
  record.transaction_id = transaction_id;
  record.operation = std::move(operation);
  record.targets = targets;
  record.source_snapshot_hash = source_snapshot_hash;
  record.candidate_hash = candidate_hash;
  record.diff = whole_file_diff(originals, candidates);
  record.authority_hash = authority_hash_;

  for (const auto &[path, content] : candidates) {
    const auto storage_name = contracts::sha256_text(path);
    const auto stored = candidate_dir / storage_name;
    atomic_write_bytes(stored, content,
                       std::filesystem::perms::owner_read |
                           std::filesystem::perms::owner_write);
    record.candidate_files[path] =
        stored.lexically_relative(state_dir_).generic_string();
  }
  for (const auto &[path, content] : originals) {
    record.original_hashes[path] =
        content ? std::optional<std::string>(hash_bytes(*content)) : std::nullopt;
  }

  save(record);
  try {
    append_event("transaction_prepared", record);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(transaction_dir, ignored);
    throw;
  }
  return record;
}

void TransactionManager::verify_candidate(const TransactionRecord &record) const {
  contracts::Json observed = contracts::Json::object();
  for (const auto &[target, stored_relative] : record.candidate_files) {
    const auto stored = state_dir_ / stored_relative;
    if (!std::filesystem::is_regular_file(stored)) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "candidate artifact missing for " + target);
    }
    observed[target] = contracts::sha256_file(stored);
  }
  if (contracts::sha256_json(observed) != record.candidate_hash) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "candidate hash mismatch");
  }
}

void TransactionManager::apply(TransactionRecord &record) {
  if (record.status != "PREPARED") {
    throw common::Error(common::ErrorCode::policy_denied,
                        "transaction is not prepared");
  }
  if (workspace_.snapshot_hash() != record.source_snapshot_hash) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "source drift blocks transaction apply");
  }
  verify_candidate(record);
  const auto transaction_dir = root_ / record.transaction_id;
  const auto backup_dir = transaction_dir / "original";
  std::filesystem::create_directories(backup_dir);
  std::vector<std::string> applied;
  try {
    for (const auto &target_name : record.targets) {
      const auto target = workspace_.resolve(target_name);
      const auto candidate = state_dir_ / record.candidate_files.at(target_name);
      BackupMetadata metadata;
      metadata.existed = std::filesystem::exists(target);
      if (metadata.existed) {
        const auto permissions = std::filesystem::status(target).permissions();
        metadata.mode = static_cast<unsigned int>(permissions) & 0777U;
        metadata.sha256 = workspace_.file_hash(target_name);
        const auto backup = backup_dir / contracts::sha256_text(target_name);
        atomic_write_bytes(backup, read_bytes(target),
                           permissions_from_mode(*metadata.mode));
        metadata.backup = backup.lexically_relative(state_dir_).generic_string();
      }
      record.backup_manifest[target_name] = metadata;
      const auto permissions = metadata.mode
                                   ? permissions_from_mode(*metadata.mode)
                                   : (std::filesystem::perms::owner_read |
                                      std::filesystem::perms::owner_write);
      atomic_write_bytes(target, read_bytes(candidate), permissions);
      applied.push_back(target_name);
      const std::string observed = workspace_.file_hash(target_name).value_or("");
      const std::string expected = contracts::sha256_file(candidate);
      if (observed != expected) {
        throw common::Error(common::ErrorCode::policy_denied,
                            "post-write hash mismatch for " + target_name);
      }
      record.applied_hashes[target_name] = observed;
    }
    record.applied_snapshot_hash = workspace_.snapshot_hash();
    record.status = "APPLIED";
    save(record);
    append_event("transaction_applied", record);
  } catch (...) {
    restore(record, applied);
    record.status = "PREPARED";
    save(record);
    throw;
  }
}

void TransactionManager::verify_applied(const TransactionRecord &record) const {
  if (record.status != "APPLIED" && record.status != "VERIFIED") {
    throw common::Error(common::ErrorCode::policy_denied,
                        "transaction is not applied");
  }
  for (const auto &[target, expected] : record.applied_hashes) {
    if (workspace_.file_hash(target).value_or("") != expected) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "applied target hash mismatch for " + target);
    }
  }
  if (!record.applied_snapshot_hash.empty() &&
      workspace_.snapshot_hash() != record.applied_snapshot_hash) {
    throw common::Error(
        common::ErrorCode::policy_denied,
        "workspace changed after transaction apply; verification is blocked");
  }
}

void TransactionManager::finalize(
    TransactionRecord &record, const std::vector<std::string> &evidence_ids) {
  if (record.status != "APPLIED") {
    throw common::Error(common::ErrorCode::policy_denied,
                        "only an applied transaction can be finalized");
  }
  std::set<std::string> seen;
  record.verification_evidence_ids.clear();
  for (const auto &evidence_id : evidence_ids) {
    if (seen.insert(evidence_id).second) {
      record.verification_evidence_ids.push_back(evidence_id);
    }
  }
  record.status = "VERIFIED";
  save(record);
  append_event("transaction_verified", record);
}

void TransactionManager::rollback(TransactionRecord &record) {
  if (record.status != "APPLIED" && record.status != "VERIFIED") {
    throw common::Error(common::ErrorCode::policy_denied,
                        "transaction cannot be rolled back");
  }
  restore(record, record.targets);
  record.status = "ROLLED_BACK";
  save(record);
  append_event("transaction_rolled_back", record);
}

void TransactionManager::discard(TransactionRecord &record) {
  if (record.status != "PREPARED") {
    throw common::Error(common::ErrorCode::policy_denied,
                        "only a prepared transaction can be discarded");
  }
  record.status = "DISCARDED";
  save(record);
  append_event("transaction_discarded", record);
}

void TransactionManager::save(const TransactionRecord &record) const {
  const auto path = root_ / record.transaction_id / "transaction.json";
  atomic_write_text(path, to_json(record).dump(2) + "\n",
                    std::filesystem::perms::owner_read |
                        std::filesystem::perms::owner_write);
}

void TransactionManager::append_event(
    std::string_view event_type, const TransactionRecord &record) const {
  const contracts::Json payload = {
      {"record_hash", contracts::sha256_json(to_json(record))},
      {"status", record.status},
      {"transaction_id", record.transaction_id},
  };
  static_cast<void>(events_.append(
      event_type, payload,
      {.run_id = {},
       .action_id = record.action_id,
       .transaction_id = record.transaction_id}));
}

void TransactionManager::restore(
    const TransactionRecord &record,
    const std::vector<std::string> &targets) const {
  for (auto iterator = targets.rbegin(); iterator != targets.rend(); ++iterator) {
    const std::string &target_name = *iterator;
    const auto target = workspace_.resolve(target_name);
    const auto metadata_iterator = record.backup_manifest.find(target_name);
    if (metadata_iterator == record.backup_manifest.end()) {
      continue;
    }
    const BackupMetadata &metadata = metadata_iterator->second;
    if (metadata.existed) {
      const auto backup = state_dir_ / metadata.backup;
      if (!std::filesystem::is_regular_file(backup)) {
        throw common::Error(common::ErrorCode::policy_denied,
                            "rollback backup missing for " + target_name);
      }
      atomic_write_bytes(
          target, read_bytes(backup),
          metadata.mode ? std::optional(permissions_from_mode(*metadata.mode))
                        : std::nullopt);
    } else if (std::filesystem::exists(target)) {
      durable_remove(target);
    }
    if (workspace_.file_hash(target_name) != metadata.sha256) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "rollback hash mismatch for " + target_name);
    }
  }
}

} // namespace statewright::core
