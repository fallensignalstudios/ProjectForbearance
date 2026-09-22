// Phase 8 of TDD 5.2 plus colonisation and the strategic mission (TDD 10-11, 14.2).
#include <algorithm>
#include <limits>

#include "sim_internal.hpp"

namespace expansion {

using sim::def_of;
using sim::recipe_of;

namespace {

Milli volume_of(const Catalog& cat, int resource, Milli qty) {
  return mul_div_ceil(qty, cat.resource(resource).volume_per_unit, kMilliOne);
}

Milli quantity_for_volume(const Catalog& cat, int resource, Milli volume) {
  const Milli per_unit = cat.resource(resource).volume_per_unit;
  if (per_unit <= 0) return std::numeric_limits<Milli>::max() / 2;
  return mul_div_floor(volume, kMilliOne, per_unit);
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 8: freight. Unload arrived ships using today's port service capacity,
// then depart eligible docked ships.
// ---------------------------------------------------------------------------

void Session::phase_freight() {
  unload_arrivals();

  // A committed strategic mission takes the ship before any colonial departure:
  // it is the prototype's central shared-capacity tradeoff (TDD 10.4, 14.2).
  depart_strategic_mission();

  RoutePlan& route = state_.route;
  ShipState& ship = state_.ship;
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);

  // An explicit authorisation works whether or not the route runs on a
  // schedule, so a one-off or repositioning trip does not need the plan enabled.
  if (route.departure_requested) {
    const bool available = ship.mission == ShipMission::None && ship.phase == ShipPhase::Docked &&
                           ship.location_planet == route.origin_planet &&
                           state_.day >= ship.earliest_departure_day;
    if (available) {
      CommandResult r = depart_colony_leg(route.departure_allow_empty, false);
      if (r.accepted) {
        route.departure_requested = false;
        route.departure_allow_empty = false;
        route.next_departure_day = checked_add(state_.day, route.departure_interval_days);
      } else {
        route.departure_requested = false;
        route.departure_allow_empty = false;
        sim::emit_fact(state_, "authorised_departure_blocked", route.origin_planet, ship.id, {}, {r.reason}, r.reason,
                       {}, "");
      }
    }
  }

  // Scheduled departures only run on an enabled route.
  if (route.enabled && route.next_departure_day <= state_.day) {
    const bool available = ship.mission == ShipMission::None && ship.phase == ShipPhase::Docked &&
                           ship.location_planet == route.origin_planet &&
                           state_.day >= ship.earliest_departure_day;
    if (available) {
      CommandResult r = depart_colony_leg(false, true);
      if (r.accepted) {
        route.next_departure_day = checked_add(state_.day, route.departure_interval_days);
      } else {
        // The departure could not be loaded today; try again tomorrow, and
        // record a missed manifest when the ship itself was diverted.
        sim::emit_fact(state_, "scheduled_departure_blocked", route.origin_planet, ship.id, {}, {r.reason}, r.reason,
                       {}, "");
        route.next_departure_day = checked_add(state_.day, 1);
      }
    } else {
      // The scheduled departure day passed while the ship was unavailable.
      auto food_it = route.outbound_targets.find(catalog_->food());
      const bool food_positive = food_it != route.outbound_targets.end() && food_it->second > 0;
      if (food_positive) {
        state_.history.missed_colonial_food_manifests += 1;
        InstanceId fact = sim::emit_fact(state_, "missed_colonial_manifest", route.destination_planet, ship.id,
                                         {{"food_target", food_it->second}}, {std::string(ship_mission_id(ship.mission))},
                                         reason::kShipUnavailable, {}, "");
        sim::emit_news(state_, *catalog_, "news.missed_colonial_manifest", route.destination_planet,
                       {{"food_target", food_it->second}}, {}, {fact},
                       "missed_manifest:" + to_decimal_string(route.next_departure_day), 1);
      }
      route.next_departure_day = checked_add(route.next_departure_day, route.departure_interval_days);
    }
  }

  // Return leg: loading begins only after all inbound cargo is unloaded.
  if (route.enabled && ship.mission == ShipMission::None && ship.phase == ShipPhase::Docked &&
      ship.location_planet == route.destination_planet && ship.cargo.empty() &&
      state_.day >= ship.earliest_departure_day) {
    PlanetState& src = state_.planet(route.destination_planet);
    PlanetState& dst = state_.planet(route.origin_planet);
    auto& sps = scratch_->at(src.planet_id);
    Milli volume_left = sc.freight.cargo_capacity;
    ResourceMap loaded;
    for (const auto& [idx, target] : route.return_targets) {
      if (target <= 0) continue;
      Milli floor_qty = route.floor_overridden.count(idx) != 0
                            ? 0
                            : (route.source_floors.count(idx) != 0
                                   ? route.source_floors.at(idx)
                                   : sim::default_reserve_floor(state_, *catalog_, src, idx));
      Milli spare = checked_sub(src.inventory.available(idx), floor_qty);
      if (spare <= 0) continue;
      Milli want = std::min<Milli>(target, spare);
      want = std::min<Milli>(want, quantity_for_volume(*catalog_, idx, volume_left));
      want = std::min<Milli>(want, quantity_for_volume(*catalog_, idx, sps.port_handling_remaining));
      want = std::min<Milli>(want, dst.inventory.free_space(idx));
      if (want <= 0) continue;
      loaded[idx] = want;
      volume_left = checked_sub(volume_left, volume_of(*catalog_, idx, want));
      sps.port_handling_remaining = checked_sub(sps.port_handling_remaining, volume_of(*catalog_, idx, want));
    }
    // A return trip may be empty; the ship is occupied either way.
    for (const auto& [idx, qty] : loaded) {
      sim::remove_stock(state_, src, idx, qty, sim::ship_account(ship.id), ship.id, reason::kCauseShipLoad);
      ship.cargo[idx] = checked_add(ship.cargo.count(idx) != 0 ? ship.cargo.at(idx) : 0, qty);
      ship.booking_claims.push_back(sim::claim_space(state_, dst, idx, qty, ship.id));
    }
    // Burn the return fuel carried from home; the destination needs no fuel.
    if (ship.tank < sc.freight.return_fuel_burn) {
      sim::emit_fact(state_, "return_departure_blocked", src.planet_id, ship.id, {{"tank", ship.tank}}, {},
                     reason::kInsufficientFuel, {}, "");
    } else {
      ship.tank = checked_sub(ship.tank, sc.freight.return_fuel_burn);
      sim::record(state_, catalog_->fuel(), sc.freight.return_fuel_burn, sim::ship_account(ship.id),
                  account::kSinkPropulsion, ship.id, reason::kCauseShipFuel);
      ship.phase = ShipPhase::Transit;
      ship.origin_planet = src.planet_id;
      ship.destination_planet = dst.planet_id;
      ship.location_planet.clear();
      ship.departure_day = state_.day;
      ship.arrival_day = checked_add(state_.day, sc.freight.leg_days);
      ship.unloaded_so_far.clear();
      ship.shipment_id = state_.allocate_id();
      InstanceId fact = sim::emit_fact(state_, "ship_departed", src.planet_id, ship.id,
                                       {{"cargo_volume", sim::cargo_volume(*catalog_, ship.cargo)},
                                        {"arrival_day", ship.arrival_day}},
                                       {"return"}, "", {}, "");
      if (!ship.cargo.empty()) {
        sim::emit_news(state_, *catalog_, "news.first_return_delivery_departed", src.planet_id, {}, {}, {fact},
                       "first_return_departure", 1);
      }
    }
  }

  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    planet.last_day.port_handling_capacity = ps.port_handling_capacity;
    planet.last_day.port_handling_used = checked_sub(ps.port_handling_capacity, ps.port_handling_remaining);
  }
}

void Session::unload_arrivals() {
  ShipState& ship = state_.ship;
  if (ship.phase != ShipPhase::ArrivedHolding && ship.phase != ShipPhase::Unloading) return;
  if (ship.cargo.empty()) {
    ship.phase = ShipPhase::Docked;
    ship.earliest_departure_day =
        checked_add(state_.day, catalog_->scenario(state_.scenario_id).freight.min_dwell_days);
    return;
  }
  PlanetState* planet = state_.find_planet(ship.location_planet);
  if (planet == nullptr) throw SimError("freight: ship is holding at an unknown planet");
  auto& ps = scratch_->at(planet->planet_id);

  // A fully blocked or unpowered destination keeps the ship in ArrivedHolding
  // with its cargo aboard.
  if (!sim::has_operable_spaceport(state_, *catalog_, planet->planet_id, state_.day) ||
      ps.port_handling_remaining <= 0) {
    ship.phase = ShipPhase::ArrivedHolding;
    sim::emit_fact(state_, "unload_blocked", planet->planet_id, ship.id,
                   {{"handling_remaining", ps.port_handling_remaining}}, {}, reason::kInsufficientHandling, {}, "");
    return;
  }

  // Release this shipment's destination claims; production already ran with
  // them in force, so the promised space is there.
  for (InstanceId claim : ship.booking_claims) {
    auto& v = planet->inventory.incoming_claims;
    auto it = std::find_if(v.begin(), v.end(), [&](const IncomingClaim& c) { return c.id == claim; });
    if (it != v.end()) v.erase(it);
  }
  ship.booking_claims.clear();

  ship.phase = ShipPhase::Unloading;
  // Deterministic order: ascending resource index.
  std::vector<int> order;
  for (const auto& [idx, qty] : ship.cargo) {
    if (qty > 0) order.push_back(idx);
  }
  std::sort(order.begin(), order.end());
  for (int idx : order) {
    Milli aboard = ship.cargo.at(idx);
    Milli by_handling = quantity_for_volume(*catalog_, idx, ps.port_handling_remaining);
    Milli room = planet->inventory.free_space(idx);
    Milli move = std::min<Milli>({aboard, by_handling, room});
    if (move <= 0) continue;
    Milli added = sim::add_stock(state_, *planet, idx, move, sim::ship_account(ship.id), ship.shipment_id,
                                 reason::kCauseShipUnload);
    ship.cargo[idx] = checked_sub(aboard, added);
    // Idempotent per shipment, resource and transferred quantity.
    ship.unloaded_so_far[idx] = checked_add(ship.unloaded_so_far.count(idx) != 0 ? ship.unloaded_so_far.at(idx) : 0,
                                            added);
    ps.port_handling_remaining = checked_sub(ps.port_handling_remaining, volume_of(*catalog_, idx, added));
    if (added > 0) {
      InstanceId fact = sim::emit_fact(state_, "cargo_unloaded", planet->planet_id, ship.shipment_id,
                                       {{"quantity", added}}, {catalog_->resource(idx).id}, reason::kCauseShipUnload,
                                       {}, "");
      const std::string direction = planet->planet_id == state_.route.destination_planet ? "outbound" : "inbound";
      state_.flags["delivery_" + direction] = true;
      if (state_.flag_days.count("delivery_" + direction) == 0) {
        state_.flag_days["delivery_" + direction] = state_.day;
      }
      sim::emit_news(state_, *catalog_, "news.first_delivery", planet->planet_id, {{"quantity", added}},
                     {catalog_->resource(idx).id}, {fact}, "first_delivery:" + direction, 0);
    }
  }
  // Drop emptied entries so an empty cargo map means an empty hold.
  for (auto it = ship.cargo.begin(); it != ship.cargo.end();) {
    if (it->second <= 0) {
      it = ship.cargo.erase(it);
    } else {
      ++it;
    }
  }
  if (ship.cargo.empty()) {
    ship.phase = ShipPhase::Docked;
    ship.earliest_departure_day =
        checked_add(state_.day, catalog_->scenario(state_.scenario_id).freight.min_dwell_days);
    ship.unloaded_so_far.clear();
  }
}

CommandResult Session::depart_colony_leg(bool allow_empty, bool scheduled) {
  CommandResult result;
  result.revision = state_.revision;
  ShipState& ship = state_.ship;
  RoutePlan& route = state_.route;
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);

  if (ship.mission != ShipMission::None) {
    result.reason = reason::kShipUnavailable;
    result.detail = "the freighter is committed to a strategic mission";
    return result;
  }
  if (ship.phase != ShipPhase::Docked) {
    result.reason = reason::kShipNotDocked;
    return result;
  }
  if (ship.location_planet != route.origin_planet) {
    result.reason = reason::kShipNotDocked;
    result.detail = "the freighter is not at the route origin";
    return result;
  }
  if (state_.day < ship.earliest_departure_day) {
    result.reason = reason::kDwellNotElapsed;
    result.detail = "earliest departure is day " + to_decimal_string(ship.earliest_departure_day);
    return result;
  }
  if (!ship.cargo.empty()) {
    result.reason = reason::kShipHasCargo;
    return result;
  }
  PlanetState& src = state_.planet(route.origin_planet);
  PlanetState* dst = state_.find_planet(route.destination_planet);
  if (dst == nullptr) {
    result.reason = reason::kUnknownPlanet;
    return result;
  }
  if (!dst->colonised) {
    result.reason = reason::kDestinationNotColonised;
    return result;
  }
  if (!sim::has_operable_spaceport(state_, *catalog_, dst->planet_id, state_.day)) {
    result.reason = reason::kNoSpaceport;
    result.detail = "no freighter can deliver before a functioning Spaceport exists at " + dst->planet_id;
    return result;
  }
  const int fuel = catalog_->fuel();
  const int machinery = catalog_->machinery();
  if (src.inventory.available(fuel) < sc.freight.round_trip_fuel) {
    result.reason = reason::kInsufficientFuel;
    result.detail = "the round trip needs " + format_milli(sc.freight.round_trip_fuel) + " Fuel; " +
                    format_milli(src.inventory.available(fuel)) + " is available";
    return result;
  }
  if (src.inventory.available(machinery) < sc.freight.round_trip_machinery) {
    result.reason = reason::kInsufficientStock;
    result.detail = "the round trip needs " + format_milli(sc.freight.round_trip_machinery) + " Machinery";
    return result;
  }

  // Compute loadable quantities from source stock, source floors, ship volume,
  // today's origin handling capacity, and destination capacity.
  Milli handling_remaining = std::numeric_limits<Milli>::max() / 4;
  DayScratch::PlanetScratch* ps = nullptr;
  if (scratch_) {
    ps = &scratch_->at(src.planet_id);
    handling_remaining = ps->port_handling_remaining;
  }
  Milli volume_left = sc.freight.cargo_capacity;
  ResourceMap loaded;
  std::vector<std::string> floor_notes;
  for (const auto& [idx, target] : route.outbound_targets) {
    if (target <= 0) continue;
    Milli floor_qty = route.floor_overridden.count(idx) != 0
                          ? 0
                          : (route.source_floors.count(idx) != 0
                                 ? route.source_floors.at(idx)
                                 : sim::default_reserve_floor(state_, *catalog_, src, idx));
    Milli spare = checked_sub(src.inventory.available(idx), floor_qty);
    if (spare <= 0) {
      floor_notes.push_back(catalog_->resource(idx).id);
      continue;
    }
    Milli want = std::min<Milli>(target, spare);
    want = std::min<Milli>(want, quantity_for_volume(*catalog_, idx, volume_left));
    want = std::min<Milli>(want, quantity_for_volume(*catalog_, idx, handling_remaining));
    want = std::min<Milli>(want, dst->inventory.free_space(idx));
    if (want <= 0) continue;
    loaded[idx] = want;
    volume_left = checked_sub(volume_left, volume_of(*catalog_, idx, want));
    handling_remaining = checked_sub(handling_remaining, volume_of(*catalog_, idx, want));
  }
  if (loaded.empty() && !allow_empty) {
    result.reason = reason::kZeroCargoNotAuthorised;
    result.detail = floor_notes.empty() ? "nothing loadable for the configured manifest"
                                        : "reserve floors held back every target resource";
    return result;
  }

  // Atomic commit: fuel, maintenance, cargo, destination claims, departure.
  sim::remove_stock(state_, src, fuel, sc.freight.round_trip_fuel, sim::ship_account(ship.id), ship.id,
                    reason::kCauseShipFuel);
  ship.tank = checked_add(ship.tank, sc.freight.round_trip_fuel);
  sim::remove_stock(state_, src, machinery, sc.freight.round_trip_machinery, account::kSinkPropulsion, ship.id,
                    reason::kCauseShipMaintenance);
  ship.tank = checked_sub(ship.tank, sc.freight.outbound_fuel_burn);
  sim::record(state_, fuel, sc.freight.outbound_fuel_burn, sim::ship_account(ship.id), account::kSinkPropulsion,
              ship.id, reason::kCauseShipFuel);
  for (const auto& [idx, qty] : loaded) {
    sim::remove_stock(state_, src, idx, qty, sim::ship_account(ship.id), ship.id, reason::kCauseShipLoad);
    ship.cargo[idx] = checked_add(ship.cargo.count(idx) != 0 ? ship.cargo.at(idx) : 0, qty);
    ship.booking_claims.push_back(sim::claim_space(state_, *dst, idx, qty, ship.id));
  }
  if (ps != nullptr) ps->port_handling_remaining = handling_remaining;

  ship.phase = ShipPhase::Transit;
  ship.origin_planet = src.planet_id;
  ship.destination_planet = dst->planet_id;
  ship.location_planet.clear();
  ship.departure_day = state_.day;
  ship.arrival_day = checked_add(state_.day, sc.freight.leg_days);
  ship.shipment_id = state_.allocate_id();
  ship.unloaded_so_far.clear();

  InstanceId fact = sim::emit_fact(state_, "ship_departed", src.planet_id, ship.id,
                                   {{"cargo_volume", sim::cargo_volume(*catalog_, ship.cargo)},
                                    {"arrival_day", ship.arrival_day}},
                                   {scheduled ? "scheduled" : "authorised"}, "", {}, "");
  result.accepted = true;
  result.reason = reason::kOk;
  result.changed_ids.push_back(ship.id);
  (void)fact;
  return result;
}

// ---------------------------------------------------------------------------
// Strategic mission departure (TDD 14.2). Loaded atomically through the home
// dock's normal handling budget, or not at all.
// ---------------------------------------------------------------------------

void Session::depart_strategic_mission() {
  DemandState& d = state_.demand;
  if (!d.mission_requested) return;
  ShipState& ship = state_.ship;
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  if (ship.mission != ShipMission::None || ship.phase != ShipPhase::Docked ||
      ship.location_planet != sc.freight.origin_planet || !ship.cargo.empty()) {
    return;
  }
  if (state_.day < ship.earliest_departure_day) return;
  PlanetState& home = state_.planet(sc.freight.origin_planet);
  auto& ps = scratch_->at(home.planet_id);
  const Milli volume = sim::cargo_volume(*catalog_, d.pending_manifest);

  const int fuel = catalog_->fuel();
  const int machinery = catalog_->machinery();
  ResourceMap needed = d.pending_manifest;
  needed[fuel] = checked_add(needed.count(fuel) != 0 ? needed.at(fuel) : 0, sc.mandate.mission_fuel);
  needed[machinery] =
      checked_add(needed.count(machinery) != 0 ? needed.at(machinery) : 0, sc.mandate.mission_machinery);
  for (const auto& [idx, qty] : needed) {
    if (home.inventory.available(idx) < qty) {
      sim::emit_fact(state_, "mission_departure_blocked", home.planet_id, ship.id, {{"required", qty}},
                     {catalog_->resource(idx).id}, reason::kInsufficientStock, {}, "");
      return;
    }
  }
  if (ps.port_handling_remaining < volume) {
    sim::emit_fact(state_, "mission_departure_blocked", home.planet_id, ship.id,
                   {{"handling_remaining", ps.port_handling_remaining}, {"manifest_volume", volume}}, {},
                   reason::kInsufficientHandling, {}, "");
    return;
  }
  if (state_.day > sc.mandate.last_departure_day) {
    d.mission_requested = false;
    d.pending_manifest.clear();
    d.freighter_committed = false;
    sim::emit_fact(state_, "mission_departure_abandoned", home.planet_id, ship.id, {}, {},
                   reason::kDepartureTooLate, {}, "");
    return;
  }

  for (const auto& [idx, qty] : d.pending_manifest) {
    sim::remove_stock(state_, home, idx, qty, sim::ship_account(ship.id), ship.id, reason::kCauseMandateLoad);
    ship.cargo[idx] = checked_add(ship.cargo.count(idx) != 0 ? ship.cargo.at(idx) : 0, qty);
  }
  // Mission Fuel is an explicit upfront voyage cost, not stored in the normal
  // four-unit round-trip tank.
  sim::remove_stock(state_, home, fuel, sc.mandate.mission_fuel, account::kSinkPropulsion, ship.id,
                    reason::kCauseMandatePropulsion);
  sim::remove_stock(state_, home, machinery, sc.mandate.mission_machinery, account::kSinkPropulsion, ship.id,
                    reason::kCauseShipMaintenance);
  ps.port_handling_remaining = checked_sub(ps.port_handling_remaining, volume);

  MandateMission m;
  m.id = state_.allocate_id();
  m.departure_day = state_.day;
  m.delivery_day = checked_add(state_.day, sc.mandate.mission_outbound_days);
  m.manifest = d.pending_manifest;
  state_.demand.missions.push_back(m);

  ship.phase = ShipPhase::Transit;
  ship.mission = ShipMission::StrategicOutbound;
  ship.origin_planet = home.planet_id;
  ship.destination_planet.clear();
  ship.location_planet.clear();
  ship.departure_day = state_.day;
  ship.arrival_day = m.delivery_day;
  ship.mission_delivery_day = m.delivery_day;
  ship.mission_delivered = false;
  d.mission_requested = false;
  d.pending_manifest.clear();
  d.freighter_committed = true;

  std::vector<NamedValue> args;
  for (const auto& [idx, qty] : m.manifest) args.push_back({catalog_->resource(idx).id, qty});
  InstanceId fact = sim::emit_fact(state_, "mission_departed", home.planet_id, ship.id, args,
                                   {to_decimal_string(m.delivery_day)}, reason::kCauseMandateLoad, {}, "");
  sim::emit_news(state_, *catalog_, "news.mission_departed", home.planet_id, args, {}, {fact},
                 "mission_departed:" + to_decimal_string_u(m.id), 1);
}

// ---------------------------------------------------------------------------
// Colonisation (TDD 11)
// ---------------------------------------------------------------------------

void Session::found_colony() {
  ColonizationState& col = state_.colonisation;
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  PlanetState& target = state_.planet(col.target_planet);
  if (target.colonised) throw SimError("colonisation: the target is already colonised");

  target.colonised = true;
  target.population = checked_add(target.population, col.residents);
  target.workers_total = checked_add(target.workers_total, col.workers);

  // Deliver the surviving cargo. The Hub is not charged again at arrival.
  std::vector<NamedValue> delivered;
  for (const auto& [idx, qty] : col.cargo) {
    if (qty <= 0) continue;
    Milli added = sim::add_stock(state_, target, idx, qty, sim::expedition_account(col.expedition_id),
                                col.expedition_id, reason::kCauseExpeditionDeliver);
    delivered.push_back({catalog_->resource(idx).id, added});
  }
  col.cargo.clear();

  // Create the founding Hub, once, unstaffed. All arriving workers enter Reserve.
  const FacilityDef& hub = catalog_->facility(sc.expedition.hub_facility_id);
  FacilityState f;
  f.id = state_.allocate_id();
  f.facility_id = hub.id;
  f.recipe_id = hub.recipes.front().id;
  f.planet_id = target.planet_id;
  f.lifecycle = FacilityLifecycle::Active;
  f.assigned_workers = 0;
  f.condition_bp = kBpOne;
  f.priority_band = hub.recipes.front().priority_band;
  f.activates_day = state_.day;
  state_.facilities.push_back(f);
  std::sort(state_.facilities.begin(), state_.facilities.end(),
            [](const FacilityState& a, const FacilityState& b) { return a.id < b.id; });

  col.founded = true;
  col.founded_day = state_.day;
  state_.flags["colony_founded"] = true;
  state_.flag_days["colony_founded"] = state_.day;

  InstanceId fact = sim::emit_fact(state_, "colony_founded", target.planet_id, f.id,
                                   {{"residents", col.residents}, {"workers", col.workers}}, {}, "", {}, "");
  sim::emit_news(state_, *catalog_, "news.colony_founded", target.planet_id, delivered, {}, {fact}, "colony_founded",
                 0);
}

// ---------------------------------------------------------------------------
// Strategic mission delivery (TDD 14.2)
// ---------------------------------------------------------------------------

void Session::credit_strategic_delivery() {
  ShipState& ship = state_.ship;
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  std::vector<NamedValue> accepted;
  for (const auto& [idx, qty] : ship.cargo) {
    if (qty <= 0) continue;
    // Cargo leaves sector ownership on acceptance at the front.
    sim::record(state_, idx, qty, sim::ship_account(ship.id), account::kFront, ship.id, reason::kCauseMandateAccepted);
    state_.demand.delivered[idx] = checked_add(state_.demand.delivered.count(idx) != 0
                                                   ? state_.demand.delivered.at(idx)
                                                   : 0,
                                               qty);
    accepted.push_back({catalog_->resource(idx).id, qty});
  }
  ship.cargo.clear();
  for (auto& m : state_.demand.missions) {
    if (m.delivery_day == state_.day && !m.delivered) m.delivered = true;
  }
  ship.mission = ShipMission::StrategicReturn;
  ship.phase = ShipPhase::Transit;
  ship.destination_planet = sc.freight.origin_planet;
  ship.location_planet.clear();
  ship.arrival_day = checked_add(state_.day, sc.mandate.mission_return_days + sc.mandate.mission_service_days);
  ship.mission_delivered = true;

  InstanceId fact = sim::emit_fact(state_, "mandate_delivery_accepted", "", ship.id, accepted, {}, "", {}, "");
  sim::emit_news(state_, *catalog_, "news.mandate_delivery", "", accepted, {}, {fact},
                 "mandate_delivery:" + to_decimal_string(state_.day), 1);
}

}  // namespace expansion
