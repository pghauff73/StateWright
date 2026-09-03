#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/governance.hpp"

#include <string>
#include <vector>

namespace statewright::egcf {

struct AssuranceCase final {
  std::string subject_id;
  std::string top_claim;
  contracts::Json subclaims = contracts::Json::array();
  contracts::Json arguments = contracts::Json::array();
  std::vector<std::string> supporting_evidence;
  std::vector<std::string> refuting_evidence;
  std::vector<std::string> invariant_ids;
  std::vector<std::string> decision_ids;
  contracts::Json capability_facts = contracts::Json::object();
  contracts::Json approval_facts = contracts::Json::object();
  contracts::Json rollback_argument = contracts::Json::object();
  std::vector<std::string> gaps;
  std::vector<std::string> conflicts;
  std::vector<std::string> uncertainties;
  std::string conclusion;
  std::string created_at;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] contracts::Json to_json(const AssuranceCase &value);

class AssuranceManager final {
public:
  AssuranceManager(EgcfStore &store, EvidenceManager &evidence,
                   InvariantManager &invariants, DecisionManager &decisions);

  [[nodiscard]] AssuranceCase generate(
      std::string subject_id, std::string top_claim,
      contracts::Json capability_facts = contracts::Json::object(),
      contracts::Json approval_facts = contracts::Json::object(),
      contracts::Json rollback_argument = contracts::Json::object(),
      std::vector<std::string> uncertainties = {});

private:
  EgcfStore &store_;
  EvidenceManager &evidence_;
  InvariantManager &invariants_;
  DecisionManager &decisions_;
};

} // namespace statewright::egcf
