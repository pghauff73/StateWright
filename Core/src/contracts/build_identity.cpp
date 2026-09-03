#include "statewright/contracts/build_identity.hpp"

#include "statewright/contracts/build_info.hpp"

namespace statewright::contracts {

Json build_identity() {
  return {
      {"build_type", STATEWRIGHT_BUILD_TYPE},
      {"compiler_id", STATEWRIGHT_COMPILER_ID},
      {"compiler_version", STATEWRIGHT_COMPILER_VERSION},
      {"oracle_commit", STATEWRIGHT_ORACLE_COMMIT},
      {"statewright_version", STATEWRIGHT_VERSION},
  };
}

} // namespace statewright::contracts

