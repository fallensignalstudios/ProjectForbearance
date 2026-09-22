// The CompareMetric whitelist for data-authored conditions (TDD 4.3).
// A condition can only read committed facts through one of these names.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace expansion {

struct SessionState;
class Catalog;

// Scope "planet" needs a planet id; "facility" needs a facility instance;
// "sector" reads session-level values.
struct MetricContext {
  std::string planet_id;
  std::uint64_t facility_id = 0;
};

// Returns true and writes `out` when the metric is known and resolvable.
bool read_metric(const SessionState& state, const Catalog& catalog, const std::string& metric,
                 const std::string& scope, const MetricContext& ctx, std::int64_t* out);

// Every accepted metric name, for diagnostics. Parameterised families appear as
// "stock_*" and "available_*".
const std::vector<std::string>& metric_whitelist();

// True when `metric` is readable in `scope`. Used by catalog validation so a typo
// fails to load rather than throwing the first time a condition is evaluated.
bool is_known_metric(const Catalog& catalog, const std::string& metric, const std::string& scope);

}  // namespace expansion
