#include "expansion/read_models.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "expansion/derived.hpp"
#include "expansion/reasons.hpp"
#include "expansion/text.hpp"

namespace expansion::read {
namespace {

std::string pad(const std::string& s, std::size_t width) {
  if (s.size() >= width) return s;
  return s + std::string(width - s.size(), ' ');
}

std::string rpad(const std::string& s, std::size_t width) {
  if (s.size() >= width) return s;
  return std::string(width - s.size(), ' ') + s;
}

const char* lifecycle_word(FacilityLifecycle l) { return facility_lifecycle_id(l); }

// Days of cover at the current rate. Labelled by the caller as a
// "current-rate estimate", never as a forecast (TDD 6.3).
std::int64_t days_of_cover(const PlanetState& p, int resource) {
  const auto& d = p.last_day;
  if (d.consumed.empty() || d.produced.empty()) return -1;
  const Milli used = d.consumed[static_cast<std::size_t>(resource)];
  const Milli made = d.produced[static_cast<std::size_t>(resource)];
  const Milli net = used - made;
  if (net <= 0) return -1;
  return p.inventory.on_hand[static_cast<std::size_t>(resource)] / net;
}

}  // namespace

std::vector<Concern> top_concerns(const Session& session, int limit) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  std::vector<Concern> out;

  for (const auto& p : s.planets) {
    if (!p.colonised) continue;
    if (p.survival_emergency_active) {
      out.push_back({p.planet_id, "survival emergency in its " + std::to_string(p.survival_emergency_days) + " day",
                     reason::kMissingInput, -1, 0});
    }
    if (p.last_day.food_fulfilment_bp < kBpOne) {
      out.push_back({p.planet_id, "food demand was not met", reason::kMissingInput, -1, 1});
    }
    if (p.last_day.water_fulfilment_bp < kBpOne) {
      out.push_back({p.planet_id, "water demand was not met", reason::kMissingInput, -1, 1});
    }
    if (p.last_day.power_fulfilment_bp < kBpOne) {
      out.push_back({p.planet_id, "residential power was not met", reason::kPowerShortfall, -1, 1});
    }
    for (int r = 0; r < cat.resource_count(); ++r) {
      std::int64_t cover = days_of_cover(p, r);
      if (cover >= 0 && cover <= 10) {
        out.push_back({p.planet_id, cat.resource(r).id + " lasts about " + std::to_string(cover) + " more days",
                       reason::kInsufficientStock, cover, 2});
      }
    }
  }
  for (const auto& e : s.events) {
    if (e.resolution != EventResolution::Open || e.deadline_day < 0) continue;
    out.push_back({e.planet_id, "a decision on " + e.event_id + " is open", reason::kOk,
                   e.deadline_day - s.day, 0});
  }
  if (s.demand.issued && !s.demand.resolved) {
    out.push_back({"", "the supply mandate is outstanding", reason::kOk, s.demand.deadline_day - s.day, 1});
  }
  if (s.colonisation.launched == false) {
    const ScenarioDef& sc = cat.scenario(s.scenario_id);
    out.push_back({"", "the colony has not launched", reason::kOk, sc.expedition.launch_deadline_day - s.day, 2});
  }
  std::stable_sort(out.begin(), out.end(), [](const Concern& a, const Concern& b) {
    if (a.severity != b.severity) return a.severity < b.severity;
    if ((a.days_remaining >= 0) != (b.days_remaining >= 0)) return a.days_remaining >= 0;
    return a.days_remaining < b.days_remaining;
  });
  if (static_cast<int>(out.size()) > limit) out.resize(static_cast<std::size_t>(limit));
  return out;
}

std::string civilization_overview(const Session& session) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  std::ostringstream o;
  o << "CIVILIZATION  day " << s.day << "   revision " << s.revision << "   faction " << s.political.faction_id
    << " (adherence " << s.political.adherence << "/100)   state " << session_lifecycle_id(s.lifecycle) << "\n";
  if (s.paused_for_decision) o << "  simulation paused for an open decision\n";
  o << "\n  " << pad("world", 12) << rpad("people", 8) << rpad("health", 9) << rpad("stability", 11)
    << rpad("fatigue", 9) << "  policy\n";
  for (const auto& p : s.planets) {
    if (!p.colonised) {
      o << "  " << pad(p.planet_id, 12) << rpad("-", 8) << rpad("-", 9) << rpad("-", 11) << rpad("-", 9)
        << "  uncolonised\n";
      continue;
    }
    o << "  " << pad(p.planet_id, 12) << rpad(std::to_string(p.population), 8)
      << rpad(format_bp_percent(p.health_bp), 9) << rpad(format_bp_percent(p.stability_bp), 11)
      << rpad(format_bp_percent(p.fatigue_bp), 9) << "  " << p.policy_id << "\n";
  }

  o << "\n  obligations\n";
  const ScenarioDef& sc = cat.scenario(s.scenario_id);
  o << "    colony launch by day " << sc.expedition.launch_deadline_day << ": "
    << (s.colonisation.launched ? "launched on day " + to_decimal_string(s.colonisation.launch_day) : "not launched")
    << "\n";
  if (s.demand.issued) {
    o << "    supply mandate accepted at the front by day " << s.demand.deadline_day << ": "
      << mandate_decision_id(s.demand.decision) << "\n";
    for (const auto& [idx, need] : s.demand.required) {
      Milli have = s.demand.delivered.count(idx) != 0 ? s.demand.delivered.at(idx) : 0;
      o << "      " << pad(cat.resource(idx).id, 12) << format_milli(have) << " of " << format_milli(need) << "\n";
    }
  } else {
    o << "    supply mandate issues on day " << sc.mandate.issue_day << "\n";
  }
  o << "    evaluation on day " << sc.completion.evaluation_day << "\n";

  o << "\n  top concerns\n";
  auto concerns = top_concerns(session, 3);
  if (concerns.empty()) {
    o << "    nothing is at risk on the last committed day\n";
  } else {
    for (const auto& c : concerns) {
      o << "    - " << (c.planet_id.empty() ? std::string("sector") : c.planet_id) << ": " << c.headline;
      if (c.days_remaining >= 0) o << " (" << c.days_remaining << " days)";
      if (c.cause != reason::kOk) o << " [" << text::reason_text(c.cause) << "]";
      o << "\n";
    }
  }
  return o.str();
}

std::string network_map(const Session& session) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  const ShipState& ship = s.ship;
  std::ostringstream o;
  o << "NETWORK  day " << s.day << "\n";
  o << "  route " << s.route.origin_planet << " <-> " << s.route.destination_planet << "   "
    << (s.route.enabled ? "enabled" : "disabled") << "\n";
  o << "  freighter: " << ship_phase_id(ship.phase);
  if (!ship.location_planet.empty()) {
    o << " at " << ship.location_planet;
  } else {
    o << " " << ship.origin_planet << " -> "
      << (ship.destination_planet.empty() ? std::string("the front") : ship.destination_planet)
      << ", arriving day " << ship.arrival_day;
  }
  if (ship.mission != ShipMission::None) o << "   mission " << ship_mission_id(ship.mission);
  o << "\n";
  o << "  cargo " << format_milli(cargo_volume_of(cat, ship.cargo)) << " of "
    << format_milli(cat.scenario(s.scenario_id).freight.cargo_capacity) << " volume";
  for (const auto& [idx, qty] : ship.cargo) o << "   " << cat.resource(idx).id << " " << format_milli(qty);
  o << "\n  tank " << format_milli(ship.tank) << "   earliest departure day " << ship.earliest_departure_day
    << "   next scheduled departure day " << s.route.next_departure_day << "\n";
  for (const auto& p : s.planets) {
    o << "  " << pad(p.planet_id, 12) << "docks " << (p.last_day.port_handling_capacity > 0 ? "working" : "none")
      << "   handling " << format_milli(p.last_day.port_handling_used) << " of "
      << format_milli(p.last_day.port_handling_capacity) << " used on day " << p.last_day.day << "\n";
  }
  if (s.colonisation.launched && !s.colonisation.founded) {
    o << "  expedition in transit, arriving day " << s.colonisation.arrival_day << "\n";
  }
  return o.str();
}

std::string planet_inspector(const Session& session, const std::string& planet_id) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  const PlanetState* p = s.find_planet(planet_id);
  if (p == nullptr) return "unknown world '" + planet_id + "'\n";
  const DailyReport& d = p->last_day;
  std::ostringstream o;
  o << "WORLD " << p->planet_id << "   day " << s.day << "   last resolved day " << d.day << "\n";
  if (!p->colonised) {
    o << "  uncolonised\n";
    return o.str();
  }
  o << "  people " << p->population << "   health " << format_bp_percent(p->health_bp) << "   stability "
    << format_bp_percent(p->stability_bp) << " (target " << format_bp_percent(d.stability_target_bp) << ")"
    << "   fatigue " << format_bp_percent(p->fatigue_bp) << "\n";
  o << "  housing " << d.housing_capacity << " for " << p->population << "   clinic cover " << d.clinic_capacity
    << " (" << format_bp_percent(d.clinic_coverage_bp) << ")\n";
  o << "  policy " << p->policy_id << " (" << p->policy_effective_days << " affected days)\n";
  o << "  power  generated " << format_milli(d.power.generated) << "   residential "
    << format_milli(d.power.residential_served) << " of " << format_milli(d.power.residential_demand)
    << "   facilities " << format_milli(d.power.facility_granted) << "   construction "
    << format_milli(d.power.construction_granted) << "   spare " << format_milli(d.power.unused) << "\n";
  o << "  needs  food " << format_milli(d.food_served) << " of " << format_milli(d.food_demand) << "   water "
    << format_milli(d.water_served) << " of " << format_milli(d.water_demand) << "\n";

  o << "\n  " << pad("resource", 12) << rpad("stock", 10) << rpad("reserved", 10) << rpad("produced", 10)
    << rpad("consumed", 10) << rpad("net", 9) << "  current-rate estimate\n";
  for (int r = 0; r < cat.resource_count(); ++r) {
    const Milli have = p->inventory.on_hand[static_cast<std::size_t>(r)];
    const Milli made = d.produced.empty() ? 0 : d.produced[static_cast<std::size_t>(r)];
    const Milli used = d.consumed.empty() ? 0 : d.consumed[static_cast<std::size_t>(r)];
    std::int64_t cover = days_of_cover(*p, r);
    o << "  " << pad(cat.resource(r).id, 12) << rpad(format_milli(have), 10)
      << rpad(format_milli(p->inventory.reserved(r)), 10) << rpad(format_milli(made), 10)
      << rpad(format_milli(used), 10) << rpad(format_milli(made - used), 9) << "  "
      << (cover < 0 ? std::string("stable or rising") : std::to_string(cover) + " days of cover") << "\n";
  }

  o << "\n  workforce  assigned " << d.workers_assigned << "   in transit " << d.workers_transitioning
    << "   reserve " << d.workers_reserve << "   ship crew " << d.workers_crew << "   of " << p->workers_total
    << " workers\n";
  o << "  " << pad("id", 5) << pad("facility", 24) << pad("state", 18) << rpad("staff", 8) << rpad("cond", 8)
    << rpad("band", 6) << "  limiting reason\n";
  for (const auto& f : s.facilities) {
    if (f.planet_id != p->planet_id) continue;
    const FacilityDef& fd = cat.facility(f.facility_id);
    const int staff = fd.recipes[static_cast<std::size_t>(fd.recipe_index(f.recipe_id))].staff;
    std::string state_word = lifecycle_word(f.lifecycle);
    if (f.construction.has_value()) {
      state_word += " " + format_bp_percent(f.construction->work_total > 0
                                                ? mul_div_floor(f.construction->work_done, kBpOne,
                                                                f.construction->work_total)
                                                : kBpOne);
    }
    if (f.service.has_value()) state_word = "servicing";
    if (f.idle) state_word = "idle";
    const std::string staffing = f.construction.has_value()
                                     ? std::to_string(f.construction->assigned_workers) + " crew"
                                     : std::to_string(f.assigned_workers) + "/" + std::to_string(staff);
    o << "  " << pad(std::to_string(f.id), 5) << pad(f.recipe_id, 24) << pad(state_word, 18) << rpad(staffing, 8)
      << rpad(format_bp_percent(f.condition_bp), 8) << rpad(std::to_string(f.priority_band), 6) << "  "
      << (f.last_explanation.day == d.day ? text::reason_text(f.last_explanation.primary_reason)
                                          : std::string("not resolved on this day"))
      << "\n";
  }
  return o.str();
}

std::string facility_inspector(const Session& session, InstanceId facility_id) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  const FacilityState* f = s.find_facility(facility_id);
  if (f == nullptr) return "unknown facility instance " + to_decimal_string_u(facility_id) + "\n";
  const FacilityDef& fd = cat.facility(f->facility_id);
  const RecipeDef& r = fd.recipes[static_cast<std::size_t>(fd.recipe_index(f->recipe_id))];
  const FacilityExplanation& x = f->last_explanation;
  std::ostringstream o;
  o << "FACILITY #" << f->id << "  " << f->facility_id << " / " << f->recipe_id << "  at " << f->planet_id << "\n";
  o << "  state " << lifecycle_word(f->lifecycle) << "   staff " << f->assigned_workers << " of " << r.staff
    << "   condition " << format_bp_percent(f->condition_bp) << "   priority band " << f->priority_band
    << "   maintenance " << format_milli(fd.maintenance_machinery_per_day) << " machinery/day\n";
  if (f->service.has_value()) o << "  servicing until day " << f->service->ready_day << "\n";
  if (f->construction.has_value()) {
    const auto& j = *f->construction;
    o << "  construction " << format_milli(j.work_done) << " of " << format_milli(j.work_total)
      << " person-days, crew " << j.assigned_workers;
    if (!j.blockage.empty()) o << ", blocked: " << text::reason_text(j.blockage);
    o << "\n  escrow";
    for (const auto& [idx, qty] : j.escrow) o << "  " << cat.resource(idx).id << " " << format_milli(qty);
    o << "\n";
  }
  if (x.day < 0) {
    o << "  this facility has not been resolved on a simulation day yet\n";
    return o.str();
  }
  o << "\n  day " << x.day << " run factor " << format_bp_percent(x.actual_throughput_bp) << " of a desired "
    << format_bp_percent(x.desired_throughput_bp) << "\n";
  o << "  factors: staffing " << format_bp_percent(x.staffing_bp) << "  health " << format_bp_percent(x.health_bp)
    << "  fatigue " << format_bp_percent(x.fatigue_bp) << "  condition " << format_bp_percent(x.condition_bp)
    << "  faction " << format_bp_percent(x.faction_bp) << "  policy " << format_bp_percent(x.policy_bp)
    << "  effects " << format_bp_percent(x.effects_bp) << "\n";
  o << "  power requested " << format_milli(x.power_requested) << "   granted " << format_milli(x.power_granted)
    << "\n";
  if (!x.inputs_consumed.empty()) {
    o << "  consumed";
    for (const auto& [idx, qty] : x.inputs_consumed) o << "  " << cat.resource(idx).id << " " << format_milli(qty);
    o << "\n";
  }
  if (!x.outputs_produced.empty()) {
    o << "  produced";
    for (const auto& [idx, qty] : x.outputs_produced) o << "  " << cat.resource(idx).id << " " << format_milli(qty);
    o << "\n";
  }
  o << "  primary reason: " << text::reason_text(x.primary_reason) << "\n";
  if (!x.missing_inputs.empty()) {
    o << "  also short of:";
    for (int idx : x.missing_inputs) o << " " << cat.resource(idx).id;
    o << "\n";
  }
  if (!x.blocked_outputs.empty()) {
    o << "  output store full for:";
    for (int idx : x.blocked_outputs) o << " " << cat.resource(idx).id;
    o << "\n";
  }
  if (!x.modifier_tags.empty()) {
    o << "  active effects:";
    for (const auto& t : x.modifier_tags) o << " " << t;
    o << "\n";
  }
  for (const auto& m : f->modifiers) {
    o << "    effect " << m.tag << " factor " << format_bp_percent(m.factor_bp) << " from " << m.source_event_id
      << (m.expires_day < 0 ? std::string(", until cleared")
                            : ", until day " + to_decimal_string(m.expires_day))
      << "\n";
  }
  return o.str();
}

std::string freight_inspector(const Session& session) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  const ScenarioDef& sc = cat.scenario(s.scenario_id);
  const RoutePlan& route = s.route;
  std::ostringstream o;
  o << "FREIGHT  day " << s.day << "\n";
  o << "  outbound manifest targets\n";
  if (route.outbound_targets.empty()) o << "    none configured\n";
  for (const auto& [idx, qty] : route.outbound_targets) {
    o << "    " << pad(cat.resource(idx).id, 12) << format_milli(qty) << "\n";
  }
  o << "  return manifest targets\n";
  if (route.return_targets.empty()) o << "    none configured\n";
  for (const auto& [idx, qty] : route.return_targets) {
    o << "    " << pad(cat.resource(idx).id, 12) << format_milli(qty) << "\n";
  }
  o << "  reserve floors (protect automatic export, not civilian use)\n";
  const PlanetState& origin = s.planet(route.origin_planet);
  for (int r = 0; r < cat.resource_count(); ++r) {
    Milli floor_qty = route.floor_overridden.count(r) != 0
                          ? 0
                          : (route.source_floors.count(r) != 0 ? route.source_floors.at(r)
                                                               : default_outbound_floor(s, cat, origin, r));
    if (floor_qty == 0 && route.floor_overridden.count(r) == 0) continue;
    o << "    " << pad(cat.resource(r).id, 12) << format_milli(floor_qty)
      << (route.floor_overridden.count(r) != 0 ? "   (overridden by the player)" : "") << "\n";
  }
  o << "  round trip: " << format_milli(sc.freight.round_trip_fuel) << " Fuel reserved, "
    << format_milli(sc.freight.outbound_fuel_burn) << " burned outbound, "
    << format_milli(sc.freight.return_fuel_burn) << " carried for the return, "
    << format_milli(sc.freight.round_trip_machinery) << " Machinery once\n";
  o << "  strategic mission: " << format_milli(sc.mandate.mission_fuel)
    << " Fuel upfront (separate from the round-trip tank) and " << format_milli(sc.mandate.mission_machinery)
    << " Machinery; the ship is committed for "
    << (sc.mandate.mission_outbound_days + sc.mandate.mission_service_days + sc.mandate.mission_return_days)
    << " days\n";
  o << "  timeline\n";
  o << "    freighter " << ship_phase_id(s.ship.phase);
  if (!s.ship.location_planet.empty()) o << " at " << s.ship.location_planet;
  o << ", earliest departure day " << s.ship.earliest_departure_day << "\n";
  for (const auto& m : s.demand.missions) {
    o << "    mission departed day " << m.departure_day << ", accepted at the front day " << m.delivery_day << " ("
      << (m.delivered ? "delivered" : "in transit") << ")\n";
  }
  if (s.history.missed_colonial_food_manifests > 0) {
    o << "    missed colonial Food manifests: " << s.history.missed_colonial_food_manifests << "\n";
  }
  return o.str();
}

std::string decision_drawer(const Session& session) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  std::ostringstream o;
  o << "DECISIONS  day " << s.day << "\n";
  bool any = false;
  for (const auto& inst : s.events) {
    if (inst.resolution != EventResolution::Open) continue;
    const EventDef* def = cat.find_event(inst.event_id);
    if (def == nullptr) continue;
    any = true;
    o << "  #" << inst.id << "  " << inst.event_id << " at " << inst.planet_id;
    if (inst.facility_id != 0) o << " facility #" << inst.facility_id;
    if (inst.deadline_day >= 0) {
      o << "   decide by day " << inst.deadline_day << " (" << (inst.deadline_day - s.day) << " days)";
    } else {
      o << "   remedies remain available";
    }
    o << "\n";
    const PlanetState* p = s.find_planet(inst.planet_id);
    for (const auto& ch : def->choices) {
      bool affordable = true;
      std::string deficit;
      if (p != nullptr) {
        for (const auto& [idx, qty] : ch.cost) {
          Milli have = p->inventory.available(idx);
          if (have < qty) {
            affordable = false;
            if (!deficit.empty()) deficit += ", ";
            deficit += cat.resource(idx).id + " short by " + format_milli(qty - have);
          }
        }
      }
      o << "      " << pad(ch.id, 24);
      if (ch.cost.empty()) {
        o << "no material cost";
      } else {
        bool first = true;
        for (const auto& [idx, qty] : ch.cost) {
          if (!first) o << ", ";
          first = false;
          o << format_milli(qty) << " " << cat.resource(idx).id;
        }
      }
      if (ch.required_reserve_workers > 0) o << "; " << ch.required_reserve_workers << " Reserve workers";
      if (ch.required_assigned_workers > 0) o << "; " << ch.required_assigned_workers << " workers on site";
      if (!affordable) o << "   UNAVAILABLE: " << deficit;
      o << "\n";
    }
  }
  if (!any) o << "  no decision is open\n";
  return o.str();
}

std::string history_drawer(const Session& session, int max_entries, const std::string& search) {
  const SessionState& s = session.state();
  std::ostringstream o;
  o << "HISTORY  " << s.news.size() << " entries\n";
  int shown = 0;
  for (auto it = s.news.rbegin(); it != s.news.rend() && shown < max_entries; ++it) {
    std::string line = text::news_line(*it);
    if (!search.empty() && line.find(search) == std::string::npos) continue;
    o << "  " << line << "\n";
    if (!it->source_facts.empty()) {
      o << "      facts:";
      for (InstanceId f : it->source_facts) o << " #" << f;
      if (it->detail_compacted) o << "  (some routine detail was compacted)";
      o << "\n";
    }
    ++shown;
  }
  if (shown == 0) o << "  nothing recorded yet\n";
  return o.str();
}

std::string ledger_view(const Session& session, Day day) {
  const SessionState& s = session.state();
  const Catalog& cat = session.catalog();
  std::ostringstream o;
  o << "LEDGER  day " << day << "\n";
  o << "  " << pad("resource", 12) << rpad("quantity", 10) << "  from -> to   (cause)\n";
  std::map<int, Milli> owned_delta;
  for (const auto& t : s.ledger) {
    if (t.day != day) continue;
    o << "  " << pad(cat.resource(t.resource).id, 12) << rpad(format_milli(t.quantity), 10) << "  "
      << t.from_account << " -> " << t.to_account << "   (" << t.cause << ")\n";
    const bool from_owned = is_sector_owned_account(t.from_account);
    const bool to_owned = is_sector_owned_account(t.to_account);
    if (to_owned && !from_owned) owned_delta[t.resource] += t.quantity;
    if (from_owned && !to_owned) owned_delta[t.resource] -= t.quantity;
  }
  o << "  net change in sector-owned quantity\n";
  for (const auto& [idx, delta] : owned_delta) {
    o << "    " << pad(cat.resource(idx).id, 12) << format_milli(delta) << "\n";
  }
  return o.str();
}

std::string outcome_report(const Session& session) {
  const SessionState& s = session.state();
  std::ostringstream o;
  o << "OUTCOME  " << session_lifecycle_id(s.lifecycle) << " on day " << s.day << "\n";
  o << "  faction " << s.political.faction_id << ", adherence " << s.political.adherence << "/100\n";
  bool relief = false;
  for (const auto& p : s.planets) {
    if (p.relief_used) relief = true;
  }
  o << "  relief contract used: " << (relief ? "yes" : "no") << "   (reported separately, not folded into a score)\n";
  o << "  missed colonial Food manifests: " << s.history.missed_colonial_food_manifests << "\n";
  for (const auto& p : s.planets) {
    if (!p.colonised) continue;
    o << "  " << pad(p.planet_id, 12) << "health " << format_bp_percent(p.health_bp) << "   stability "
      << format_bp_percent(p.stability_bp) << "   clean final days " << p.clean_day_streak << "\n";
  }
  for (const auto& f : s.facts) {
    if (f.kind != "scenario_evaluated") continue;
    if (f.text_args.empty()) {
      o << "  every completion predicate held\n";
    } else {
      o << "  unmet predicates:\n";
      for (const auto& t : f.text_args) o << "    - " << t << "\n";
    }
  }
  return o.str();
}

}  // namespace expansion::read
