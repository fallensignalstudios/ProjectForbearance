#include "expansion/metrics.hpp"

#include "sim_internal.hpp"

namespace expansion {

namespace {

const std::vector<std::string> kWhitelist = {
    // planet scope
    "population", "workers_total", "workers_reserve", "health_bp", "stability_bp", "fatigue_bp",
    "food_fulfilment_bp", "water_fulfilment_bp", "power_fulfilment_bp", "housing_fulfilment_bp",
    "clinic_coverage_bp", "survival_emergency_days", "colonised", "housing_capacity", "clinic_capacity",
    "free_slots",
    // planet scope, parameterised: "stock_<resource>", "available_<resource>"
    "stock_*", "available_*",
    // facility scope
    "condition_bp", "assigned_workers", "actual_throughput_bp", "desired_throughput_bp", "operating",
    // sector scope
    "day", "adherence", "missed_colonial_food_manifests", "mandate_issued", "mandate_resolved",
    "mandate_days_remaining", "colony_launched", "colony_founded",
};

bool resource_suffix_metric(const Catalog& cat, const std::string& metric, const char* prefix, int* resource_out) {
  const std::size_t n = std::string(prefix).size();
  if (metric.size() <= n || metric.compare(0, n, prefix) != 0) return false;
  int idx = cat.find_resource(metric.substr(n));
  if (idx < 0) return false;
  *resource_out = idx;
  return true;
}

}  // namespace

const std::vector<std::string>& metric_whitelist() { return kWhitelist; }

bool is_known_metric(const Catalog& cat, const std::string& metric, const std::string& scope) {
  static const std::vector<std::string> kSector = {
      "day", "adherence", "missed_colonial_food_manifests", "mandate_issued", "mandate_resolved",
      "mandate_days_remaining", "colony_launched", "colony_founded"};
  static const std::vector<std::string> kFacility = {"condition_bp", "assigned_workers", "actual_throughput_bp",
                                                     "desired_throughput_bp", "operating"};
  static const std::vector<std::string> kPlanet = {
      "population", "workers_total", "workers_reserve", "health_bp", "stability_bp", "fatigue_bp",
      "food_fulfilment_bp", "water_fulfilment_bp", "power_fulfilment_bp", "housing_fulfilment_bp",
      "clinic_coverage_bp", "survival_emergency_days", "colonised", "housing_capacity", "clinic_capacity",
      "free_slots"};
  const std::vector<std::string>* set = nullptr;
  if (scope == "sector") {
    set = &kSector;
  } else if (scope == "facility") {
    set = &kFacility;
  } else if (scope == "planet") {
    set = &kPlanet;
  } else {
    return false;
  }
  for (const auto& name : *set) {
    if (name == metric) return true;
  }
  if (scope != "planet") return false;
  int resource = -1;
  return resource_suffix_metric(cat, metric, "stock_", &resource) ||
         resource_suffix_metric(cat, metric, "available_", &resource);
}

bool read_metric(const SessionState& state, const Catalog& cat, const std::string& metric, const std::string& scope,
                 const MetricContext& ctx, std::int64_t* out) {
  if (scope == "sector") {
    if (metric == "day") return *out = state.day, true;
    if (metric == "adherence") return *out = state.political.adherence, true;
    if (metric == "missed_colonial_food_manifests")
      return *out = state.history.missed_colonial_food_manifests, true;
    if (metric == "mandate_issued") return *out = state.demand.issued ? 1 : 0, true;
    if (metric == "mandate_resolved") return *out = state.demand.resolved ? 1 : 0, true;
    if (metric == "mandate_days_remaining") {
      *out = state.demand.issued ? (state.demand.deadline_day - state.day) : 0;
      return true;
    }
    if (metric == "colony_launched") return *out = state.colonisation.launched ? 1 : 0, true;
    if (metric == "colony_founded") return *out = state.colonisation.founded ? 1 : 0, true;
    return false;
  }

  if (scope == "facility") {
    const FacilityState* f = state.find_facility(ctx.facility_id);
    if (f == nullptr) return false;
    if (metric == "condition_bp") return *out = f->condition_bp, true;
    if (metric == "assigned_workers") return *out = f->assigned_workers, true;
    if (metric == "actual_throughput_bp") return *out = f->last_explanation.actual_throughput_bp, true;
    if (metric == "desired_throughput_bp") return *out = f->last_explanation.desired_throughput_bp, true;
    if (metric == "operating") {
      *out = (f->lifecycle == FacilityLifecycle::Active && !f->idle && !f->service.has_value() &&
              f->last_explanation.actual_throughput_bp > 0)
                 ? 1
                 : 0;
      return true;
    }
    return false;
  }

  if (scope != "planet") return false;
  const PlanetState* p = state.find_planet(ctx.planet_id);
  if (p == nullptr) return false;
  if (metric == "population") return *out = p->population, true;
  if (metric == "workers_total") return *out = p->workers_total, true;
  if (metric == "workers_reserve") return *out = sim::workers_reserve(state, *p), true;
  if (metric == "health_bp") return *out = p->health_bp, true;
  if (metric == "stability_bp") return *out = p->stability_bp, true;
  if (metric == "fatigue_bp") return *out = p->fatigue_bp, true;
  if (metric == "food_fulfilment_bp") return *out = p->last_day.food_fulfilment_bp, true;
  if (metric == "water_fulfilment_bp") return *out = p->last_day.water_fulfilment_bp, true;
  if (metric == "power_fulfilment_bp") return *out = p->last_day.power_fulfilment_bp, true;
  if (metric == "housing_fulfilment_bp") return *out = p->last_day.housing_fulfilment_bp, true;
  if (metric == "clinic_coverage_bp") return *out = p->last_day.clinic_coverage_bp, true;
  if (metric == "survival_emergency_days") return *out = p->survival_emergency_days, true;
  if (metric == "colonised") return *out = p->colonised ? 1 : 0, true;
  if (metric == "housing_capacity") return *out = p->last_day.housing_capacity, true;
  if (metric == "clinic_capacity") return *out = p->last_day.clinic_capacity, true;
  if (metric == "free_slots") {
    *out = cat.planet(p->planet_id).slot_count - sim::used_slots(state, p->planet_id);
    return true;
  }
  int resource = -1;
  if (resource_suffix_metric(cat, metric, "stock_", &resource)) {
    *out = p->inventory.on_hand.at(static_cast<std::size_t>(resource));
    return true;
  }
  if (resource_suffix_metric(cat, metric, "available_", &resource)) {
    *out = p->inventory.available(resource);
    return true;
  }
  return false;
}

namespace sim {

namespace {
bool compare(CompareOp op, std::int64_t a, std::int64_t b) {
  switch (op) {
    case CompareOp::Lt: return a < b;
    case CompareOp::Lte: return a <= b;
    case CompareOp::Gt: return a > b;
    case CompareOp::Gte: return a >= b;
    case CompareOp::Eq: return a == b;
    case CompareOp::Neq: return a != b;
  }
  return false;
}
}  // namespace

bool evaluate_condition(const SessionState& state, const Catalog& cat, const ConditionNode& node,
                        const MetricContext& ctx) {
  switch (node.kind) {
    case ConditionKind::All:
      for (const auto& c : node.children) {
        if (!evaluate_condition(state, cat, c, ctx)) return false;
      }
      return true;
    case ConditionKind::Any:
      for (const auto& c : node.children) {
        if (evaluate_condition(state, cat, c, ctx)) return true;
      }
      return false;
    case ConditionKind::Not:
      return !evaluate_condition(state, cat, node.children.front(), ctx);
    case ConditionKind::CompareMetric: {
      std::int64_t value = 0;
      if (!read_metric(state, cat, node.metric, node.scope, ctx, &value)) {
        throw SimError("condition: metric '" + node.metric + "' is not readable in scope '" + node.scope + "'");
      }
      return compare(node.op, value, node.value);
    }
    case ConditionKind::HasFlag: {
      auto it = state.flags.find(node.key);
      return it != state.flags.end() && it->second;
    }
    case ConditionKind::DaysSince: {
      auto it = state.flag_days.find(node.key);
      if (it == state.flag_days.end()) return false;
      return compare(node.op, state.day - it->second, node.value);
    }
    case ConditionKind::HasActiveEffect: {
      const std::vector<ActiveModifier>* mods = nullptr;
      if (node.scope == "facility") {
        const FacilityState* f = state.find_facility(ctx.facility_id);
        if (f == nullptr) return false;
        mods = &f->modifiers;
      } else if (node.scope == "planet") {
        const PlanetState* p = state.find_planet(ctx.planet_id);
        if (p == nullptr) return false;
        mods = &p->modifiers;
      } else {
        mods = &state.political.modifiers;
      }
      for (const auto& m : *mods) {
        if (m.tag != node.key) continue;
        if (m.expires_day >= 0 && state.day > m.expires_day) continue;
        return true;
      }
      return false;
    }
  }
  return false;
}

}  // namespace sim
}  // namespace expansion
