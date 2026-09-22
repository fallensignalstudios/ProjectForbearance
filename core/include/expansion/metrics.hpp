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

// Every accepted metric name, for catalog validation and diagnostics.
const std::vector<std::string>& metric_whitelist();

}  // namespace expansion
