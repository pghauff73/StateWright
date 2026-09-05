#pragma once

#include "statewright/egcf/internet_improvement_director.hpp"

namespace statewright::egcf {

// Counts current candidates using supersedence; receipt and admission totals
// describe immutable history. No scheduling, provider calls, or new records.
[[nodiscard]] contracts::Json
internet_improvement_metrics(const InternetImprovementState &state);
[[nodiscard]] contracts::Json internet_improvement_metrics(EgcfStore &store);

} // namespace statewright::egcf
