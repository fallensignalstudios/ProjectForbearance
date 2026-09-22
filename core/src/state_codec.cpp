#include "expansion/state_codec.hpp"

#include <algorithm>

#include "expansion/reasons.hpp"

namespace expansion {
namespace {

using json::Value;

Value enc_resource_map(const ResourceMap& m, const Catalog& cat) {
  Value out = Value::object({});
  for (const auto& [idx, qty] : m) out.set(cat.resource(idx).id, json::dec(qty));
  return out;
}

ResourceMap dec_resource_map(const Value& v, const Catalog& cat, const std::string& ctx) {
  ResourceMap out;
  if (!v.is_object()) throw SimError("save: " + ctx + ": expected an object");
  for (const auto& [key, val] : v.as_object()) {
    int idx = cat.find_resource(key);
    if (idx < 0) throw SimError("save: " + ctx + ": unknown resource '" + key + "'");
    if (!val.is_string()) throw SimError("save: " + ctx + ": quantities are decimal strings");
    out[idx] = parse_decimal_string(val.as_string());
  }
  return out;
}

Value enc_milli_vector(const std::vector<Milli>& v) {
  Value out = Value::array({});
  for (Milli m : v) out.push_back(json::dec(m));
  return out;
}

std::vector<Milli> dec_milli_vector(const Value& v, const std::string& ctx) {
  if (!v.is_array()) throw SimError("save: " + ctx + ": expected an array");
  std::vector<Milli> out;
  out.reserve(v.as_array().size());
  for (const auto& e : v.as_array()) {
    if (!e.is_string()) throw SimError("save: " + ctx + ": values are decimal strings");
    out.push_back(parse_decimal_string(e.as_string()));
  }
  return out;
}

Value enc_named_values(const std::vector<NamedValue>& args) {
  Value out = Value::array({});
  for (const auto& a : args) {
    Value e = Value::object({});
    e.set("key", Value::string(a.key));
    e.set("value", json::dec(a.value));
    out.push_back(e);
  }
  return out;
}

std::vector<NamedValue> dec_named_values(const Value& v, const std::string& ctx) {
  std::vector<NamedValue> out;
  if (!v.is_array()) throw SimError("save: " + ctx + ": expected an array");
  for (const auto& e : v.as_array()) {
    NamedValue nv;
    nv.key = e.require_string("key", ctx);
    nv.value = e.require_decimal("value", ctx);
    out.push_back(nv);
  }
  return out;
}

Value enc_strings(const std::vector<std::string>& v) {
  Value out = Value::array({});
  for (const auto& s : v) out.push_back(Value::string(s));
  return out;
}

std::vector<std::string> dec_strings(const Value& v, const std::string& ctx) {
  std::vector<std::string> out;
  if (!v.is_array()) throw SimError("save: " + ctx + ": expected an array");
  for (const auto& e : v.as_array()) {
    if (!e.is_string()) throw SimError("save: " + ctx + ": expected strings");
    out.push_back(e.as_string());
  }
  return out;
}

Value enc_ids(const std::vector<InstanceId>& v) {
  Value out = Value::array({});
  for (InstanceId id : v) out.push_back(json::dec_u(id));
  return out;
}

std::vector<InstanceId> dec_ids(const Value& v, const std::string& ctx) {
  std::vector<InstanceId> out;
  if (!v.is_array()) throw SimError("save: " + ctx + ": expected an array");
  for (const auto& e : v.as_array()) {
    if (!e.is_string()) throw SimError("save: " + ctx + ": identifiers are decimal strings");
    out.push_back(parse_decimal_string_u(e.as_string()));
  }
  return out;
}

Value enc_int_map(const std::map<std::string, Day>& m) {
  Value out = Value::object({});
  for (const auto& [key, value] : m) out.set(key, json::dec(value));
  return out;
}

Value enc_int_map(const std::map<std::string, int>& m) {
  Value out = Value::object({});
  for (const auto& [key, value] : m) out.set(key, json::dec(value));
  return out;
}

Value enc_modifiers(const std::vector<ActiveModifier>& mods) {
  Value out = Value::array({});
  for (const auto& m : mods) {
    Value e = Value::object({});
    e.set("id", json::dec_u(m.id));
    e.set("tag", Value::string(m.tag));
    e.set("target", Value::string(modifier_target_id(m.target)));
    e.set("factor_bp", json::dec(m.factor_bp));
    e.set("amount", json::dec(m.amount));
    e.set("expires_day", json::dec(m.expires_day));
    e.set("source_event_id", Value::string(m.source_event_id));
    e.set("source_event_instance", json::dec_u(m.source_event_instance));
    out.push_back(e);
  }
  return out;
}

std::vector<ActiveModifier> dec_modifiers(const Value& v, const std::string& ctx) {
  std::vector<ActiveModifier> out;
  if (!v.is_array()) throw SimError("save: " + ctx + ": expected an array");
  for (const auto& e : v.as_array()) {
    ActiveModifier m;
    m.id = e.require_decimal_u("id", ctx);
    m.tag = e.require_string("tag", ctx);
    auto target = parse_modifier_target(e.require_string("target", ctx));
    if (!target) throw SimError("save: " + ctx + ": unknown modifier target");
    m.target = *target;
    m.factor_bp = e.require_decimal("factor_bp", ctx);
    m.amount = e.require_decimal("amount", ctx);
    m.expires_day = e.require_decimal("expires_day", ctx);
    m.source_event_id = e.require_string("source_event_id", ctx);
    m.source_event_instance = e.require_decimal_u("source_event_instance", ctx);
    out.push_back(m);
  }
  std::sort(out.begin(), out.end(), [](const ActiveModifier& a, const ActiveModifier& b) { return a.id < b.id; });
  return out;
}

Value enc_explanation(const FacilityExplanation& x, const Catalog& cat) {
  Value e = Value::object({});
  e.set("day", json::dec(x.day));
  e.set("desired_throughput_bp", json::dec(x.desired_throughput_bp));
  e.set("actual_throughput_bp", json::dec(x.actual_throughput_bp));
  e.set("staffing_bp", json::dec(x.staffing_bp));
  e.set("health_bp", json::dec(x.health_bp));
  e.set("fatigue_bp", json::dec(x.fatigue_bp));
  e.set("condition_bp", json::dec(x.condition_bp));
  e.set("faction_bp", json::dec(x.faction_bp));
  e.set("policy_bp", json::dec(x.policy_bp));
  e.set("effects_bp", json::dec(x.effects_bp));
  e.set("power_requested", json::dec(x.power_requested));
  e.set("power_granted", json::dec(x.power_granted));
  e.set("inputs_consumed", enc_resource_map(x.inputs_consumed, cat));
  e.set("outputs_produced", enc_resource_map(x.outputs_produced, cat));
  Value missing = Value::array({});
  for (int r : x.missing_inputs) missing.push_back(Value::string(cat.resource(r).id));
  e.set("missing_inputs", missing);
  Value blocked = Value::array({});
  for (int r : x.blocked_outputs) blocked.push_back(Value::string(cat.resource(r).id));
  e.set("blocked_outputs", blocked);
  e.set("power_limited", Value::boolean(x.power_limited));
  e.set("labour_limited", Value::boolean(x.labour_limited));
  e.set("idle", Value::boolean(x.idle));
  e.set("servicing", Value::boolean(x.servicing));
  e.set("modifier_tags", enc_strings(x.modifier_tags));
  e.set("primary_reason", Value::string(x.primary_reason));
  return e;
}

FacilityExplanation dec_explanation(const Value& v, const Catalog& cat, const std::string& ctx) {
  FacilityExplanation x;
  x.day = v.require_decimal("day", ctx);
  x.desired_throughput_bp = v.require_decimal("desired_throughput_bp", ctx);
  x.actual_throughput_bp = v.require_decimal("actual_throughput_bp", ctx);
  x.staffing_bp = v.require_decimal("staffing_bp", ctx);
  x.health_bp = v.require_decimal("health_bp", ctx);
  x.fatigue_bp = v.require_decimal("fatigue_bp", ctx);
  x.condition_bp = v.require_decimal("condition_bp", ctx);
  x.faction_bp = v.require_decimal("faction_bp", ctx);
  x.policy_bp = v.require_decimal("policy_bp", ctx);
  x.effects_bp = v.require_decimal("effects_bp", ctx);
  x.power_requested = v.require_decimal("power_requested", ctx);
  x.power_granted = v.require_decimal("power_granted", ctx);
  x.inputs_consumed = dec_resource_map(v.require("inputs_consumed", ctx), cat, ctx);
  x.outputs_produced = dec_resource_map(v.require("outputs_produced", ctx), cat, ctx);
  for (const auto& s : dec_strings(v.require("missing_inputs", ctx), ctx)) x.missing_inputs.push_back(cat.resource_index(s));
  for (const auto& s : dec_strings(v.require("blocked_outputs", ctx), ctx)) x.blocked_outputs.push_back(cat.resource_index(s));
  x.power_limited = v.bool_or("power_limited", false);
  x.labour_limited = v.bool_or("labour_limited", false);
  x.idle = v.bool_or("idle", false);
  x.servicing = v.bool_or("servicing", false);
  x.modifier_tags = dec_strings(v.require("modifier_tags", ctx), ctx);
  x.primary_reason = v.require_string("primary_reason", ctx);
  return x;
}

Value enc_power(const PowerReport& p) {
  Value e = Value::object({});
  e.set("generated", json::dec(p.generated));
  e.set("residential_demand", json::dec(p.residential_demand));
  e.set("residential_served", json::dec(p.residential_served));
  e.set("facility_granted", json::dec(p.facility_granted));
  e.set("construction_granted", json::dec(p.construction_granted));
  e.set("used", json::dec(p.used));
  e.set("unused", json::dec(p.unused));
  return e;
}

PowerReport dec_power(const Value& v, const std::string& ctx) {
  PowerReport p;
  p.generated = v.require_decimal("generated", ctx);
  p.residential_demand = v.require_decimal("residential_demand", ctx);
  p.residential_served = v.require_decimal("residential_served", ctx);
  p.facility_granted = v.require_decimal("facility_granted", ctx);
  p.construction_granted = v.require_decimal("construction_granted", ctx);
  p.used = v.require_decimal("used", ctx);
  p.unused = v.require_decimal("unused", ctx);
  return p;
}

Value enc_daily(const DailyReport& d) {
  Value e = Value::object({});
  e.set("day", json::dec(d.day));
  e.set("food_fulfilment_bp", json::dec(d.food_fulfilment_bp));
  e.set("water_fulfilment_bp", json::dec(d.water_fulfilment_bp));
  e.set("power_fulfilment_bp", json::dec(d.power_fulfilment_bp));
  e.set("housing_fulfilment_bp", json::dec(d.housing_fulfilment_bp));
  e.set("clinic_coverage_bp", json::dec(d.clinic_coverage_bp));
  e.set("food_demand", json::dec(d.food_demand));
  e.set("food_served", json::dec(d.food_served));
  e.set("water_demand", json::dec(d.water_demand));
  e.set("water_served", json::dec(d.water_served));
  e.set("housing_capacity", json::dec(d.housing_capacity));
  e.set("clinic_capacity", json::dec(d.clinic_capacity));
  e.set("power", enc_power(d.power));
  e.set("health_target_bp", json::dec(d.health_target_bp));
  e.set("stability_target_bp", json::dec(d.stability_target_bp));
  e.set("maintenance_machinery_paid", json::dec(d.maintenance_machinery_paid));
  e.set("maintenance_machinery_due", json::dec(d.maintenance_machinery_due));
  e.set("opening_stock", enc_milli_vector(d.opening_stock));
  e.set("closing_stock", enc_milli_vector(d.closing_stock));
  e.set("produced", enc_milli_vector(d.produced));
  e.set("consumed", enc_milli_vector(d.consumed));
  e.set("port_handling_capacity", json::dec(d.port_handling_capacity));
  e.set("port_handling_used", json::dec(d.port_handling_used));
  e.set("workers_assigned", json::dec(d.workers_assigned));
  e.set("workers_transitioning", json::dec(d.workers_transitioning));
  e.set("workers_reserve", json::dec(d.workers_reserve));
  e.set("workers_crew", json::dec(d.workers_crew));
  return e;
}

DailyReport dec_daily(const Value& v, const std::string& ctx) {
  DailyReport d;
  d.day = v.require_decimal("day", ctx);
  d.food_fulfilment_bp = v.require_decimal("food_fulfilment_bp", ctx);
  d.water_fulfilment_bp = v.require_decimal("water_fulfilment_bp", ctx);
  d.power_fulfilment_bp = v.require_decimal("power_fulfilment_bp", ctx);
  d.housing_fulfilment_bp = v.require_decimal("housing_fulfilment_bp", ctx);
  d.clinic_coverage_bp = v.require_decimal("clinic_coverage_bp", ctx);
  d.food_demand = v.require_decimal("food_demand", ctx);
  d.food_served = v.require_decimal("food_served", ctx);
  d.water_demand = v.require_decimal("water_demand", ctx);
  d.water_served = v.require_decimal("water_served", ctx);
  d.housing_capacity = v.require_decimal("housing_capacity", ctx);
  d.clinic_capacity = v.require_decimal("clinic_capacity", ctx);
  d.power = dec_power(v.require("power", ctx), ctx);
  d.health_target_bp = v.require_decimal("health_target_bp", ctx);
  d.stability_target_bp = v.require_decimal("stability_target_bp", ctx);
  d.maintenance_machinery_paid = v.require_decimal("maintenance_machinery_paid", ctx);
  d.maintenance_machinery_due = v.require_decimal("maintenance_machinery_due", ctx);
  d.opening_stock = dec_milli_vector(v.require("opening_stock", ctx), ctx);
  d.closing_stock = dec_milli_vector(v.require("closing_stock", ctx), ctx);
  d.produced = dec_milli_vector(v.require("produced", ctx), ctx);
  d.consumed = dec_milli_vector(v.require("consumed", ctx), ctx);
  d.port_handling_capacity = v.require_decimal("port_handling_capacity", ctx);
  d.port_handling_used = v.require_decimal("port_handling_used", ctx);
  d.workers_assigned = static_cast<int>(v.require_decimal("workers_assigned", ctx));
  d.workers_transitioning = static_cast<int>(v.require_decimal("workers_transitioning", ctx));
  d.workers_reserve = static_cast<int>(v.require_decimal("workers_reserve", ctx));
  d.workers_crew = static_cast<int>(v.require_decimal("workers_crew", ctx));
  return d;
}

}  // namespace

json::Value encode_state(const SessionState& s, const Catalog& cat) {
  Value root = Value::object({});
  root.set("schema_version", Value::integer(kStateSchemaVersion));
  root.set("simulation_version", Value::string(s.simulation_version));
  root.set("catalog_hash", Value::string(s.catalog_hash));
  root.set("scenario_id", Value::string(s.scenario_id));
  root.set("faction_id", Value::string(s.faction_id));
  root.set("day", json::dec(s.day));
  root.set("revision", json::dec_u(s.revision));
  root.set("command_sequence", json::dec_u(s.command_sequence));
  root.set("next_instance_id", json::dec_u(s.next_instance_id));
  root.set("seed", json::dec_u(s.seed));
  root.set("lifecycle", Value::string(session_lifecycle_id(s.lifecycle)));
  root.set("paused_for_decision", Value::boolean(s.paused_for_decision));
  root.set("decisions_opened_today", Value::integer(s.decisions_opened_today));

  Value planets = Value::array({});
  for (const auto& p : s.planets) {
    Value e = Value::object({});
    e.set("planet_id", Value::string(p.planet_id));
    e.set("colonised", Value::boolean(p.colonised));
    e.set("population", json::dec(p.population));
    e.set("workers_total", json::dec(p.workers_total));
    e.set("on_hand", enc_milli_vector(p.inventory.on_hand));
    e.set("capacity_per_resource", json::dec(p.inventory.capacity_per_resource));
    Value reservations = Value::array({});
    for (const auto& r : p.inventory.reservations) {
      Value x = Value::object({});
      x.set("id", json::dec_u(r.id));
      x.set("resource", Value::string(cat.resource(r.resource).id));
      x.set("quantity", json::dec(r.quantity));
      x.set("owner_kind", Value::string(r.owner_kind));
      x.set("owner_id", json::dec_u(r.owner_id));
      x.set("reason", Value::string(r.reason));
      reservations.push_back(x);
    }
    e.set("reservations", reservations);
    Value claims = Value::array({});
    for (const auto& c : p.inventory.incoming_claims) {
      Value x = Value::object({});
      x.set("id", json::dec_u(c.id));
      x.set("resource", Value::string(cat.resource(c.resource).id));
      x.set("quantity", json::dec(c.quantity));
      x.set("owner_id", json::dec_u(c.owner_id));
      claims.push_back(x);
    }
    e.set("incoming_claims", claims);
    e.set("health_bp", json::dec(p.health_bp));
    e.set("fatigue_bp", json::dec(p.fatigue_bp));
    e.set("stability_bp", json::dec(p.stability_bp));
    e.set("policy_id", Value::string(p.policy_id));
    e.set("policy_started_day", json::dec(p.policy_started_day));
    e.set("policy_effective_days", json::dec(p.policy_effective_days));
    e.set("policy_cooldown_until", enc_int_map(p.policy_cooldown_until));
    e.set("food_emergency_streak", Value::integer(p.food_emergency_streak));
    e.set("water_emergency_streak", Value::integer(p.water_emergency_streak));
    e.set("survival_emergency_days", Value::integer(p.survival_emergency_days));
    e.set("survival_emergency_active", Value::boolean(p.survival_emergency_active));
    e.set("survival_emergency_opened_day", json::dec(p.survival_emergency_opened_day));
    e.set("relief_pending_day", json::dec(p.relief_pending_day));
    e.set("relief_used", Value::boolean(p.relief_used));
    e.set("ship_crew_reserved", Value::integer(p.ship_crew_reserved));
    e.set("modifiers", enc_modifiers(p.modifiers));
    Value shortages = Value::object({});
    for (const auto& [idx, t] : p.shortages) {
      Value x = Value::object({});
      x.set("missed_days", Value::integer(t.missed_days));
      x.set("fulfilled_days", Value::integer(t.fulfilled_days));
      x.set("open", Value::boolean(t.open));
      shortages.set(cat.resource(idx).id, x);
    }
    e.set("shortages", shortages);
    e.set("last_day", enc_daily(p.last_day));
    e.set("clean_day_streak", Value::integer(p.clean_day_streak));
    Value highs = Value::object({});
    for (const auto& [idx, v] : p.milestone_high) highs.set(cat.resource(idx).id, json::dec(v));
    e.set("milestone_high", highs);
    Value high_days = Value::object({});
    for (const auto& [idx, v] : p.milestone_last_news_day) high_days.set(cat.resource(idx).id, json::dec(v));
    e.set("milestone_last_news_day", high_days);
    planets.push_back(e);
  }
  root.set("planets", planets);

  Value facilities = Value::array({});
  for (const auto& f : s.facilities) {
    Value e = Value::object({});
    e.set("id", json::dec_u(f.id));
    e.set("facility_id", Value::string(f.facility_id));
    e.set("recipe_id", Value::string(f.recipe_id));
    e.set("planet_id", Value::string(f.planet_id));
    e.set("lifecycle", Value::string(facility_lifecycle_id(f.lifecycle)));
    e.set("assigned_workers", Value::integer(f.assigned_workers));
    e.set("condition_bp", json::dec(f.condition_bp));
    e.set("priority_band", Value::integer(f.priority_band));
    e.set("idle", Value::boolean(f.idle));
    e.set("activates_day", json::dec(f.activates_day));
    e.set("low_condition_streak", Value::integer(f.low_condition_streak));
    e.set("last_service_completed_day", json::dec(f.last_service_completed_day));
    if (f.construction.has_value()) {
      Value j = Value::object({});
      j.set("work_total", json::dec(f.construction->work_total));
      j.set("work_done", json::dec(f.construction->work_done));
      j.set("escrow", enc_resource_map(f.construction->escrow, cat));
      j.set("original_cost", enc_resource_map(f.construction->original_cost, cat));
      j.set("consumed", enc_resource_map(f.construction->consumed, cat));
      j.set("assigned_workers", Value::integer(f.construction->assigned_workers));
      j.set("started_day", json::dec(f.construction->started_day));
      j.set("days_elapsed", json::dec(f.construction->days_elapsed));
      j.set("blockage", Value::string(f.construction->blockage));
      e.set("construction", j);
    }
    if (f.service.has_value()) {
      Value j = Value::object({});
      j.set("started_day", json::dec(f.service->started_day));
      j.set("ready_day", json::dec(f.service->ready_day));
      e.set("service", j);
    }
    e.set("modifiers", enc_modifiers(f.modifiers));
    e.set("last_explanation", enc_explanation(f.last_explanation, cat));
    facilities.push_back(e);
  }
  root.set("facilities", facilities);

  Value transfers = Value::array({});
  for (const auto& t : s.transfers) {
    Value e = Value::object({});
    e.set("id", json::dec_u(t.id));
    e.set("planet_id", Value::string(t.planet_id));
    e.set("from_facility", json::dec_u(t.from_facility));
    e.set("to_facility", json::dec_u(t.to_facility));
    e.set("count", Value::integer(t.count));
    e.set("ready_day", json::dec(t.ready_day));
    transfers.push_back(e);
  }
  root.set("transfers", transfers);

  {
    const ShipState& sh = s.ship;
    Value e = Value::object({});
    e.set("id", json::dec_u(sh.id));
    e.set("phase", Value::string(ship_phase_id(sh.phase)));
    e.set("mission", Value::string(ship_mission_id(sh.mission)));
    e.set("location_planet", Value::string(sh.location_planet));
    e.set("origin_planet", Value::string(sh.origin_planet));
    e.set("destination_planet", Value::string(sh.destination_planet));
    e.set("departure_day", json::dec(sh.departure_day));
    e.set("arrival_day", json::dec(sh.arrival_day));
    e.set("earliest_departure_day", json::dec(sh.earliest_departure_day));
    e.set("cargo", enc_resource_map(sh.cargo, cat));
    e.set("tank", json::dec(sh.tank));
    e.set("crew", Value::integer(sh.crew));
    e.set("crew_home_planet", Value::string(sh.crew_home_planet));
    e.set("booked_manifest", enc_resource_map(sh.booked_manifest, cat));
    e.set("booking_reservations", enc_ids(sh.booking_reservations));
    e.set("booking_claims", enc_ids(sh.booking_claims));
    e.set("unloaded_so_far", enc_resource_map(sh.unloaded_so_far, cat));
    e.set("shipment_id", json::dec_u(sh.shipment_id));
    e.set("mission_delivery_day", json::dec(sh.mission_delivery_day));
    e.set("mission_return_departure_day", json::dec(sh.mission_return_departure_day));
    e.set("mission_delivered", Value::boolean(sh.mission_delivered));
    root.set("ship", e);
  }
  {
    const RoutePlan& r = s.route;
    Value e = Value::object({});
    e.set("id", Value::string(r.id));
    e.set("origin_planet", Value::string(r.origin_planet));
    e.set("destination_planet", Value::string(r.destination_planet));
    e.set("outbound_targets", enc_resource_map(r.outbound_targets, cat));
    e.set("return_targets", enc_resource_map(r.return_targets, cat));
    Value floors = Value::object({});
    for (const auto& [idx, qty] : r.source_floors) floors.set(cat.resource(idx).id, json::dec(qty));
    e.set("source_floors", floors);
    Value overrides = Value::array({});
    for (int idx : r.floor_overridden) overrides.push_back(Value::string(cat.resource(idx).id));
    e.set("floor_overridden", overrides);
    e.set("enabled", Value::boolean(r.enabled));
    e.set("next_departure_day", json::dec(r.next_departure_day));
    e.set("departure_interval_days", json::dec(r.departure_interval_days));
    e.set("departure_requested", Value::boolean(r.departure_requested));
    e.set("departure_allow_empty", Value::boolean(r.departure_allow_empty));
    root.set("route", e);
  }
  {
    const ColonizationState& c = s.colonisation;
    Value e = Value::object({});
    e.set("launched", Value::boolean(c.launched));
    e.set("founded", Value::boolean(c.founded));
    e.set("expedition_id", json::dec_u(c.expedition_id));
    e.set("target_planet", Value::string(c.target_planet));
    e.set("residents", json::dec(c.residents));
    e.set("workers", json::dec(c.workers));
    e.set("cargo", enc_resource_map(c.cargo, cat));
    e.set("consumed_transit", enc_resource_map(c.consumed_transit, cat));
    e.set("launch_day", json::dec(c.launch_day));
    e.set("arrival_day", json::dec(c.arrival_day));
    e.set("founded_day", json::dec(c.founded_day));
    root.set("colonisation", e);
  }
  {
    const PoliticalState& p = s.political;
    Value e = Value::object({});
    e.set("faction_id", Value::string(p.faction_id));
    e.set("adherence", Value::integer(p.adherence));
    Value resolved = Value::array({});
    for (const auto& k : p.resolved_choices) resolved.push_back(Value::string(k));
    e.set("resolved_choices", resolved);
    e.set("low_adherence_streak", Value::integer(p.low_adherence_streak));
    e.set("faction_review_cooldown_until", json::dec(p.faction_review_cooldown_until));
    e.set("modifiers", enc_modifiers(p.modifiers));
    root.set("political", e);
  }

  Value events = Value::array({});
  for (const auto& i : s.events) {
    Value e = Value::object({});
    e.set("id", json::dec_u(i.id));
    e.set("event_id", Value::string(i.event_id));
    e.set("chain_id", Value::string(i.chain_id));
    e.set("planet_id", Value::string(i.planet_id));
    e.set("facility_id", json::dec_u(i.facility_id));
    e.set("opened_day", json::dec(i.opened_day));
    e.set("deadline_day", json::dec(i.deadline_day));
    e.set("resolution", Value::string(event_resolution_id(i.resolution)));
    e.set("chosen_choice", Value::string(i.chosen_choice));
    e.set("chosen_day", json::dec(i.chosen_day));
    e.set("trigger_facts", enc_ids(i.trigger_facts));
    events.push_back(e);
  }
  root.set("events", events);

  Value scheduled = Value::array({});
  for (const auto& x : s.scheduled) {
    Value e = Value::object({});
    e.set("id", json::dec_u(x.id));
    e.set("due_day", json::dec(x.due_day));
    e.set("priority", Value::integer(x.priority));
    e.set("event_id", Value::string(x.event_id));
    e.set("choice_id", Value::string(x.choice_id));
    e.set("list_name", Value::string(x.list_name));
    e.set("effect_index", Value::integer(x.effect_index));
    e.set("nested_index", Value::integer(x.nested_index));
    e.set("planet_id", Value::string(x.planet_id));
    e.set("facility_id", json::dec_u(x.facility_id));
    e.set("source_event_instance", json::dec_u(x.source_event_instance));
    scheduled.push_back(e);
  }
  root.set("scheduled", scheduled);

  Value requests = Value::array({});
  for (const auto& r : s.event_requests) {
    Value e = Value::object({});
    e.set("id", json::dec_u(r.id));
    e.set("event_id", Value::string(r.event_id));
    e.set("planet_id", Value::string(r.planet_id));
    e.set("facility_id", json::dec_u(r.facility_id));
    e.set("requested_day", json::dec(r.requested_day));
    e.set("trigger_facts", enc_ids(r.trigger_facts));
    requests.push_back(e);
  }
  root.set("event_requests", requests);

  {
    const DemandState& d = s.demand;
    Value e = Value::object({});
    e.set("issued", Value::boolean(d.issued));
    e.set("issue_day", json::dec(d.issue_day));
    e.set("deadline_day", json::dec(d.deadline_day));
    e.set("required", enc_resource_map(d.required, cat));
    e.set("delivered", enc_resource_map(d.delivered, cat));
    e.set("negotiated", Value::boolean(d.negotiated));
    e.set("resolved", Value::boolean(d.resolved));
    e.set("decision", Value::string(mandate_decision_id(d.decision)));
    Value missions = Value::array({});
    for (const auto& m : d.missions) {
      Value x = Value::object({});
      x.set("id", json::dec_u(m.id));
      x.set("departure_day", json::dec(m.departure_day));
      x.set("delivery_day", json::dec(m.delivery_day));
      x.set("manifest", enc_resource_map(m.manifest, cat));
      x.set("delivered", Value::boolean(m.delivered));
      missions.push_back(x);
    }
    e.set("missions", missions);
    e.set("freighter_committed", Value::boolean(d.freighter_committed));
    e.set("pending_manifest", enc_resource_map(d.pending_manifest, cat));
    e.set("mission_requested", Value::boolean(d.mission_requested));
    e.set("mission_requested_day", json::dec(d.mission_requested_day));
    root.set("demand", e);
  }
  {
    const HistoryState& h = s.history;
    Value e = Value::object({});
    Value samples = Value::array({});
    for (const auto& x : h.samples) {
      Value y = Value::object({});
      y.set("day", json::dec(x.day));
      y.set("planet_id", Value::string(x.planet_id));
      y.set("health_bp", json::dec(x.health_bp));
      y.set("stability_bp", json::dec(x.stability_bp));
      y.set("fatigue_bp", json::dec(x.fatigue_bp));
      y.set("food_fulfilment_bp", json::dec(x.food_fulfilment_bp));
      y.set("water_fulfilment_bp", json::dec(x.water_fulfilment_bp));
      y.set("power_fulfilment_bp", json::dec(x.power_fulfilment_bp));
      y.set("closing_stock", enc_milli_vector(x.closing_stock));
      samples.push_back(y);
    }
    e.set("samples", samples);
    Value weekly = Value::array({});
    for (const auto& x : h.weekly) {
      Value y = Value::object({});
      y.set("first_day", json::dec(x.first_day));
      y.set("last_day", json::dec(x.last_day));
      y.set("planet_id", Value::string(x.planet_id));
      y.set("min_health_bp", json::dec(x.min_health_bp));
      y.set("min_stability_bp", json::dec(x.min_stability_bp));
      y.set("min_food_fulfilment_bp", json::dec(x.min_food_fulfilment_bp));
      y.set("min_water_fulfilment_bp", json::dec(x.min_water_fulfilment_bp));
      weekly.push_back(y);
    }
    e.set("weekly", weekly);
    e.set("day_hashes", enc_strings(h.day_hashes));
    e.set("first_retained_hash_day", json::dec(h.first_retained_hash_day));
    e.set("missed_colonial_food_manifests", Value::integer(h.missed_colonial_food_manifests));
    Value flags = Value::array({});
    for (const auto& f : h.milestone_flags) flags.push_back(Value::string(f));
    e.set("milestone_flags", flags);
    root.set("history", e);
  }

  Value flags = Value::object({});
  for (const auto& [key, value] : s.flags) flags.set(key, Value::boolean(value));
  root.set("flags", flags);
  root.set("flag_days", enc_int_map(s.flag_days));
  root.set("cooldowns", enc_int_map(s.cooldowns));
  root.set("condition_streaks", enc_int_map(s.condition_streaks));

  Value applied = Value::object({});
  for (const auto& [key, rec] : s.applied_commands) {
    Value e = Value::object({});
    e.set("accepted", Value::boolean(rec.accepted));
    e.set("revision", json::dec_u(rec.revision));
    e.set("reason", Value::string(rec.reason));
    e.set("detail", Value::string(rec.detail));
    applied.set(key, e);
  }
  root.set("applied_commands", applied);

  Value ledger = Value::array({});
  for (const auto& t : s.ledger) {
    Value e = Value::object({});
    e.set("id", json::dec_u(t.id));
    e.set("day", json::dec(t.day));
    e.set("resource", Value::string(cat.resource(t.resource).id));
    e.set("quantity", json::dec(t.quantity));
    e.set("from_account", Value::string(t.from_account));
    e.set("to_account", Value::string(t.to_account));
    e.set("operation_id", json::dec_u(t.operation_id));
    e.set("cause", Value::string(t.cause));
    ledger.push_back(e);
  }
  root.set("ledger", ledger);

  Value facts = Value::array({});
  for (const auto& f : s.facts) {
    Value e = Value::object({});
    e.set("id", json::dec_u(f.id));
    e.set("day", json::dec(f.day));
    e.set("kind", Value::string(f.kind));
    e.set("planet_id", Value::string(f.planet_id));
    e.set("entity_id", json::dec_u(f.entity_id));
    e.set("args", enc_named_values(f.args));
    e.set("text_args", enc_strings(f.text_args));
    e.set("causal_parents", enc_ids(f.causal_parents));
    e.set("reason_id", Value::string(f.reason_id));
    e.set("dedupe_key", Value::string(f.dedupe_key));
    facts.push_back(e);
  }
  root.set("facts", facts);

  Value news = Value::array({});
  for (const auto& n : s.news) {
    Value e = Value::object({});
    e.set("id", json::dec_u(n.id));
    e.set("day", json::dec(n.day));
    e.set("template_key", Value::string(n.template_key));
    e.set("planet_id", Value::string(n.planet_id));
    e.set("args", enc_named_values(n.args));
    e.set("text_args", enc_strings(n.text_args));
    e.set("source_facts", enc_ids(n.source_facts));
    e.set("dedupe_key", Value::string(n.dedupe_key));
    e.set("priority", Value::integer(n.priority));
    e.set("detail_compacted", Value::boolean(n.detail_compacted));
    news.push_back(e);
  }
  root.set("news", news);

  return root;
}

SessionState decode_state(const json::Value& root, const Catalog& cat) {
  const std::string ctx = "state";
  if (!root.is_object()) throw SimError("save: top level must be an object");
  const std::int64_t schema = root.require_int("schema_version", ctx);
  if (schema != kStateSchemaVersion) {
    throw SimError("save: unsupported state schema version " + std::to_string(schema));
  }
  SessionState s;
  s.simulation_version = root.require_string("simulation_version", ctx);
  s.catalog_hash = root.require_string("catalog_hash", ctx);
  s.scenario_id = root.require_string("scenario_id", ctx);
  s.faction_id = root.require_string("faction_id", ctx);
  cat.scenario(s.scenario_id);
  cat.faction(s.faction_id);
  s.day = root.require_decimal("day", ctx);
  s.revision = root.require_decimal_u("revision", ctx);
  s.command_sequence = root.require_decimal_u("command_sequence", ctx);
  s.next_instance_id = root.require_decimal_u("next_instance_id", ctx);
  if (s.next_instance_id == 0) throw SimError("save: the instance id allocator cannot be zero");
  s.seed = root.require_decimal_u("seed", ctx);
  auto lifecycle = parse_session_lifecycle(root.require_string("lifecycle", ctx));
  if (!lifecycle) throw SimError("save: unknown lifecycle state");
  s.lifecycle = *lifecycle;
  s.paused_for_decision = root.bool_or("paused_for_decision", false);
  s.decisions_opened_today = static_cast<int>(root.int_or("decisions_opened_today", 0));

  for (const auto& e : root.require_array("planets", ctx)) {
    PlanetState p;
    p.planet_id = e.require_string("planet_id", ctx);
    cat.planet(p.planet_id);
    p.colonised = e.bool_or("colonised", false);
    p.population = e.require_decimal("population", ctx);
    p.workers_total = e.require_decimal("workers_total", ctx);
    p.inventory.on_hand = dec_milli_vector(e.require("on_hand", ctx), ctx);
    if (static_cast<int>(p.inventory.on_hand.size()) != cat.resource_count()) {
      throw SimError("save: planet '" + p.planet_id + "': inventory width does not match the catalog");
    }
    p.inventory.capacity_per_resource = e.require_decimal("capacity_per_resource", ctx);
    for (const auto& r : e.require_array("reservations", ctx)) {
      StockReservation x;
      x.id = r.require_decimal_u("id", ctx);
      x.resource = cat.resource_index(r.require_string("resource", ctx));
      x.quantity = r.require_decimal("quantity", ctx);
      x.owner_kind = r.require_string("owner_kind", ctx);
      x.owner_id = r.require_decimal_u("owner_id", ctx);
      x.reason = r.require_string("reason", ctx);
      p.inventory.reservations.push_back(x);
    }
    for (const auto& r : e.require_array("incoming_claims", ctx)) {
      IncomingClaim x;
      x.id = r.require_decimal_u("id", ctx);
      x.resource = cat.resource_index(r.require_string("resource", ctx));
      x.quantity = r.require_decimal("quantity", ctx);
      x.owner_id = r.require_decimal_u("owner_id", ctx);
      p.inventory.incoming_claims.push_back(x);
    }
    p.health_bp = e.require_decimal("health_bp", ctx);
    p.fatigue_bp = e.require_decimal("fatigue_bp", ctx);
    p.stability_bp = e.require_decimal("stability_bp", ctx);
    p.policy_id = e.require_string("policy_id", ctx);
    if (cat.find_policy(p.policy_id) == nullptr) throw SimError("save: unknown policy '" + p.policy_id + "'");
    p.policy_started_day = e.require_decimal("policy_started_day", ctx);
    p.policy_effective_days = e.require_decimal("policy_effective_days", ctx);
    for (const auto& [key, val] : e.require_object("policy_cooldown_until", ctx)) {
      p.policy_cooldown_until[key] = parse_decimal_string(val.as_string());
    }
    p.food_emergency_streak = static_cast<int>(e.require_int("food_emergency_streak", ctx));
    p.water_emergency_streak = static_cast<int>(e.require_int("water_emergency_streak", ctx));
    p.survival_emergency_days = static_cast<int>(e.require_int("survival_emergency_days", ctx));
    p.survival_emergency_active = e.bool_or("survival_emergency_active", false);
    p.survival_emergency_opened_day = e.require_decimal("survival_emergency_opened_day", ctx);
    p.relief_pending_day = e.require_decimal("relief_pending_day", ctx);
    p.relief_used = e.bool_or("relief_used", false);
    p.ship_crew_reserved = static_cast<int>(e.require_int("ship_crew_reserved", ctx));
    p.modifiers = dec_modifiers(e.require("modifiers", ctx), ctx);
    for (const auto& [key, val] : e.require_object("shortages", ctx)) {
      ShortageTracker t;
      t.missed_days = static_cast<int>(val.require_int("missed_days", ctx));
      t.fulfilled_days = static_cast<int>(val.require_int("fulfilled_days", ctx));
      t.open = val.bool_or("open", false);
      p.shortages[cat.resource_index(key)] = t;
    }
    p.last_day = dec_daily(e.require("last_day", ctx), ctx);
    p.clean_day_streak = static_cast<int>(e.require_int("clean_day_streak", ctx));
    for (const auto& [key, val] : e.require_object("milestone_high", ctx)) {
      p.milestone_high[cat.resource_index(key)] = parse_decimal_string(val.as_string());
    }
    for (const auto& [key, val] : e.require_object("milestone_last_news_day", ctx)) {
      p.milestone_last_news_day[cat.resource_index(key)] = parse_decimal_string(val.as_string());
    }
    s.planets.push_back(p);
  }

  for (const auto& e : root.require_array("facilities", ctx)) {
    FacilityState f;
    f.id = e.require_decimal_u("id", ctx);
    f.facility_id = e.require_string("facility_id", ctx);
    const FacilityDef& fd = cat.facility(f.facility_id);
    f.recipe_id = e.require_string("recipe_id", ctx);
    if (fd.recipe_index(f.recipe_id) < 0) throw SimError("save: facility recipe '" + f.recipe_id + "' does not exist");
    f.planet_id = e.require_string("planet_id", ctx);
    auto lc = parse_facility_lifecycle(e.require_string("lifecycle", ctx));
    if (!lc) throw SimError("save: unknown facility lifecycle");
    f.lifecycle = *lc;
    f.assigned_workers = static_cast<int>(e.require_int("assigned_workers", ctx));
    f.condition_bp = e.require_decimal("condition_bp", ctx);
    f.priority_band = static_cast<int>(e.require_int("priority_band", ctx));
    f.idle = e.bool_or("idle", false);
    f.activates_day = e.require_decimal("activates_day", ctx);
    f.low_condition_streak = static_cast<int>(e.require_int("low_condition_streak", ctx));
    f.last_service_completed_day = e.require_decimal("last_service_completed_day", ctx);
    if (const json::Value* j = e.find("construction")) {
      ConstructionJob job;
      job.work_total = j->require_decimal("work_total", ctx);
      job.work_done = j->require_decimal("work_done", ctx);
      job.escrow = dec_resource_map(j->require("escrow", ctx), cat, ctx);
      job.original_cost = dec_resource_map(j->require("original_cost", ctx), cat, ctx);
      job.consumed = dec_resource_map(j->require("consumed", ctx), cat, ctx);
      job.assigned_workers = static_cast<int>(j->require_int("assigned_workers", ctx));
      job.started_day = j->require_decimal("started_day", ctx);
      job.days_elapsed = j->require_decimal("days_elapsed", ctx);
      job.blockage = j->require_string("blockage", ctx);
      f.construction = job;
    }
    if (const json::Value* j = e.find("service")) {
      ServiceJob job;
      job.started_day = j->require_decimal("started_day", ctx);
      job.ready_day = j->require_decimal("ready_day", ctx);
      f.service = job;
    }
    f.modifiers = dec_modifiers(e.require("modifiers", ctx), ctx);
    f.last_explanation = dec_explanation(e.require("last_explanation", ctx), cat, ctx);
    s.facilities.push_back(f);
  }
  std::sort(s.facilities.begin(), s.facilities.end(),
            [](const FacilityState& a, const FacilityState& b) { return a.id < b.id; });

  for (const auto& e : root.require_array("transfers", ctx)) {
    WorkerTransfer t;
    t.id = e.require_decimal_u("id", ctx);
    t.planet_id = e.require_string("planet_id", ctx);
    t.from_facility = e.require_decimal_u("from_facility", ctx);
    t.to_facility = e.require_decimal_u("to_facility", ctx);
    t.count = static_cast<int>(e.require_int("count", ctx));
    t.ready_day = e.require_decimal("ready_day", ctx);
    s.transfers.push_back(t);
  }

  {
    const json::Value& e = root.require("ship", ctx);
    ShipState& sh = s.ship;
    sh.id = e.require_decimal_u("id", ctx);
    auto phase = parse_ship_phase(e.require_string("phase", ctx));
    if (!phase) throw SimError("save: unknown ship phase");
    sh.phase = *phase;
    auto mission = parse_ship_mission(e.require_string("mission", ctx));
    if (!mission) throw SimError("save: unknown ship mission");
    sh.mission = *mission;
    sh.location_planet = e.require_string("location_planet", ctx);
    sh.origin_planet = e.require_string("origin_planet", ctx);
    sh.destination_planet = e.require_string("destination_planet", ctx);
    sh.departure_day = e.require_decimal("departure_day", ctx);
    sh.arrival_day = e.require_decimal("arrival_day", ctx);
    sh.earliest_departure_day = e.require_decimal("earliest_departure_day", ctx);
    sh.cargo = dec_resource_map(e.require("cargo", ctx), cat, ctx);
    sh.tank = e.require_decimal("tank", ctx);
    sh.crew = static_cast<int>(e.require_int("crew", ctx));
    sh.crew_home_planet = e.require_string("crew_home_planet", ctx);
    sh.booked_manifest = dec_resource_map(e.require("booked_manifest", ctx), cat, ctx);
    sh.booking_reservations = dec_ids(e.require("booking_reservations", ctx), ctx);
    sh.booking_claims = dec_ids(e.require("booking_claims", ctx), ctx);
    sh.unloaded_so_far = dec_resource_map(e.require("unloaded_so_far", ctx), cat, ctx);
    sh.shipment_id = e.require_decimal_u("shipment_id", ctx);
    sh.mission_delivery_day = e.require_decimal("mission_delivery_day", ctx);
    sh.mission_return_departure_day = e.require_decimal("mission_return_departure_day", ctx);
    sh.mission_delivered = e.bool_or("mission_delivered", false);
  }
  {
    const json::Value& e = root.require("route", ctx);
    RoutePlan& r = s.route;
    r.id = e.require_string("id", ctx);
    r.origin_planet = e.require_string("origin_planet", ctx);
    r.destination_planet = e.require_string("destination_planet", ctx);
    r.outbound_targets = dec_resource_map(e.require("outbound_targets", ctx), cat, ctx);
    r.return_targets = dec_resource_map(e.require("return_targets", ctx), cat, ctx);
    for (const auto& [key, val] : e.require_object("source_floors", ctx)) {
      r.source_floors[cat.resource_index(key)] = parse_decimal_string(val.as_string());
    }
    for (const auto& v : dec_strings(e.require("floor_overridden", ctx), ctx)) {
      r.floor_overridden.insert(cat.resource_index(v));
    }
    r.enabled = e.bool_or("enabled", false);
    r.next_departure_day = e.require_decimal("next_departure_day", ctx);
    r.departure_interval_days = e.require_decimal("departure_interval_days", ctx);
    r.departure_requested = e.bool_or("departure_requested", false);
    r.departure_allow_empty = e.bool_or("departure_allow_empty", false);
  }
  {
    const json::Value& e = root.require("colonisation", ctx);
    ColonizationState& c = s.colonisation;
    c.launched = e.bool_or("launched", false);
    c.founded = e.bool_or("founded", false);
    c.expedition_id = e.require_decimal_u("expedition_id", ctx);
    c.target_planet = e.require_string("target_planet", ctx);
    c.residents = e.require_decimal("residents", ctx);
    c.workers = e.require_decimal("workers", ctx);
    c.cargo = dec_resource_map(e.require("cargo", ctx), cat, ctx);
    c.consumed_transit = dec_resource_map(e.require("consumed_transit", ctx), cat, ctx);
    c.launch_day = e.require_decimal("launch_day", ctx);
    c.arrival_day = e.require_decimal("arrival_day", ctx);
    c.founded_day = e.require_decimal("founded_day", ctx);
  }
  {
    const json::Value& e = root.require("political", ctx);
    PoliticalState& p = s.political;
    p.faction_id = e.require_string("faction_id", ctx);
    p.adherence = static_cast<int>(e.require_int("adherence", ctx));
    for (const auto& v : dec_strings(e.require("resolved_choices", ctx), ctx)) p.resolved_choices.insert(v);
    p.low_adherence_streak = static_cast<int>(e.require_int("low_adherence_streak", ctx));
    p.faction_review_cooldown_until = e.require_decimal("faction_review_cooldown_until", ctx);
    p.modifiers = dec_modifiers(e.require("modifiers", ctx), ctx);
  }

  for (const auto& e : root.require_array("events", ctx)) {
    EventInstance i;
    i.id = e.require_decimal_u("id", ctx);
    i.event_id = e.require_string("event_id", ctx);
    if (cat.find_event(i.event_id) == nullptr) throw SimError("save: unknown event '" + i.event_id + "'");
    i.chain_id = e.require_string("chain_id", ctx);
    i.planet_id = e.require_string("planet_id", ctx);
    i.facility_id = e.require_decimal_u("facility_id", ctx);
    i.opened_day = e.require_decimal("opened_day", ctx);
    i.deadline_day = e.require_decimal("deadline_day", ctx);
    auto res = parse_event_resolution(e.require_string("resolution", ctx));
    if (!res) throw SimError("save: unknown event resolution");
    i.resolution = *res;
    i.chosen_choice = e.require_string("chosen_choice", ctx);
    i.chosen_day = e.require_decimal("chosen_day", ctx);
    i.trigger_facts = dec_ids(e.require("trigger_facts", ctx), ctx);
    s.events.push_back(i);
  }
  for (const auto& e : root.require_array("scheduled", ctx)) {
    ScheduledEffect x;
    x.id = e.require_decimal_u("id", ctx);
    x.due_day = e.require_decimal("due_day", ctx);
    x.priority = static_cast<int>(e.require_int("priority", ctx));
    x.event_id = e.require_string("event_id", ctx);
    x.choice_id = e.require_string("choice_id", ctx);
    x.list_name = e.require_string("list_name", ctx);
    x.effect_index = static_cast<int>(e.require_int("effect_index", ctx));
    x.nested_index = static_cast<int>(e.require_int("nested_index", ctx));
    x.planet_id = e.require_string("planet_id", ctx);
    x.facility_id = e.require_decimal_u("facility_id", ctx);
    x.source_event_instance = e.require_decimal_u("source_event_instance", ctx);
    s.scheduled.push_back(x);
  }
  for (const auto& e : root.require_array("event_requests", ctx)) {
    EventRequest r;
    r.id = e.require_decimal_u("id", ctx);
    r.event_id = e.require_string("event_id", ctx);
    r.planet_id = e.require_string("planet_id", ctx);
    r.facility_id = e.require_decimal_u("facility_id", ctx);
    r.requested_day = e.require_decimal("requested_day", ctx);
    r.trigger_facts = dec_ids(e.require("trigger_facts", ctx), ctx);
    s.event_requests.push_back(r);
  }
  {
    const json::Value& e = root.require("demand", ctx);
    DemandState& d = s.demand;
    d.issued = e.bool_or("issued", false);
    d.issue_day = e.require_decimal("issue_day", ctx);
    d.deadline_day = e.require_decimal("deadline_day", ctx);
    d.required = dec_resource_map(e.require("required", ctx), cat, ctx);
    d.delivered = dec_resource_map(e.require("delivered", ctx), cat, ctx);
    d.negotiated = e.bool_or("negotiated", false);
    d.resolved = e.bool_or("resolved", false);
    auto dec = parse_mandate_decision(e.require_string("decision", ctx));
    if (!dec) throw SimError("save: unknown mandate decision");
    d.decision = *dec;
    for (const auto& m : e.require_array("missions", ctx)) {
      MandateMission x;
      x.id = m.require_decimal_u("id", ctx);
      x.departure_day = m.require_decimal("departure_day", ctx);
      x.delivery_day = m.require_decimal("delivery_day", ctx);
      x.manifest = dec_resource_map(m.require("manifest", ctx), cat, ctx);
      x.delivered = m.bool_or("delivered", false);
      d.missions.push_back(x);
    }
    d.freighter_committed = e.bool_or("freighter_committed", false);
    d.pending_manifest = dec_resource_map(e.require("pending_manifest", ctx), cat, ctx);
    d.mission_requested = e.bool_or("mission_requested", false);
    d.mission_requested_day = e.require_decimal("mission_requested_day", ctx);
  }
  {
    const json::Value& e = root.require("history", ctx);
    HistoryState& h = s.history;
    for (const auto& y : e.require_array("samples", ctx)) {
      MetricSample x;
      x.day = y.require_decimal("day", ctx);
      x.planet_id = y.require_string("planet_id", ctx);
      x.health_bp = y.require_decimal("health_bp", ctx);
      x.stability_bp = y.require_decimal("stability_bp", ctx);
      x.fatigue_bp = y.require_decimal("fatigue_bp", ctx);
      x.food_fulfilment_bp = y.require_decimal("food_fulfilment_bp", ctx);
      x.water_fulfilment_bp = y.require_decimal("water_fulfilment_bp", ctx);
      x.power_fulfilment_bp = y.require_decimal("power_fulfilment_bp", ctx);
      x.closing_stock = dec_milli_vector(y.require("closing_stock", ctx), ctx);
      h.samples.push_back(x);
    }
    for (const auto& y : e.require_array("weekly", ctx)) {
      WeeklySummary x;
      x.first_day = y.require_decimal("first_day", ctx);
      x.last_day = y.require_decimal("last_day", ctx);
      x.planet_id = y.require_string("planet_id", ctx);
      x.min_health_bp = y.require_decimal("min_health_bp", ctx);
      x.min_stability_bp = y.require_decimal("min_stability_bp", ctx);
      x.min_food_fulfilment_bp = y.require_decimal("min_food_fulfilment_bp", ctx);
      x.min_water_fulfilment_bp = y.require_decimal("min_water_fulfilment_bp", ctx);
      h.weekly.push_back(x);
    }
    h.day_hashes = dec_strings(e.require("day_hashes", ctx), ctx);
    h.first_retained_hash_day = e.require_decimal("first_retained_hash_day", ctx);
    h.missed_colonial_food_manifests = static_cast<int>(e.require_int("missed_colonial_food_manifests", ctx));
    for (const auto& v : dec_strings(e.require("milestone_flags", ctx), ctx)) h.milestone_flags.insert(v);
  }

  for (const auto& [key, val] : root.require_object("flags", ctx)) s.flags[key] = val.as_bool();
  for (const auto& [key, val] : root.require_object("flag_days", ctx)) {
    s.flag_days[key] = parse_decimal_string(val.as_string());
  }
  for (const auto& [key, val] : root.require_object("cooldowns", ctx)) {
    s.cooldowns[key] = parse_decimal_string(val.as_string());
  }
  for (const auto& [key, val] : root.require_object("condition_streaks", ctx)) {
    s.condition_streaks[key] = static_cast<int>(parse_decimal_string(val.as_string()));
  }
  for (const auto& [key, val] : root.require_object("applied_commands", ctx)) {
    RecordedCommandResult rec;
    rec.accepted = val.bool_or("accepted", false);
    rec.revision = val.require_decimal_u("revision", ctx);
    rec.reason = val.require_string("reason", ctx);
    rec.detail = val.require_string("detail", ctx);
    s.applied_commands[key] = rec;
  }
  for (const auto& e : root.require_array("ledger", ctx)) {
    Transaction t;
    t.id = e.require_decimal_u("id", ctx);
    t.day = e.require_decimal("day", ctx);
    t.resource = cat.resource_index(e.require_string("resource", ctx));
    t.quantity = e.require_decimal("quantity", ctx);
    t.from_account = e.require_string("from_account", ctx);
    t.to_account = e.require_string("to_account", ctx);
    t.operation_id = e.require_decimal_u("operation_id", ctx);
    t.cause = e.require_string("cause", ctx);
    s.ledger.push_back(t);
  }
  for (const auto& e : root.require_array("facts", ctx)) {
    FactRecord f;
    f.id = e.require_decimal_u("id", ctx);
    f.day = e.require_decimal("day", ctx);
    f.kind = e.require_string("kind", ctx);
    f.planet_id = e.require_string("planet_id", ctx);
    f.entity_id = e.require_decimal_u("entity_id", ctx);
    f.args = dec_named_values(e.require("args", ctx), ctx);
    f.text_args = dec_strings(e.require("text_args", ctx), ctx);
    f.causal_parents = dec_ids(e.require("causal_parents", ctx), ctx);
    f.reason_id = e.require_string("reason_id", ctx);
    f.dedupe_key = e.require_string("dedupe_key", ctx);
    s.facts.push_back(f);
  }
  for (const auto& e : root.require_array("news", ctx)) {
    NewsRecord n;
    n.id = e.require_decimal_u("id", ctx);
    n.day = e.require_decimal("day", ctx);
    n.template_key = e.require_string("template_key", ctx);
    n.planet_id = e.require_string("planet_id", ctx);
    n.args = dec_named_values(e.require("args", ctx), ctx);
    n.text_args = dec_strings(e.require("text_args", ctx), ctx);
    n.source_facts = dec_ids(e.require("source_facts", ctx), ctx);
    n.dedupe_key = e.require_string("dedupe_key", ctx);
    n.priority = static_cast<int>(e.require_int("priority", ctx));
    n.detail_compacted = e.bool_or("detail_compacted", false);
    s.news.push_back(n);
  }
  return s;
}

}  // namespace expansion
