// The daily resolver: StepDay and phases 1-3 and 10 of TDD 5.2.
#include <algorithm>
#include <cstddef>

#include "expansion/metrics.hpp"
#include "expansion/sha256.hpp"
#include "sim_internal.hpp"

namespace expansion {

using sim::def_of;
using sim::recipe_of;

DayResult Session::step_day() {
  if (state_.lifecycle != SessionLifecycle::Running) {
    DayResult r;
    r.day = state_.day;
    r.lifecycle = state_.lifecycle;
    r.paused_for_decision = state_.paused_for_decision;
    if (!state_.history.day_hashes.empty()) r.day_hash = state_.history.day_hashes.back();
    return r;
  }

  day_facts_.clear();
  day_news_.clear();
  const std::size_t facts_before = state_.facts.size();
  const std::size_t news_before = state_.news.size();

  // A day advances the calendar first; every phase below resolves day D+1.
  state_.day = checked_add(state_.day, 1);
  state_.decisions_opened_today = 0;
  scratch_ = std::make_unique<DayScratch>();
  for (const auto& p : state_.planets) {
    DayScratch::PlanetScratch ps;
    const int n = catalog_->resource_count();
    ps.opening_stock.assign(static_cast<std::size_t>(n), 0);
    ps.available_input.assign(static_cast<std::size_t>(n), 0);
    ps.staged_output.assign(static_cast<std::size_t>(n), 0);
    ps.produced.assign(static_cast<std::size_t>(n), 0);
    ps.consumed.assign(static_cast<std::size_t>(n), 0);
    scratch_->planets.emplace(p.planet_id, std::move(ps));
  }

  phase_due_work();
  phase_maintenance();
  phase_opening_snapshot();
  phase_power();
  phase_production();
  phase_commit_and_needs();
  phase_construction();
  phase_freight();
  phase_consequences();
  phase_commit();

  DayResult result;
  result.day = state_.day;
  result.day_hash = state_.history.day_hashes.empty() ? std::string() : state_.history.day_hashes.back();
  result.paused_for_decision = state_.paused_for_decision;
  result.lifecycle = state_.lifecycle;
  for (std::size_t i = facts_before; i < state_.facts.size(); ++i) result.new_facts.push_back(state_.facts[i].id);
  for (std::size_t i = news_before; i < state_.news.size(); ++i) result.new_news.push_back(state_.news[i].id);
  scratch_.reset();
  return result;
}

// ---------------------------------------------------------------------------
// Phase 1: due work. Expire timed effects, apply scheduled consequences,
// complete worker transfers, found due expeditions, mark ship arrivals. All
// sorted by due day, effect priority and stable id.
// ---------------------------------------------------------------------------

void Session::phase_due_work() {
  const Day today = state_.day;

  auto expire = [&](std::vector<ActiveModifier>& mods) {
    mods.erase(std::remove_if(mods.begin(), mods.end(),
                              [&](const ActiveModifier& m) { return m.expires_day >= 0 && m.expires_day < today; }),
               mods.end());
  };
  for (auto& f : state_.facilities) expire(f.modifiers);
  for (auto& p : state_.planets) expire(p.modifiers);
  expire(state_.political.modifiers);

  // Facilities completed yesterday become usable today, unstaffed.
  for (auto& f : state_.facilities) {
    if (f.lifecycle == FacilityLifecycle::Commissioning && today >= f.activates_day) {
      f.lifecycle = FacilityLifecycle::Active;
    }
  }

  // Service jobs and their absolute ready days.
  for (auto& f : state_.facilities) {
    if (!f.service.has_value()) continue;
    if (today < f.service->ready_day) continue;
    const Bp gain = catalog_->condition_rules().service_gain_bp;
    f.condition_bp = clamp_i64(checked_add(f.condition_bp, gain), catalog_->condition_rules().min_bp,
                               catalog_->condition_rules().max_bp);
    f.last_service_completed_day = today;
    f.service.reset();
    sim::emit_fact(state_, "service_completed", f.planet_id, f.id, {{"condition_bp", f.condition_bp}}, {},
                   reason::kCauseServiceCost, {}, "");
  }

  // Scheduled consequences, in (due day, priority, id) order.
  std::vector<ScheduledEffect> due;
  {
    std::vector<ScheduledEffect> keep;
    for (const auto& s : state_.scheduled) {
      if (s.due_day <= today) {
        due.push_back(s);
      } else {
        keep.push_back(s);
      }
    }
    state_.scheduled = keep;
  }
  for (const auto& s : due) {
    const EffectDef* effect = sim::resolve_scheduled_effect(*catalog_, s);
    if (effect == nullptr) {
      throw SimError("scheduled effect " + to_decimal_string_u(s.id) + " no longer resolves against the catalog");
    }
    sim::execute_effect(state_, *catalog_, *effect, s.event_id, s.choice_id, s.list_name, s.effect_index, s.planet_id,
                        s.facility_id, s.source_event_instance);
  }

  // Worker transfers complete on their ready day. Cancelling cannot make a
  // worker productive before that day (TDD 7.1).
  {
    std::vector<WorkerTransfer> keep;
    for (const auto& t : state_.transfers) {
      if (t.ready_day > today) {
        keep.push_back(t);
        continue;
      }
      if (t.to_facility != 0) {
        FacilityState* f = state_.find_facility(t.to_facility);
        if (f == nullptr || f->lifecycle == FacilityLifecycle::Cancelled) {
          // The destination no longer exists: the workers land in Reserve.
          sim::emit_fact(state_, "transfer_returned_to_reserve", t.planet_id, t.to_facility, {{"workers", t.count}}, {},
                         reason::kUnknownFacility, {}, "");
        } else if (f->construction.has_value() && f->lifecycle == FacilityLifecycle::UnderConstruction) {
          f->construction->assigned_workers += t.count;
        } else {
          f->assigned_workers += t.count;
        }
      }
      sim::emit_fact(state_, "transfer_completed", t.planet_id, t.to_facility, {{"workers", t.count}}, {}, "", {}, "");
    }
    state_.transfers = keep;
  }

  // Relief contracts arrive independently of player freight, as an explicit
  // external grant recorded in the ledger (TDD 14.4).
  for (auto& p : state_.planets) {
    if (p.relief_pending_day < 0 || p.relief_pending_day > today) continue;
    const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
    std::vector<NamedValue> args;
    for (const auto& [idx, qty] : sc.relief.grant) {
      Milli added = sim::add_stock(state_, p, idx, qty, account::kExternal, 0, reason::kCauseReliefGrant);
      args.push_back({catalog_->resource(idx).id, added});
    }
    p.relief_pending_day = -1;
    InstanceId fact = sim::emit_fact(state_, "relief_delivered", p.planet_id, 0, args, {}, reason::kCauseReliefGrant,
                                     {}, "");
    sim::emit_news(state_, *catalog_, "news.relief_delivered", p.planet_id, args, {}, {fact},
                   "relief_delivered:" + p.planet_id, 0);
  }

  // Expedition transit and arrival. Each transit day is charged from the
  // expedition's own cargo, and a due expedition consumes its final transit
  // day's needs before founding (TDD 5.2, 11.1).
  auto charge_transit_day = [&]() {
    const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
    for (const auto& [idx, per_day] : sc.expedition.transit_per_day) {
      auto it = state_.colonisation.cargo.find(idx);
      Milli have = it == state_.colonisation.cargo.end() ? 0 : it->second;
      Milli take = have < per_day ? have : per_day;
      if (take <= 0) continue;
      state_.colonisation.cargo[idx] = have - take;
      state_.colonisation.consumed_transit[idx] += take;
      sim::record(state_, idx, take, sim::expedition_account(state_.colonisation.expedition_id),
                  account::kSinkCivilian, state_.colonisation.expedition_id, reason::kCauseExpeditionTransit);
    }
  };
  if (state_.colonisation.launched && !state_.colonisation.founded && state_.colonisation.arrival_day > today) {
    charge_transit_day();
  }
  if (state_.colonisation.launched && !state_.colonisation.founded && state_.colonisation.arrival_day == today) {
    charge_transit_day();
    found_colony();
  }

  // Ship arrivals.
  ShipState& ship = state_.ship;
  if (ship.phase == ShipPhase::Transit && ship.arrival_day >= 0 && ship.arrival_day <= today) {
    if (ship.mission == ShipMission::StrategicOutbound) {
      credit_strategic_delivery();
    } else if (ship.mission == ShipMission::StrategicReturn) {
      ship.phase = ShipPhase::Docked;
      ship.location_planet = ship.destination_planet;
      ship.mission = ShipMission::None;
      ship.earliest_departure_day = checked_add(today, catalog_->scenario(state_.scenario_id).freight.min_dwell_days);
      state_.demand.freighter_committed = false;
      sim::emit_fact(state_, "ship_returned_from_front", ship.location_planet, ship.id, {}, {}, "", {}, "");
    } else {
      ship.phase = ShipPhase::ArrivedHolding;
      ship.location_planet = ship.destination_planet;
      ship.shipment_id = ship.shipment_id == 0 ? state_.allocate_id() : ship.shipment_id;
      sim::emit_fact(state_, "ship_arrived", ship.location_planet, ship.id,
                     {{"cargo_volume", sim::cargo_volume(*catalog_, ship.cargo)}}, {}, "", {}, "");
    }
  }
}

// ---------------------------------------------------------------------------
// Phase 2: maintenance. Pay today's facility machinery maintenance from
// unreserved opening stock in explicit priority order. Construction jobs do not
// pay facility maintenance.
// ---------------------------------------------------------------------------

void Session::phase_maintenance() {
  const auto& rules = catalog_->condition_rules();
  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    const int machinery = catalog_->machinery();
    for (InstanceId id : sim::production_order(state_, planet.planet_id, false)) {
      FacilityState& f = state_.facility(id);
      if (f.lifecycle != FacilityLifecycle::Active) continue;
      const Milli due = def_of(*catalog_, f).maintenance_machinery_per_day;
      ps.maintenance_due = checked_add(ps.maintenance_due, due);
      if (due == 0) {
        f.condition_bp = clamp_i64(checked_add(f.condition_bp, rules.maintained_gain_per_day), rules.min_bp,
                                   rules.max_bp);
        continue;
      }
      if (planet.inventory.available(machinery) >= due) {
        sim::remove_stock(state_, planet, machinery, due, account::kSinkMaintenance, f.id, reason::kCauseMaintenance);
        ps.maintenance_paid = checked_add(ps.maintenance_paid, due);
        f.condition_bp = clamp_i64(checked_add(f.condition_bp, rules.maintained_gain_per_day), rules.min_bp,
                                   rules.max_bp);
      } else {
        f.condition_bp = clamp_i64(checked_sub(f.condition_bp, rules.unmaintained_loss_per_day), rules.min_bp,
                                   rules.max_bp);
        sim::emit_fact(state_, "maintenance_unpaid", planet.planet_id, f.id,
                       {{"due", due}, {"available", planet.inventory.available(machinery)},
                        {"condition_bp", f.condition_bp}},
                       {catalog_->resource(machinery).id}, reason::kInsufficientStock, {}, "");
      }
    }
    planet.last_day.maintenance_machinery_due = ps.maintenance_due;
    planet.last_day.maintenance_machinery_paid = ps.maintenance_paid;
  }
}

// ---------------------------------------------------------------------------
// Phase 3: opening snapshot. Freeze eligible workers, condition, prior-day
// health and fatigue, initial available inventories, and free output capacity.
// ---------------------------------------------------------------------------

void Session::phase_opening_snapshot() {
  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    for (int r = 0; r < catalog_->resource_count(); ++r) {
      ps.opening_stock[static_cast<std::size_t>(r)] = planet.inventory.on_hand[static_cast<std::size_t>(r)];
      ps.available_input[static_cast<std::size_t>(r)] = planet.inventory.available(r);
    }
    ps.prior_health_bp = planet.health_bp;
    ps.prior_fatigue_bp = planet.fatigue_bp;
    ps.housing_capacity = sim::housing_capacity(state_, *catalog_, planet.planet_id);
    for (const auto& f : state_.facilities) {
      if (f.planet_id != planet.planet_id) continue;
      ps.frozen_workers[f.id] = f.assigned_workers;
      ps.frozen_condition[f.id] = f.condition_bp;
      if (sim::is_operable(f, state_.day)) ps.docks += def_of(*catalog_, f).passive.docks;
    }
  }
}

// ---------------------------------------------------------------------------
// Phase 10: commit. Validate invariants, publish the snapshot, append the
// canonical day hash. Only a complete committed boundary is saveable.
// ---------------------------------------------------------------------------

void Session::phase_commit() {
  sim::validate_invariants(state_, *catalog_);

  // Bounded daily metric samples, compacted into fixed weekly summaries.
  for (const auto& planet : state_.planets) {
    MetricSample s;
    s.day = state_.day;
    s.planet_id = planet.planet_id;
    s.health_bp = planet.health_bp;
    s.stability_bp = planet.stability_bp;
    s.fatigue_bp = planet.fatigue_bp;
    s.food_fulfilment_bp = planet.last_day.food_fulfilment_bp;
    s.water_fulfilment_bp = planet.last_day.water_fulfilment_bp;
    s.power_fulfilment_bp = planet.last_day.power_fulfilment_bp;
    s.closing_stock = planet.inventory.on_hand;
    extend_digest(state_.archive_digests.samples, digest_entry(s));
    state_.history.samples.push_back(s);
  }
  const int sample_cap = catalog_->news_rules().max_metric_samples * static_cast<int>(state_.planets.size());
  if (sample_cap > 0 && static_cast<int>(state_.history.samples.size()) > sample_cap) {
    const std::size_t drop = state_.history.samples.size() - static_cast<std::size_t>(sample_cap);
    for (std::size_t i = 0; i < drop; ++i) {
      const MetricSample& s = state_.history.samples[i];
      // Merge into the fixed weekly bucket this day belongs to, so a summary is
      // extended rather than appended once per compaction.
      const Day week = div_floor(s.day, 7);
      WeeklySummary* bucket = nullptr;
      for (auto& w : state_.history.weekly) {
        if (w.planet_id == s.planet_id && div_floor(w.first_day, 7) == week) {
          bucket = &w;
          break;
        }
      }
      if (bucket == nullptr) {
        WeeklySummary w;
        w.planet_id = s.planet_id;
        w.first_day = s.day;
        w.last_day = s.day;
        w.min_health_bp = s.health_bp;
        w.min_stability_bp = s.stability_bp;
        w.min_food_fulfilment_bp = s.food_fulfilment_bp;
        w.min_water_fulfilment_bp = s.water_fulfilment_bp;
        state_.history.weekly.push_back(w);
      } else {
        bucket->last_day = std::max(bucket->last_day, s.day);
        bucket->min_health_bp = std::min(bucket->min_health_bp, s.health_bp);
        bucket->min_stability_bp = std::min(bucket->min_stability_bp, s.stability_bp);
        bucket->min_food_fulfilment_bp = std::min(bucket->min_food_fulfilment_bp, s.food_fulfilment_bp);
        bucket->min_water_fulfilment_bp = std::min(bucket->min_water_fulfilment_bp, s.water_fulfilment_bp);
      }
    }
    state_.history.samples.erase(state_.history.samples.begin(),
                                 state_.history.samples.begin() + static_cast<long>(drop));
  }
  // The weekly archive is bounded too: a decade of weeks per world. A summary is
  // digested when it leaves, so a dropped week still shapes the canonical hash.
  const std::size_t weekly_cap = 520 * state_.planets.size();
  while (state_.history.weekly.size() > weekly_cap) {
    extend_digest(state_.archive_digests.weekly, digest_entry(state_.history.weekly.front()));
    state_.history.weekly.erase(state_.history.weekly.begin());
  }

  const std::string day_hash = canonical_hash();
  extend_digest(state_.archive_digests.day_hashes, day_hash);
  state_.history.day_hashes.push_back(day_hash);
  constexpr std::size_t kHashCap = 400;
  if (state_.history.day_hashes.size() > kHashCap) {
    const std::size_t drop = state_.history.day_hashes.size() - kHashCap;
    state_.history.day_hashes.erase(state_.history.day_hashes.begin(),
                                    state_.history.day_hashes.begin() + static_cast<long>(drop));
    state_.history.first_retained_hash_day = checked_add(state_.history.first_retained_hash_day,
                                                         static_cast<Day>(drop));
  }
}

namespace sim {

void validate_invariants(const SessionState& state, const Catalog& cat) {
  const int n = cat.resource_count();
  for (const auto& p : state.planets) {
    if (static_cast<int>(p.inventory.on_hand.size()) != n) throw SimError("invariant: inventory width");
    for (int r = 0; r < n; ++r) {
      Milli have = p.inventory.on_hand[static_cast<std::size_t>(r)];
      if (have < 0) throw SimError("invariant: negative stock of '" + cat.resource(r).id + "' on " + p.planet_id);
      if (have > p.inventory.capacity_per_resource) {
        throw SimError("invariant: stock of '" + cat.resource(r).id + "' exceeds capacity on " + p.planet_id);
      }
      if (p.inventory.reserved(r) > have) {
        throw SimError("invariant: reservations exceed on-hand for '" + cat.resource(r).id + "' on " + p.planet_id);
      }
      Milli committed = checked_add(have, p.inventory.claimed_space(r));
      if (committed > p.inventory.capacity_per_resource) {
        throw SimError("invariant: incoming claims overbook capacity for '" + cat.resource(r).id + "' on " +
                       p.planet_id);
      }
    }
    if (p.workers_total > p.population) throw SimError("invariant: workers exceed population on " + p.planet_id);
    if (p.health_bp < 0 || p.health_bp > kBpOne) throw SimError("invariant: health out of range on " + p.planet_id);
    if (p.fatigue_bp < 0 || p.fatigue_bp > kBpOne) throw SimError("invariant: fatigue out of range on " + p.planet_id);
    if (p.stability_bp < 0 || p.stability_bp > kBpOne) {
      throw SimError("invariant: stability out of range on " + p.planet_id);
    }
    // Workers: assignments + transitions + Reserve + ship crew equal the pool.
    const int assigned = workers_assigned(state, p.planet_id);
    const int transitioning = workers_transitioning(state, p.planet_id);
    const int reserve = workers_reserve(state, p);
    if (assigned + transitioning + reserve + p.ship_crew_reserved != p.workers_total) {
      throw SimError("invariant: worker accounting does not balance on " + p.planet_id);
    }
    const int slots = cat.planet(p.planet_id).slot_count;
    if (used_slots(state, p.planet_id) > slots) throw SimError("invariant: slot overcommit on " + p.planet_id);
  }
  if (state.political.adherence < 0 || state.political.adherence > 100) {
    throw SimError("invariant: faction adherence out of range");
  }
  for (const auto& f : state.facilities) {
    const FacilityDef& d = cat.facility(f.facility_id);
    if (d.recipe_index(f.recipe_id) < 0) throw SimError("invariant: facility recipe no longer exists");
    if (f.condition_bp < cat.condition_rules().min_bp || f.condition_bp > cat.condition_rules().max_bp) {
      throw SimError("invariant: facility condition out of range");
    }
    const RecipeDef& r = cat.facility(f.facility_id).recipes[static_cast<std::size_t>(d.recipe_index(f.recipe_id))];
    if (f.assigned_workers < 0 || f.assigned_workers > r.staff) {
      throw SimError("invariant: facility " + to_decimal_string_u(f.id) + " staffing out of range");
    }
    if (f.construction.has_value()) {
      const auto& job = *f.construction;
      if (job.work_done > job.work_total) throw SimError("invariant: construction work exceeds the total");
      for (const auto& [idx, qty] : job.escrow) {
        auto it = job.original_cost.find(idx);
        Milli original = it == job.original_cost.end() ? 0 : it->second;
        if (qty > original) throw SimError("invariant: construction escrow exceeds the original cost");
      }
    }
    for (const auto& m : f.modifiers) {
      if (!m.source_event_id.empty() && cat.find_event(m.source_event_id) == nullptr) {
        throw SimError("invariant: active modifier references a missing event definition");
      }
    }
  }
  // A completed shipment cannot simultaneously be in transit.
  if (state.ship.phase == ShipPhase::Transit && !state.ship.location_planet.empty()) {
    throw SimError("invariant: a ship in transit cannot also be at a planet");
  }
  if (state.ship.tank < 0) throw SimError("invariant: negative fuel tank");
  for (const auto& s : state.scheduled) {
    if (s.due_day < state.day) throw SimError("invariant: a scheduled effect is overdue after commit");
    if (resolve_scheduled_effect(cat, s) == nullptr) {
      throw SimError("invariant: a scheduled effect no longer resolves against the catalog");
    }
  }
  for (const auto& e : state.events) {
    if (e.resolution == EventResolution::Open && e.deadline_day >= 0 && e.deadline_day < state.day) {
      throw SimError("invariant: an open decision window is past its deadline after commit");
    }
  }
}

}  // namespace sim
}  // namespace expansion
