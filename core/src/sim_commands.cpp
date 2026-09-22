// Command application (TDD 5.1). A command executes atomically between
// simulation days, including while paused. A rejected command alters neither
// state nor schedule.
#include <algorithm>
#include <limits>

#include "expansion/metrics.hpp"
#include "sim_internal.hpp"

namespace expansion {

using sim::def_of;
using sim::recipe_of;

namespace {

CommandResult reject(Revision revision, const char* why, std::string detail = {}) {
  CommandResult r;
  r.accepted = false;
  r.revision = revision;
  r.reason = why;
  r.detail = std::move(detail);
  return r;
}

std::string deficit_text(const Catalog& cat, const ResourceMap& needed, const InventoryState& inv) {
  std::string out;
  for (const auto& [idx, qty] : needed) {
    Milli have = inv.available(idx);
    if (have >= qty) continue;
    if (!out.empty()) out += ", ";
    out += cat.resource(idx).id + " short by " + format_milli(checked_sub(qty, have));
  }
  return out;
}

bool can_afford(const ResourceMap& needed, const InventoryState& inv) {
  for (const auto& [idx, qty] : needed) {
    if (inv.available(idx) < qty) return false;
  }
  return true;
}

}  // namespace

CommandResult Session::apply_command(const Command& command, std::optional<Revision> expected_revision) {
  // Duplicate command ids return their recorded result without applying again.
  if (!command.id.empty()) {
    auto it = state_.applied_commands.find(command.id);
    if (it != state_.applied_commands.end()) {
      CommandResult r;
      r.accepted = it->second.accepted;
      r.revision = it->second.revision;
      r.reason = it->second.reason;
      r.detail = it->second.detail;
      r.replayed = true;
      return r;
    }
  }
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    CommandResult r = reject(state_.revision, reason::kRevisionMismatch,
                             "the preview was built on revision " + to_decimal_string_u(*expected_revision) +
                                 "; the committed revision is " + to_decimal_string_u(state_.revision));
    reason_counts_[r.reason] += 1;
    return r;
  }
  if (state_.lifecycle != SessionLifecycle::Running && command.kind != CommandKind::Surrender) {
    CommandResult r = reject(state_.revision, reason::kScenarioFinished);
    reason_counts_[r.reason] += 1;
    return r;
  }

  // Apply to a copy so a failure mid-way cannot leave partial state behind.
  SessionState backup = state_;
  CommandResult result;
  try {
    switch (command.kind) {
      case CommandKind::AssignWorkers: result = do_assign_workers(command); break;
      case CommandKind::SetProductionPriority: result = do_set_priority(command); break;
      case CommandKind::SetFacilityIdle: result = do_set_idle(command); break;
      case CommandKind::StartConstruction: result = do_start_construction(command); break;
      case CommandKind::CancelConstruction: result = do_cancel_construction(command); break;
      case CommandKind::ServiceFacility: result = do_service_facility(command); break;
      case CommandKind::SelectPolicy: result = do_select_policy(command); break;
      case CommandKind::UpdateRoute: result = do_update_route(command); break;
      case CommandKind::AuthoriseDeparture: result = do_authorise_departure(command); break;
      case CommandKind::LaunchColonization: result = do_launch_colonization(command); break;
      case CommandKind::ResolveEvent: result = do_resolve_event(command); break;
      case CommandKind::RespondToDemand: result = do_respond_to_demand(command); break;
      case CommandKind::LaunchStrategicMission: result = do_launch_strategic_mission(command); break;
      case CommandKind::RequestRelief: result = do_request_relief(command); break;
      case CommandKind::Surrender: result = do_surrender(command); break;
    }
  } catch (const SimError& e) {
    state_ = backup;
    result = reject(state_.revision, reason::kInvalidArgument, e.what());
  }

  if (!result.accepted) {
    state_ = backup;   // neither state nor random schedule changes
    result.revision = state_.revision;
  } else {
    if (state_.revision == ~0ULL || state_.command_sequence == ~0ULL) {
      throw SimError("session: revision counter exhausted");
    }
    state_.revision += 1;
    state_.command_sequence += 1;
    result.revision = state_.revision;
    result.reason = result.reason.empty() ? reason::kOk : result.reason;
    try {
      sim::validate_invariants(state_, *catalog_);
    } catch (const SimError& e) {
      state_ = backup;
      result = reject(state_.revision, reason::kInvalidArgument,
                      std::string("command would break an invariant: ") + e.what());
    }
  }
  reason_counts_[result.reason] += 1;
  if (!command.id.empty()) {
    RecordedCommandResult rec;
    rec.accepted = result.accepted;
    rec.revision = result.revision;
    rec.reason = result.reason;
    rec.detail = result.detail;
    state_.applied_commands[command.id] = rec;
  }
  return result;
}

// ---------------------------------------------------------------------------
// Workforce
// ---------------------------------------------------------------------------

CommandResult Session::do_assign_workers(const Command& c) {
  PlanetState* planet = state_.find_planet(c.planet_id);
  if (planet == nullptr) return reject(state_.revision, reason::kUnknownPlanet);
  if (c.count <= 0) return reject(state_.revision, reason::kInvalidArgument, "count must be positive");
  if (c.facility_id == c.to_facility_id) {
    return reject(state_.revision, reason::kInvalidArgument, "source and destination are the same");
  }

  FacilityState* from = c.facility_id == 0 ? nullptr : state_.find_facility(c.facility_id);
  FacilityState* to = c.to_facility_id == 0 ? nullptr : state_.find_facility(c.to_facility_id);
  if (c.facility_id != 0 && from == nullptr) return reject(state_.revision, reason::kUnknownFacility);
  if (c.to_facility_id != 0 && to == nullptr) return reject(state_.revision, reason::kUnknownFacility);
  if (from != nullptr && from->planet_id != planet->planet_id) {
    return reject(state_.revision, reason::kInvalidArgument, "workers do not move between worlds outside an expedition");
  }
  if (to != nullptr && to->planet_id != planet->planet_id) {
    return reject(state_.revision, reason::kInvalidArgument, "workers do not move between worlds outside an expedition");
  }

  // Source must actually hold the workers now.
  if (from == nullptr) {
    if (sim::workers_reserve(state_, *planet) < c.count) {
      return reject(state_.revision, reason::kNotEnoughWorkers,
                    "Reserve holds " + std::to_string(sim::workers_reserve(state_, *planet)) + " workers");
    }
  } else {
    const int available = from->construction.has_value() ? from->construction->assigned_workers : from->assigned_workers;
    if (available < c.count) {
      return reject(state_.revision, reason::kNotEnoughWorkers,
                    "that job holds " + std::to_string(available) + " workers");
    }
  }

  // Destination must not be overstaffed, counting workers already in transit.
  if (to != nullptr) {
    if (to->lifecycle == FacilityLifecycle::Cancelled) return reject(state_.revision, reason::kUnknownFacility);
    if (to->construction.has_value()) {
      // A construction crew has no staffing ceiling beyond the planet's workers.
    } else {
      if (to->lifecycle != FacilityLifecycle::Active && to->lifecycle != FacilityLifecycle::Commissioning) {
        return reject(state_.revision, reason::kNotUnderConstruction, "that facility cannot be staffed yet");
      }
      int incoming = 0;
      for (const auto& t : state_.transfers) {
        if (t.to_facility == to->id) incoming += t.count;
      }
      const int cap = recipe_of(*catalog_, *to).staff;
      if (to->assigned_workers + incoming + c.count > cap) {
        return reject(state_.revision, reason::kOverstaffed,
                      "full staff is " + std::to_string(cap) + "; " + std::to_string(to->assigned_workers) +
                          " assigned and " + std::to_string(incoming) + " already in transit");
      }
    }
  }

  // Workers leave the old job immediately and are marked transitioning.
  const bool from_job = from != nullptr;
  const bool to_job = to != nullptr;
  const Day delay = (from_job && to_job) ? catalog_->workforce_rules().job_to_job_days
                                         : catalog_->workforce_rules().reserve_to_job_days;
  if (from != nullptr) {
    if (from->construction.has_value()) {
      from->construction->assigned_workers -= c.count;
    } else {
      from->assigned_workers -= c.count;
    }
  }
  WorkerTransfer t;
  t.id = state_.allocate_id();
  t.planet_id = planet->planet_id;
  t.from_facility = c.facility_id;
  t.to_facility = c.to_facility_id;
  t.count = c.count;
  t.ready_day = checked_add(state_.day, delay);
  state_.transfers.push_back(t);
  std::sort(state_.transfers.begin(), state_.transfers.end(),
            [](const WorkerTransfer& a, const WorkerTransfer& b) { return a.id < b.id; });

  CommandResult r;
  r.accepted = true;
  r.reason = reason::kOk;
  r.detail = "ready on day " + to_decimal_string(t.ready_day);
  r.changed_ids = {t.id};
  if (c.facility_id != 0) r.changed_ids.push_back(c.facility_id);
  if (c.to_facility_id != 0) r.changed_ids.push_back(c.to_facility_id);
  return r;
}

CommandResult Session::do_set_priority(const Command& c) {
  FacilityState* f = state_.find_facility(c.facility_id);
  if (f == nullptr) return reject(state_.revision, reason::kUnknownFacility);
  if (c.priority_band < 1 || c.priority_band > 99) {
    return reject(state_.revision, reason::kInvalidArgument, "priority band must be between 1 and 99");
  }
  f->priority_band = c.priority_band;
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {f->id};
  return r;   // changing a priority does not alter the workforce
}

CommandResult Session::do_set_idle(const Command& c) {
  FacilityState* f = state_.find_facility(c.facility_id);
  if (f == nullptr) return reject(state_.revision, reason::kUnknownFacility);
  f->idle = c.flag;
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {f->id};
  return r;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CommandResult Session::do_start_construction(const Command& c) {
  PlanetState* planet = state_.find_planet(c.planet_id);
  if (planet == nullptr) return reject(state_.revision, reason::kUnknownPlanet);
  if (!planet->colonised) return reject(state_.revision, reason::kDestinationNotColonised);
  const FacilityDef* def = catalog_->find_facility(c.content_id);
  if (def == nullptr) return reject(state_.revision, reason::kUnknownFacility);
  if (!def->buildable) return reject(state_.revision, reason::kNotBuildable, def->id + " is not buildable in P1");

  // Changing an Extraction Site's recipe is not supported, so the variant is
  // fixed when the job starts (TDD 8.1).
  std::string recipe_id = def->recipes.front().id;
  if (!c.recipe_id.empty()) {
    if (def->recipe_index(c.recipe_id) < 0) {
      return reject(state_.revision, reason::kUnknownRecipe, def->id + " has no recipe '" + c.recipe_id + "'");
    }
    recipe_id = c.recipe_id;
  } else if (def->recipes.size() > 1) {
    return reject(state_.revision, reason::kUnknownRecipe,
                  def->id + " has fixed recipe variants; name one explicitly");
  }
  if (def->one_per_world) {
    for (const auto& f : state_.facilities) {
      if (f.planet_id == planet->planet_id && f.facility_id == def->id) {
        return reject(state_.revision, reason::kAlreadyPresent);
      }
    }
  }
  const int slots = catalog_->planet(planet->planet_id).slot_count;
  if (sim::used_slots(state_, planet->planet_id) + def->slots > slots) {
    return reject(state_.revision, reason::kNoFreeSlot,
                  "all " + std::to_string(slots) + " planetary slots are occupied or reserved");
  }
  if (!can_afford(def->build_cost, planet->inventory)) {
    return reject(state_.revision, reason::kInsufficientStock, deficit_text(*catalog_, def->build_cost,
                                                                           planet->inventory));
  }

  FacilityState f;
  f.id = state_.allocate_id();
  f.facility_id = def->id;
  f.recipe_id = recipe_id;
  f.planet_id = planet->planet_id;
  f.lifecycle = FacilityLifecycle::UnderConstruction;
  f.condition_bp = kBpOne;
  f.priority_band = def->recipes[static_cast<std::size_t>(def->recipe_index(recipe_id))].priority_band;
  f.activates_day = std::numeric_limits<Day>::max();
  ConstructionJob job;
  job.work_total = def->build_work;
  job.original_cost = def->build_cost;
  job.escrow = def->build_cost;
  job.started_day = state_.day;
  f.construction = job;
  state_.facilities.push_back(f);
  std::sort(state_.facilities.begin(), state_.facilities.end(),
            [](const FacilityState& a, const FacilityState& b) { return a.id < b.id; });

  // Materials move to escrow immediately and the slot is reserved.
  for (const auto& [idx, qty] : def->build_cost) {
    sim::remove_stock(state_, *planet, idx, qty, sim::escrow_account(f.id), f.id, reason::kCauseConstructionEscrow);
  }
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {f.id};
  r.detail = "job " + to_decimal_string_u(f.id) + " needs " + format_milli(job.work_total) + " person-days";
  return r;
}

CommandResult Session::do_cancel_construction(const Command& c) {
  FacilityState* f = state_.find_facility(c.facility_id);
  if (f == nullptr) return reject(state_.revision, reason::kUnknownFacility);
  if (f->lifecycle != FacilityLifecycle::UnderConstruction || !f->construction.has_value()) {
    return reject(state_.revision, reason::kNotUnderConstruction);
  }
  PlanetState& planet = state_.planet(f->planet_id);
  // Only unconsumed escrow is returned; consumed materials are not refunded.
  for (const auto& [idx, held] : f->construction->escrow) {
    if (held <= 0) continue;
    sim::add_stock(state_, planet, idx, held, sim::escrow_account(f->id), f->id, reason::kCauseConstructionRefund);
  }
  const int crew = f->construction->assigned_workers;
  const InstanceId id = f->id;
  if (crew > 0) {
    WorkerTransfer t;
    t.id = state_.allocate_id();
    t.planet_id = planet.planet_id;
    t.from_facility = id;
    t.to_facility = 0;
    t.count = crew;
    t.ready_day = checked_add(state_.day, 1);
    state_.transfers.push_back(t);
    std::sort(state_.transfers.begin(), state_.transfers.end(),
              [](const WorkerTransfer& a, const WorkerTransfer& b) { return a.id < b.id; });
  }
  state_.facilities.erase(std::remove_if(state_.facilities.begin(), state_.facilities.end(),
                                         [&](const FacilityState& x) { return x.id == id; }),
                          state_.facilities.end());
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {id};
  r.detail = "slot released; consumed materials are not refunded";
  return r;
}

CommandResult Session::do_service_facility(const Command& c) {
  FacilityState* f = state_.find_facility(c.facility_id);
  if (f == nullptr) return reject(state_.revision, reason::kUnknownFacility);
  if (f->lifecycle != FacilityLifecycle::Active) return reject(state_.revision, reason::kNotUnderConstruction);
  const auto& rules = catalog_->condition_rules();
  if (f->condition_bp >= rules.service_max_condition_bp) {
    return reject(state_.revision, reason::kConditionTooHigh,
                  "servicing requires condition below " + format_bp_percent(rules.service_max_condition_bp));
  }
  if (f->service.has_value()) return reject(state_.revision, reason::kServiceInProgress);
  if (f->last_service_completed_day >= 0 &&
      state_.day - f->last_service_completed_day < rules.service_cooldown_days) {
    return reject(state_.revision, reason::kServiceCooldown,
                  "the next service is available on day " +
                      to_decimal_string(f->last_service_completed_day + rules.service_cooldown_days));
  }
  PlanetState& planet = state_.planet(f->planet_id);
  const int machinery = catalog_->machinery();
  if (planet.inventory.available(machinery) < rules.service_machinery_cost) {
    return reject(state_.revision, reason::kInsufficientStock,
                  "servicing costs " + format_milli(rules.service_machinery_cost) + " Machinery; " +
                      format_milli(planet.inventory.available(machinery)) + " is available");
  }
  sim::remove_stock(state_, planet, machinery, rules.service_machinery_cost, account::kSinkEvent, f->id,
                    reason::kCauseServiceCost);
  ServiceJob job;
  job.started_day = state_.day;
  // Production stops for the downtime window; condition lands when it ends.
  job.ready_day = checked_add(state_.day, checked_add(rules.service_downtime_days, 1));
  f->service = job;
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {f->id};
  r.detail = "production stops until day " + to_decimal_string(job.ready_day);
  return r;
}

// ---------------------------------------------------------------------------
// Policies (TDD 12.3)
// ---------------------------------------------------------------------------

CommandResult Session::do_select_policy(const Command& c) {
  PlanetState* planet = state_.find_planet(c.planet_id);
  if (planet == nullptr) return reject(state_.revision, reason::kUnknownPlanet);
  const PolicyDef* next = catalog_->find_policy(c.content_id);
  if (next == nullptr) return reject(state_.revision, reason::kUnknownPolicy);
  if (next->id == planet->policy_id) return reject(state_.revision, reason::kPolicyAlreadyActive);

  const PolicyDef* current = catalog_->find_policy(planet->policy_id);
  if (current != nullptr && !current->is_default) {
    // A policy lasts at least seven actual affected daily steps.
    if (planet->policy_effective_days < current->min_days) {
      return reject(state_.revision, reason::kPolicyMinimumNotMet,
                    current->id + " has run " + std::to_string(planet->policy_effective_days) + " of " +
                        std::to_string(current->min_days) + " required days");
    }
  }
  if (!next->is_default) {
    auto cd = planet->policy_cooldown_until.find(next->id);
    if (cd != planet->policy_cooldown_until.end() && state_.day < cd->second) {
      return reject(state_.revision, reason::kPolicyCooldown,
                    next->id + " can be reapplied on day " + to_decimal_string(cd->second));
    }
  }

  if (current != nullptr && !current->is_default) {
    planet->policy_cooldown_until[current->id] = checked_add(state_.day, current->cooldown_days);
  }
  planet->policy_id = next->id;
  planet->policy_started_day = state_.day;
  planet->policy_effective_days = 0;

  // One-time adherence consequences occur on entering, not once per day, and
  // commit with the command so oscillating while paused cannot farm them.
  int delta = 0;
  auto it = next->adherence_on_entry.find(state_.political.faction_id);
  if (it != next->adherence_on_entry.end()) delta = it->second;
  if (delta != 0) {
    state_.political.adherence = static_cast<int>(clamp_i64(checked_add(state_.political.adherence, delta), 0, 100));
  }
  InstanceId fact = sim::emit_fact(state_, "policy_selected", planet->planet_id, 0,
                                   {{"adherence_delta", delta}, {"adherence", state_.political.adherence}},
                                   {next->id}, "", {}, "");
  sim::emit_news(state_, *catalog_, "news.policy_change", planet->planet_id, {{"adherence_delta", delta}}, {next->id},
                 {fact}, "policy:" + planet->planet_id + ":" + next->id + ":" + to_decimal_string(state_.day), 1);
  CommandResult r;
  r.accepted = true;
  r.detail = "economic effects begin with the next simulation day";
  return r;
}

// ---------------------------------------------------------------------------
// Freight (TDD 10)
// ---------------------------------------------------------------------------

CommandResult Session::do_update_route(const Command& c) {
  RoutePlan& route = state_.route;
  for (const auto& [idx, qty] : c.resources) {
    if (idx < 0 || idx >= catalog_->resource_count()) return reject(state_.revision, reason::kUnknownResource);
    if (qty < 0) return reject(state_.revision, reason::kInvalidArgument, "manifest targets cannot be negative");
  }
  if (c.content_id == "outbound") {
    route.outbound_targets = c.resources;
  } else if (c.content_id == "return") {
    route.return_targets = c.resources;
  } else if (c.content_id == "enable") {
    route.enabled = c.flag;
    if (route.enabled && route.next_departure_day < state_.day) route.next_departure_day = checked_add(state_.day, 1);
  } else if (c.content_id == "floors") {
    for (const auto& [idx, qty] : c.floors) route.source_floors[idx] = qty;
    for (int idx : c.floor_overrides) route.floor_overridden.insert(idx);
  } else if (c.content_id == "interval") {
    if (c.count < 1) return reject(state_.revision, reason::kInvalidArgument, "interval must be at least one day");
    route.departure_interval_days = c.count;
  } else {
    return reject(state_.revision, reason::kInvalidArgument,
                  "route field must be outbound, return, enable, floors or interval");
  }
  CommandResult r;
  r.accepted = true;
  r.detail = "editing the plan does not rewrite an in-flight manifest";
  return r;
}

CommandResult Session::do_authorise_departure(const Command& c) {
  ShipState& ship = state_.ship;
  if (ship.mission != ShipMission::None) {
    return reject(state_.revision, reason::kShipUnavailable, "the freighter is committed to a strategic mission");
  }
  if (ship.phase != ShipPhase::Docked) return reject(state_.revision, reason::kShipNotDocked);
  if (state_.day < ship.earliest_departure_day) {
    return reject(state_.revision, reason::kDwellNotElapsed,
                  "earliest departure is day " + to_decimal_string(ship.earliest_departure_day));
  }
  state_.route.departure_requested = true;
  state_.route.departure_allow_empty = c.flag;
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {ship.id};
  r.detail = "the freighter loads and departs during the next day's port operations";
  return r;
}

// ---------------------------------------------------------------------------
// Colonisation (TDD 11.1-11.2)
// ---------------------------------------------------------------------------

CommandResult Session::do_launch_colonization(const Command& c) {
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  ColonizationState& col = state_.colonisation;
  if (col.launched) return reject(state_.revision, reason::kExpeditionAlreadyLaunched);
  PlanetState* target = state_.find_planet(sc.expedition.target_planet);
  if (target == nullptr) return reject(state_.revision, reason::kUnknownPlanet);
  if (target->colonised) return reject(state_.revision, reason::kTargetAlreadyColonised);
  PlanetState* home = state_.find_planet(sc.freight.origin_planet);
  if (home == nullptr) return reject(state_.revision, reason::kUnknownPlanet);
  if (!c.planet_id.empty() && c.planet_id != target->planet_id) {
    return reject(state_.revision, reason::kInvalidArgument, "P1 has exactly one colonisation target");
  }
  if (!can_afford(sc.expedition.launch_cost, home->inventory)) {
    return reject(state_.revision, reason::kInsufficientStock,
                  deficit_text(*catalog_, sc.expedition.launch_cost, home->inventory));
  }
  const int reserve = sim::workers_reserve(state_, *home);
  if (reserve < sc.expedition.workers) {
    return reject(state_.revision, reason::kNotEnoughWorkers,
                  "the expedition needs " + std::to_string(sc.expedition.workers) +
                      " Reserve workers; Reserve holds " + std::to_string(reserve));
  }
  const People dependents = checked_sub(sc.expedition.residents, sc.expedition.workers);
  const People available_dependents = checked_sub(home->population, home->workers_total);
  if (available_dependents < dependents) {
    return reject(state_.revision, reason::kNotEnoughWorkers,
                  "the expedition needs " + std::to_string(dependents) + " dependents");
  }

  // Debit the exact cost. The travelling portion becomes cargo; the rest is an
  // explicit outfitting and propulsion sink for the one-use charter.
  for (const auto& [idx, qty] : sc.expedition.launch_cost) {
    auto cargo_it = sc.expedition.cargo.find(idx);
    const Milli travels = cargo_it == sc.expedition.cargo.end() ? 0 : cargo_it->second;
    const Milli sunk = checked_sub(qty, travels);
    if (travels > 0) {
      sim::remove_stock(state_, *home, idx, travels, sim::expedition_account(state_.next_instance_id), 0,
                        reason::kCauseExpeditionLaunch);
    }
    if (sunk > 0) {
      sim::remove_stock(state_, *home, idx, sunk, account::kSinkOutfitting, 0, reason::kCauseExpeditionOutfitting);
    }
  }
  col.expedition_id = state_.allocate_id();
  col.launched = true;
  col.target_planet = target->planet_id;
  col.residents = sc.expedition.residents;
  col.workers = sc.expedition.workers;
  col.cargo = sc.expedition.cargo;
  col.launch_day = state_.day;
  col.arrival_day = checked_add(state_.day, sc.expedition.transit_days);

  home->population = checked_sub(home->population, sc.expedition.residents);
  home->workers_total = checked_sub(home->workers_total, sc.expedition.workers);

  state_.flags["colony_launched"] = true;
  state_.flag_days["colony_launched"] = state_.day;
  InstanceId fact = sim::emit_fact(state_, "colony_launched", home->planet_id, col.expedition_id,
                                   {{"residents", col.residents}, {"arrival_day", col.arrival_day}},
                                   {target->planet_id}, "", {}, "");
  sim::emit_news(state_, *catalog_, "news.colony_launched", home->planet_id, {{"arrival_day", col.arrival_day}},
                 {target->planet_id}, {fact}, "colony_launched", 0);
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {col.expedition_id};
  r.detail = "arrival on day " + to_decimal_string(col.arrival_day) + "; launch is irreversible";
  return r;
}

// ---------------------------------------------------------------------------
// Events (TDD 13.2)
// ---------------------------------------------------------------------------

CommandResult Session::do_resolve_event(const Command& c) {
  EventInstance* inst = state_.find_event_instance(c.event_instance_id);
  if (inst == nullptr) return reject(state_.revision, reason::kUnknownEvent);
  if (inst->resolution != EventResolution::Open) return reject(state_.revision, reason::kEventNotOpen);
  const EventDef* def = catalog_->find_event(inst->event_id);
  if (def == nullptr) return reject(state_.revision, reason::kUnknownEvent);
  const EventChoiceDef* choice = nullptr;
  for (const auto& ch : def->choices) {
    if (ch.id == c.content_id) choice = &ch;
  }
  if (choice == nullptr) return reject(state_.revision, reason::kUnknownChoice);
  const std::string resolved_key = to_decimal_string_u(inst->id) + ":" + choice->id;
  if (state_.political.resolved_choices.count(resolved_key) != 0) {
    return reject(state_.revision, reason::kChoiceAlreadyApplied);
  }
  PlanetState* planet = state_.find_planet(inst->planet_id);
  if (planet == nullptr) return reject(state_.revision, reason::kUnknownPlanet);
  if (!can_afford(choice->cost, planet->inventory)) {
    return reject(state_.revision, reason::kChoiceUnaffordable, deficit_text(*catalog_, choice->cost,
                                                                            planet->inventory));
  }
  if (choice->required_reserve_workers > 0 &&
      sim::workers_reserve(state_, *planet) < choice->required_reserve_workers) {
    return reject(state_.revision, reason::kNotEnoughWorkers,
                  "this response needs " + std::to_string(choice->required_reserve_workers) + " Reserve workers");
  }
  if (choice->required_assigned_workers > 0) {
    const FacilityState* f = state_.find_facility(inst->facility_id);
    if (f == nullptr || f->assigned_workers < choice->required_assigned_workers) {
      return reject(state_.revision, reason::kNotEnoughWorkers,
                    "this response needs " + std::to_string(choice->required_assigned_workers) +
                        " workers currently assigned at the site");
    }
  }

  for (const auto& [idx, qty] : choice->cost) {
    sim::remove_stock(state_, *planet, idx, qty, account::kSinkEvent, inst->id, reason::kCauseEventCost);
  }
  // Cancel not-yet-due consequences queued by an earlier choice.
  if (!choice->cancels_scheduled_from.empty()) {
    auto& sched = state_.scheduled;
    sched.erase(std::remove_if(sched.begin(), sched.end(),
                               [&](const ScheduledEffect& s) {
                                 if (s.source_event_instance != inst->id) return false;
                                 for (const auto& from : choice->cancels_scheduled_from) {
                                   if (s.choice_id == from) return true;
                                 }
                                 return false;
                               }),
                sched.end());
    // A pending Worker Demands request that has not opened is also cancelled.
    auto& reqs = state_.event_requests;
    reqs.erase(std::remove_if(reqs.begin(), reqs.end(),
                              [&](const EventRequest& r) {
                                const EventDef* d = catalog_->find_event(r.event_id);
                                return d != nullptr && d->chain_id == def->chain_id && r.event_id != def->id;
                              }),
               reqs.end());
  }

  inst->chosen_choice = choice->id;
  inst->chosen_day = state_.day;
  state_.political.resolved_choices.insert(resolved_key);
  if (choice->resolution == ChoiceResolution::Resolve) {
    inst->resolution = EventResolution::Resolved;
  } else {
    inst->deadline_day = -1;   // remedies stay selectable
  }

  for (std::size_t i = 0; i < choice->effects.size(); ++i) {
    sim::execute_effect(state_, *catalog_, choice->effects[i], def->id, choice->id, "choice", static_cast<int>(i),
                        inst->planet_id, inst->facility_id, inst->id);
  }
  InstanceId fact = sim::emit_fact(state_, "event_choice_made", inst->planet_id, inst->facility_id, {},
                                   {def->id, choice->id}, "", inst->trigger_facts, "");
  if (!choice->news_template.empty()) {
    sim::emit_news(state_, *catalog_, choice->news_template, inst->planet_id, {}, {def->id, choice->id}, {fact},
                   "choice:" + resolved_key, 1);
  }
  bool any_window = false;
  for (const auto& e : state_.events) {
    if (e.resolution == EventResolution::Open && e.deadline_day >= 0) any_window = true;
  }
  if (!any_window) state_.paused_for_decision = false;

  CommandResult r;
  r.accepted = true;
  r.changed_ids = {inst->id};
  return r;
}

// ---------------------------------------------------------------------------
// Strategic demand (TDD 14.2-14.3)
// ---------------------------------------------------------------------------

CommandResult Session::do_respond_to_demand(const Command& c) {
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  DemandState& d = state_.demand;
  if (!d.issued) return reject(state_.revision, reason::kMandateNotIssued);
  if (d.resolved) return reject(state_.revision, reason::kMandateResolved);

  if (c.content_id == "honour") {
    CommandResult r;
    r.accepted = true;
    r.detail = "the original manifest stands";
    return r;
  }
  if (c.content_id == "negotiate") {
    if (d.negotiated) return reject(state_.revision, reason::kInvalidArgument, "already negotiated");
    if (state_.day >= sc.mandate.negotiate_before_day) {
      return reject(state_.revision, reason::kNegotiationWindowClosed,
                    "negotiation closed on day " + to_decimal_string(sc.mandate.negotiate_before_day));
    }
    d.negotiated = true;
    d.required = sc.mandate.negotiated_requirement;
    int delta = 0;
    auto it = sc.mandate.negotiate_adherence.find(state_.political.faction_id);
    if (it != sc.mandate.negotiate_adherence.end()) delta = it->second;
    state_.political.adherence = static_cast<int>(clamp_i64(checked_add(state_.political.adherence, delta), 0, 100));
    std::vector<NamedValue> args;
    for (const auto& [idx, qty] : d.required) args.push_back({catalog_->resource(idx).id, qty});
    InstanceId fact = sim::emit_fact(state_, "mandate_negotiated", "", 0, args, {sc.mandate.id}, "", {}, "");
    sim::emit_news(state_, *catalog_, "news.mandate_negotiated", "", args, {sc.mandate.id}, {fact},
                   "mandate_negotiated", 0);
    CommandResult r;
    r.accepted = true;
    r.detail = "the deadline is unchanged";
    return r;
  }
  if (c.content_id == "decline") {
    d.resolved = true;
    d.decision = MandateDecision::Declined;
    state_.political.adherence =
        static_cast<int>(clamp_i64(checked_add(state_.political.adherence, sc.mandate.fail_adherence), 0, 100));
    InstanceId fact = sim::emit_fact(state_, "mandate_declined", "", 0,
                                     {{"adherence", state_.political.adherence}}, {sc.mandate.id}, "", {}, "");
    sim::emit_news(state_, *catalog_, "news.mandate_declined", "", {}, {sc.mandate.id}, {fact}, "mandate_resolved", 0);
    CommandResult r;
    r.accepted = true;
    r.detail = "the scenario remains playable; the outcome can no longer be Complete";
    return r;
  }
  return reject(state_.revision, reason::kInvalidArgument, "response must be honour, negotiate or decline");
}

CommandResult Session::do_launch_strategic_mission(const Command& c) {
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  DemandState& d = state_.demand;
  ShipState& ship = state_.ship;
  if (!d.issued) return reject(state_.revision, reason::kMandateNotIssued);
  if (d.resolved) return reject(state_.revision, reason::kMandateResolved);
  if (d.mission_requested) return reject(state_.revision, reason::kShipUnavailable, "a mission is already committed");
  if (ship.mission != ShipMission::None) return reject(state_.revision, reason::kShipUnavailable);
  if (ship.phase != ShipPhase::Docked || ship.location_planet != sc.freight.origin_planet) {
    return reject(state_.revision, reason::kShipNotDocked, "the freighter must be docked at the home world");
  }
  if (!ship.cargo.empty()) return reject(state_.revision, reason::kShipHasCargo);
  if (state_.day > sc.mandate.last_departure_day) {
    return reject(state_.revision, reason::kDepartureTooLate,
                  "the last supply mission must depart no later than day " +
                      to_decimal_string(sc.mandate.last_departure_day));
  }
  if (c.resources.empty()) return reject(state_.revision, reason::kInvalidArgument, "the manifest is empty");

  PlanetState& home = state_.planet(sc.freight.origin_planet);
  ResourceMap manifest;
  for (const auto& [idx, qty] : c.resources) {
    if (qty <= 0) continue;
    manifest[idx] = qty;
  }
  // Quantities beyond the remaining demand are rejected.
  for (const auto& [idx, qty] : manifest) {
    auto need_it = d.required.find(idx);
    if (need_it == d.required.end()) {
      return reject(state_.revision, reason::kManifestExceedsDemand,
                    catalog_->resource(idx).id + " is not part of the mandate");
    }
    Milli already = d.delivered.count(idx) != 0 ? d.delivered.at(idx) : 0;
    Milli remaining = checked_sub(need_it->second, already);
    if (qty > remaining) {
      return reject(state_.revision, reason::kManifestExceedsDemand,
                    catalog_->resource(idx).id + " remaining demand is " + format_milli(remaining));
    }
  }
  const Milli volume = sim::cargo_volume(*catalog_, manifest);
  if (volume > sc.freight.cargo_capacity) {
    return reject(state_.revision, reason::kManifestExceedsCapacity,
                  "the manifest is " + format_milli(volume) + " cargo volume; capacity is " +
                      format_milli(sc.freight.cargo_capacity));
  }
  if (home.last_day.port_handling_capacity < volume) {
    return reject(state_.revision, reason::kInsufficientHandling,
                  "the home dock handled " + format_milli(home.last_day.port_handling_capacity) +
                      " cargo volume yesterday; the manifest needs " + format_milli(volume));
  }
  ResourceMap needed = manifest;
  needed[catalog_->fuel()] = checked_add(needed.count(catalog_->fuel()) != 0 ? needed.at(catalog_->fuel()) : 0,
                                         sc.mandate.mission_fuel);
  needed[catalog_->machinery()] = checked_add(
      needed.count(catalog_->machinery()) != 0 ? needed.at(catalog_->machinery()) : 0, sc.mandate.mission_machinery);
  if (!can_afford(needed, home.inventory)) {
    return reject(state_.revision, reason::kInsufficientStock, deficit_text(*catalog_, needed, home.inventory));
  }

  d.pending_manifest = manifest;
  d.mission_requested = true;
  d.mission_requested_day = state_.day;
  d.freighter_committed = true;
  CommandResult r;
  r.accepted = true;
  r.changed_ids = {ship.id};
  r.detail = "the freighter is unavailable for colonial trips for " +
             to_decimal_string(sc.mandate.mission_outbound_days + sc.mandate.mission_service_days +
                               sc.mandate.mission_return_days) +
             " days";
  return r;
}

// ---------------------------------------------------------------------------
// Relief and surrender (TDD 14.4)
// ---------------------------------------------------------------------------

CommandResult Session::do_request_relief(const Command& c) {
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  PlanetState* planet = state_.find_planet(c.planet_id);
  if (planet == nullptr) return reject(state_.revision, reason::kUnknownPlanet);
  if (!planet->survival_emergency_active) return reject(state_.revision, reason::kNoSurvivalEmergency);
  if (planet->relief_used) return reject(state_.revision, reason::kReliefAlreadyUsed);
  if (planet->relief_pending_day >= 0) return reject(state_.revision, reason::kReliefPending);
  planet->relief_used = true;
  planet->relief_pending_day = checked_add(state_.day, sc.relief.delivery_delay_days);
  state_.political.adherence =
      static_cast<int>(clamp_i64(checked_add(state_.political.adherence, sc.relief.adherence_cost), 0, 100));
  state_.flags[sc.relief.flag] = true;
  state_.flag_days[sc.relief.flag] = state_.day;
  InstanceId fact = sim::emit_fact(state_, "relief_requested", planet->planet_id, 0,
                                   {{"arrives_day", planet->relief_pending_day},
                                    {"adherence", state_.political.adherence}},
                                   {}, reason::kCauseReliefGrant, {}, "");
  sim::emit_news(state_, *catalog_, "news.relief_requested", planet->planet_id,
                 {{"arrives_day", planet->relief_pending_day}}, {}, {fact}, "relief_requested:" + planet->planet_id, 0);
  CommandResult r;
  r.accepted = true;
  r.detail = "the grant arrives on day " + to_decimal_string(planet->relief_pending_day);
  return r;
}

CommandResult Session::do_surrender(const Command& c) {
  (void)c;
  if (state_.lifecycle != SessionLifecycle::Running) return reject(state_.revision, reason::kScenarioFinished);
  state_.lifecycle = SessionLifecycle::Surrendered;
  InstanceId fact = sim::emit_fact(state_, "surrendered", "", 0, {{"day", state_.day}}, {}, "", {}, "");
  sim::emit_news(state_, *catalog_, "news.surrendered", "", {{"day", state_.day}}, {}, {fact}, "surrendered", 0);
  CommandResult r;
  r.accepted = true;
  r.detail = "the history report is preserved";
  return r;
}

const char* command_kind_id(CommandKind k) {
  switch (k) {
    case CommandKind::AssignWorkers: return "assign_workers";
    case CommandKind::SetProductionPriority: return "set_production_priority";
    case CommandKind::SetFacilityIdle: return "set_facility_idle";
    case CommandKind::StartConstruction: return "start_construction";
    case CommandKind::CancelConstruction: return "cancel_construction";
    case CommandKind::ServiceFacility: return "service_facility";
    case CommandKind::SelectPolicy: return "select_policy";
    case CommandKind::UpdateRoute: return "update_route";
    case CommandKind::AuthoriseDeparture: return "authorise_departure";
    case CommandKind::LaunchColonization: return "launch_colonization";
    case CommandKind::ResolveEvent: return "resolve_event";
    case CommandKind::RespondToDemand: return "respond_to_demand";
    case CommandKind::LaunchStrategicMission: return "launch_strategic_mission";
    case CommandKind::RequestRelief: return "request_relief";
    case CommandKind::Surrender: return "surrender";
  }
  return "surrender";
}

std::optional<CommandKind> parse_command_kind(const std::string& s) {
  static const std::pair<const char*, CommandKind> kNames[] = {
      {"assign_workers", CommandKind::AssignWorkers},
      {"set_production_priority", CommandKind::SetProductionPriority},
      {"set_facility_idle", CommandKind::SetFacilityIdle},
      {"start_construction", CommandKind::StartConstruction},
      {"cancel_construction", CommandKind::CancelConstruction},
      {"service_facility", CommandKind::ServiceFacility},
      {"select_policy", CommandKind::SelectPolicy},
      {"update_route", CommandKind::UpdateRoute},
      {"authorise_departure", CommandKind::AuthoriseDeparture},
      {"launch_colonization", CommandKind::LaunchColonization},
      {"resolve_event", CommandKind::ResolveEvent},
      {"respond_to_demand", CommandKind::RespondToDemand},
      {"launch_strategic_mission", CommandKind::LaunchStrategicMission},
      {"request_relief", CommandKind::RequestRelief},
      {"surrender", CommandKind::Surrender},
  };
  for (const auto& [name, kind] : kNames) {
    if (s == name) return kind;
  }
  return std::nullopt;
}

}  // namespace expansion
