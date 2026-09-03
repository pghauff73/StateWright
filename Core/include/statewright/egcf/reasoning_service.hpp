#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/store.hpp"

#include <string>
#include <vector>

namespace statewright::egcf {

class OiecSrProposalService final {
public:
  OiecSrProposalService(EgcfStore &store, Ieps &ieps);

  [[nodiscard]] contracts::Json propose(
      const contracts::Json &request, std::string source_snapshot_hash,
      std::vector<std::string> scope = {"**"});

private:
  EgcfStore &store_;
  Ieps &ieps_;
};

} // namespace statewright::egcf
