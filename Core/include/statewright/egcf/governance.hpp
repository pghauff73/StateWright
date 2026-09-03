#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/store.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

struct InvariantRecord final {
  std::string name;
  std::string statement;
  std::vector<std::string> scope;
  std::string status;
  contracts::Json validator = contracts::Json::object();
  std::vector<std::string> evidence_ids;
  std::string falsifier;
  std::vector<std::string> counterexamples;
  std::string authority;
  std::string created_at;
  std::string supersedes;

  [[nodiscard]] std::string object_id() const;
};

struct DecisionRecord final {
  std::string question;
  std::vector<std::string> alternatives;
  std::string choice;
  std::string rationale;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> constraints;
  std::string owner;
  std::vector<std::string> scope;
  std::string status;
  std::string created_at;
  std::string supersedes;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] contracts::Json to_json(const InvariantRecord &record);
[[nodiscard]] contracts::Json to_json(const DecisionRecord &record);
[[nodiscard]] InvariantRecord
invariant_from_json(const contracts::Json &value);
[[nodiscard]] DecisionRecord decision_from_json(const contracts::Json &value);

class InvariantManager final {
public:
  explicit InvariantManager(EgcfStore &store);

  [[nodiscard]] std::vector<std::string>
  discover(std::vector<std::string> statements, std::vector<std::string> scope,
           std::string source);
  [[nodiscard]] std::string register_invariant(
      std::string name, std::string statement, std::vector<std::string> scope,
      contracts::Json validator, std::vector<std::string> evidence_ids,
      std::string falsifier, std::string authority,
      std::vector<std::string> counterexamples = {});
  [[nodiscard]] std::vector<InvariantRecord>
  records(bool active_only = false);
  [[nodiscard]] std::string validate(std::string_view invariant_id,
                                     bool success,
                                     std::vector<std::string> evidence_ids);
  [[nodiscard]] contracts::Json conflicts();
  [[nodiscard]] std::string supersede(std::string_view old_id,
                                      InvariantRecord replacement,
                                      std::string reason,
                                      std::string authority);

private:
  EgcfStore &store_;
};

class DecisionManager final {
public:
  explicit DecisionManager(EgcfStore &store);

  [[nodiscard]] std::string create(
      std::string question, std::vector<std::string> alternatives,
      std::string choice, std::string rationale,
      std::vector<std::string> evidence_ids,
      std::vector<std::string> constraints, std::string owner,
      std::vector<std::string> scope, bool activate = false,
      std::string authority = {});
  [[nodiscard]] std::vector<DecisionRecord>
  records(bool active_only = false);
  [[nodiscard]] contracts::Json
  query(std::string_view text = {}, std::vector<std::string> scope = {});
  [[nodiscard]] contracts::Json conflicts();
  [[nodiscard]] std::string supersede(
      std::string_view old_id, std::string choice, std::string rationale,
      std::vector<std::string> evidence_ids, std::string authority);

private:
  EgcfStore &store_;
};

} // namespace statewright::egcf
