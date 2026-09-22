// Phases 4-7 of TDD 5.2: power, production, commit and needs, construction.
#include <algorithm>
#include <cstddef>
#include <limits>

#include "sim_internal.hpp"

namespace expansion {

using sim::def_of;
using sim::recipe_of;

namespace {

// Largest run factor in basis points, up to `desired`, that satisfies every
// limiting constraint. Limiting constraints use a minimum; they are never
// multiplied together (TDD 8.2).
struct RunLimits {
  const RecipeDef* recipe = nullptr;
  const std::vector<Milli>* available_input = nullptr;
  PowerMilli power_pool = 0;
  std::vector<Milli> output_headroom;   // per resource index
  Bp output_efficiency_bp = kBpOne;
  EfficiencyClass efficiency_class = EfficiencyClass::None;
};

Milli scaled_output(Milli base, Bp run_bp, Bp efficiency_bp, bool apply_efficiency) {
  Milli out = mul_div_floor(base, run_bp, kBpOne);
  if (apply_efficiency) out = mul_div_floor(out, efficiency_bp, kBpOne);
  return out;
}

bool feasible(const RunLimits& lim, Bp run_bp) {
  if (run_bp == 0) return true;
  for (const auto& [idx, qty] : lim.recipe->inputs) {
    Milli need = mul_div_ceil(qty, run_bp, kBpOne);
    if (need > (*lim.available_input)[static_cast<std::size_t>(idx)]) return false;
  }
  if (lim.recipe->power_per_day > 0) {
    if (mul_div_ceil(lim.recipe->power_per_day, run_bp, kBpOne) > lim.power_pool) return false;
  }
  for (const auto& [idx, qty] : lim.recipe->outputs) {
    const bool apply = lim.efficiency_class != EfficiencyClass::None;
    Milli produced = scaled_output(qty, run_bp, lim.output_efficiency_bp, apply);
    if (produced > lim.output_headroom[static_cast<std::size_t>(idx)]) return false;
  }
  return true;
}

Bp largest_feasible_run(const RunLimits& lim, Bp desired) {
  if (feasible(lim, desired)) return desired;
  Bp lo = 0;
  Bp hi = desired;
  while (lo < hi) {
    Bp mid = lo + (hi - lo + 1) / 2;
    if (feasible(lim, mid)) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

void classify_limits(const RunLimits& lim, Bp desired, Bp actual, FacilityExplanation* ex) {
  if (actual >= desired) return;
  // Report every constraint that a one-basis-point larger run would violate.
  const Bp probe = actual + 1 > desired ? desired : actual + 1;
  for (const auto& [idx, qty] : lim.recipe->inputs) {
    if (mul_div_ceil(qty, probe, kBpOne) > (*lim.available_input)[static_cast<std::size_t>(idx)]) {
      ex->missing_inputs.push_back(idx);
    }
  }
  if (lim.recipe->power_per_day > 0 &&
      mul_div_ceil(lim.recipe->power_per_day, probe, kBpOne) > lim.power_pool) {
    ex->power_limited = true;
  }
  for (const auto& [idx, qty] : lim.recipe->outputs) {
    const bool apply = lim.efficiency_class != EfficiencyClass::None;
    if (scaled_output(qty, probe, lim.output_efficiency_bp, apply) > lim.output_headroom[static_cast<std::size_t>(idx)]) {
      ex->blocked_outputs.push_back(idx);
    }
  }
}

// A low output caused by labour must not be reported as missing material
// (TDD 8.4).
void choose_primary_reason(FacilityExplanation* ex) {
  if (!ex->missing_inputs.empty()) {
    ex->primary_reason = reason::kMissingInput;
    return;
  }
  if (ex->power_limited) {
    ex->primary_reason = reason::kPowerShortfall;
    return;
  }
  if (!ex->blocked_outputs.empty()) {
    ex->primary_reason = reason::kOutputStoreFull;
    return;
  }
  struct Candidate {
    Bp value;
    const char* reason;
  };
  const Candidate candidates[] = {
      {ex->staffing_bp, ex->staffing_bp == 0 ? reason::kNoStaff : reason::kUnderstaffed},
      {ex->effects_bp, reason::kActiveModifier},
      {ex->condition_bp, reason::kPoorCondition},
      {ex->health_bp, reason::kPoorHealth},
      {ex->fatigue_bp, reason::kFatigued},
  };
  Bp worst = kBpOne;
  const char* worst_reason = reason::kFullThroughput;
  for (const auto& c : candidates) {
    if (c.value < worst) {
      worst = c.value;
      worst_reason = c.reason;
    }
  }
  ex->labour_limited = worst_reason == reason::kUnderstaffed || worst_reason == reason::kNoStaff;
  ex->primary_reason = worst_reason;
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 4: power. Generate local capacity, consume thermal fuel, protect
// residential demand first, then reserve power by priority.
// ---------------------------------------------------------------------------

void Session::phase_power() {
  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    PowerMilli generated = 0;

    for (InstanceId id : sim::production_order(state_, planet.planet_id, true)) {
      FacilityState& f = state_.facility(id);
      if (!sim::is_operable(f, state_.day)) continue;
      const FacilityDef& fd = def_of(*catalog_, f);
      // Passive supply (e.g. Colony Hub solar) is independent of staffing and
      // condition, and needs no fuel input.
      generated = checked_add(generated, fd.passive.power_output);

      const RecipeDef& r = recipe_of(*catalog_, f);
      if (!r.is_generator()) continue;

      FacilityExplanation ex;
      ex.day = state_.day;
      if (f.idle) {
        ex.idle = true;
        ex.primary_reason = reason::kIdleByChoice;
        f.last_explanation = ex;
        continue;
      }
      if (f.service.has_value()) {
        ex.servicing = true;
        ex.primary_reason = reason::kServicingDowntime;
        f.last_explanation = ex;
        continue;
      }
      const int assigned = ps.frozen_workers[f.id];
      const Bp condition = ps.frozen_condition[f.id];
      const Bp desired = sim::desired_throughput_bp(*catalog_, state_, planet, f, assigned, condition,
                                                    ps.prior_health_bp, ps.prior_fatigue_bp, &ex);
      RunLimits lim;
      lim.recipe = &r;
      lim.available_input = &ps.available_input;
      lim.power_pool = 0;
      lim.output_headroom.assign(static_cast<std::size_t>(catalog_->resource_count()),
                                 std::numeric_limits<Milli>::max());
      lim.efficiency_class = EfficiencyClass::None;
      const Bp run = largest_feasible_run(lim, desired);

      // Classify the limits against the pre-consumption snapshot, before the
      // inputs are drawn down.
      ex.actual_throughput_bp = run;
      classify_limits(lim, desired, run, &ex);

      // A generator cannot burn Coal produced on that same day: inputs come
      // from opening available stock only.
      for (const auto& [idx, qty] : r.inputs) {
        Milli need = mul_div_ceil(qty, run, kBpOne);
        if (need == 0) continue;
        sim::remove_stock(state_, planet, idx, need, account::kSinkRecipe, f.id, reason::kCauseGeneration);
        ps.available_input[static_cast<std::size_t>(idx)] -= need;
        ps.consumed[static_cast<std::size_t>(idx)] += need;
        ex.inputs_consumed[idx] = need;
      }
      const PowerMilli supplied = mul_div_floor(r.power_output_per_day, run, kBpOne);
      generated = checked_add(generated, supplied);

      ex.power_granted = supplied;   // a generator's grant is what it supplies
      choose_primary_reason(&ex);
      f.last_explanation = ex;
    }

    const PowerMilli residential = sim::residential_power_demand(*catalog_, planet);
    const PowerMilli served = generated < residential ? generated : residential;
    ps.power.generated = generated;
    ps.power.residential_demand = residential;
    ps.power.residential_served = served;
    ps.power_pool = checked_sub(generated, served);
  }
}

// ---------------------------------------------------------------------------
// Phase 5: production. Allocate commodity inputs and power in deterministic
// recipe order. Consume inputs from opening stock; stage all outputs. Never
// feed staged output into another recipe today.
// ---------------------------------------------------------------------------

void Session::phase_production() {
  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    const PlanetDef& pd = catalog_->planet(planet.planet_id);
    const FactionDef& faction = catalog_->faction(state_.political.faction_id);

    for (InstanceId id : sim::production_order(state_, planet.planet_id, false)) {
      FacilityState& f = state_.facility(id);
      if (!sim::is_operable(f, state_.day)) continue;
      const RecipeDef& r = recipe_of(*catalog_, f);
      if (r.is_generator()) continue;   // resolved in the power phase

      FacilityExplanation ex;
      ex.day = state_.day;
      if (f.idle) {
        ex.idle = true;
        ex.primary_reason = reason::kIdleByChoice;
        f.last_explanation = ex;
        continue;
      }
      // A service job stops that facility's active production for its window,
      // while passive housing and solar remain available (TDD 8.2).
      if (f.service.has_value()) {
        ex.servicing = true;
        ex.primary_reason = reason::kServicingDowntime;
        f.last_explanation = ex;
        continue;
      }

      const int assigned = ps.frozen_workers[f.id];
      const Bp condition = ps.frozen_condition[f.id];
      const Bp desired = sim::desired_throughput_bp(*catalog_, state_, planet, f, assigned, condition,
                                                    ps.prior_health_bp, ps.prior_fatigue_bp, &ex);

      RunLimits lim;
      lim.recipe = &r;
      lim.available_input = &ps.available_input;
      lim.power_pool = ps.power_pool;
      lim.efficiency_class = r.efficiency_class;
      lim.output_efficiency_bp = pd.efficiency_for(r.efficiency_class);
      lim.output_headroom.assign(static_cast<std::size_t>(catalog_->resource_count()), 0);
      for (int res = 0; res < catalog_->resource_count(); ++res) {
        Milli used = checked_add(planet.inventory.on_hand[static_cast<std::size_t>(res)],
                                 planet.inventory.claimed_space(res));
        used = checked_add(used, ps.staged_output[static_cast<std::size_t>(res)]);
        Milli room = planet.inventory.capacity_per_resource - used;
        lim.output_headroom[static_cast<std::size_t>(res)] = room < 0 ? 0 : room;
      }

      Bp run = largest_feasible_run(lim, desired);
      // A job that would produce no minimum output quantum does not consume
      // inputs (TDD 4.1).
      if (!r.outputs.empty()) {
        bool any_output = false;
        for (const auto& [idx, qty] : r.outputs) {
          if (scaled_output(qty, run, lim.output_efficiency_bp, r.efficiency_class != EfficiencyClass::None) > 0) {
            any_output = true;
          }
        }
        if (!any_output) run = 0;
      }

      ex.actual_throughput_bp = run;
      classify_limits(lim, desired, run, &ex);

      const PowerMilli power_request = mul_div_ceil(r.power_per_day, desired, kBpOne);
      const PowerMilli power_grant = mul_div_ceil(r.power_per_day, run, kBpOne);
      ex.power_requested = power_request;
      ex.power_granted = power_grant;
      ps.power_pool = checked_sub(ps.power_pool, power_grant);
      ps.power.facility_granted = checked_add(ps.power.facility_granted, power_grant);

      for (const auto& [idx, qty] : r.inputs) {
        Milli need = mul_div_ceil(qty, run, kBpOne);
        if (need == 0) continue;
        sim::remove_stock(state_, planet, idx, need, account::kSinkRecipe, f.id, reason::kCauseRecipeInput);
        ps.available_input[static_cast<std::size_t>(idx)] -= need;
        ps.consumed[static_cast<std::size_t>(idx)] += need;
        ex.inputs_consumed[idx] = need;
      }
      for (const auto& [idx, qty] : r.outputs) {
        Milli made = scaled_output(qty, run, lim.output_efficiency_bp, r.efficiency_class != EfficiencyClass::None);
        if (made == 0) continue;
        ps.staged_output[static_cast<std::size_t>(idx)] += made;
        ex.outputs_produced[idx] = made;
      }

      // Service capacities scale with the same run factor.
      if (r.clinic_capacity > 0) {
        People capacity = mul_div_floor(r.clinic_capacity, run, kBpOne);
        Bp clinic_bp = faction.clinic_capacity_bp;
        clinic_bp = bp_mul(clinic_bp, sim::modifier_product_bp(planet.modifiers, ModifierTarget::PlanetClinicCapacity,
                                                               state_.day, nullptr));
        capacity = mul_div_floor(capacity, clinic_bp, kBpOne);
        ps.clinic_capacity = checked_add(ps.clinic_capacity, capacity);
      }
      if (r.cargo_handling_per_day > 0) {
        Milli handling = mul_div_floor(r.cargo_handling_per_day, run, kBpOne);
        ps.port_handling_capacity = checked_add(ps.port_handling_capacity, handling);
      }

      choose_primary_reason(&ex);
      f.last_explanation = ex;
    }
    ps.port_handling_remaining = ps.port_handling_capacity;
  }
}

// ---------------------------------------------------------------------------
// Phase 6: commit and needs. Commit outputs, consume civilian food and water,
// measure actual power and clinic coverage, update health, fatigue and stability.
// ---------------------------------------------------------------------------

void Session::phase_commit_and_needs() {
  const auto& w = catalog_->wellbeing();
  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    DailyReport& rep = planet.last_day;
    rep = DailyReport{};
    rep.day = state_.day;
    rep.opening_stock = ps.opening_stock;
    rep.maintenance_machinery_due = ps.maintenance_due;
    rep.maintenance_machinery_paid = ps.maintenance_paid;

    for (int r = 0; r < catalog_->resource_count(); ++r) {
      Milli staged = ps.staged_output[static_cast<std::size_t>(r)];
      if (staged == 0) continue;
      Milli added = sim::add_stock(state_, planet, r, staged, account::kSourceRecipe, 0, reason::kCauseProduction);
      ps.produced[static_cast<std::size_t>(r)] += added;
      if (added < staged) {
        sim::emit_fact(state_, "production_blocked_by_storage", planet.planet_id, 0,
                       {{"staged", staged}, {"stored", added}}, {catalog_->resource(r).id}, reason::kOutputStoreFull,
                       {}, "");
      }
    }

    // Civilian needs.
    const Milli food_demand = sim::civilian_food_demand(state_, *catalog_, planet);
    const Milli water_demand = sim::civilian_water_demand(*catalog_, planet);
    const Milli food_have = planet.inventory.available(catalog_->food());
    const Milli water_have = planet.inventory.available(catalog_->water());
    const Milli food_served = food_have < food_demand ? food_have : food_demand;
    const Milli water_served = water_have < water_demand ? water_have : water_demand;
    if (food_served > 0) {
      sim::remove_stock(state_, planet, catalog_->food(), food_served, account::kSinkCivilian, 0,
                        reason::kCauseCivilian);
      ps.consumed[static_cast<std::size_t>(catalog_->food())] += food_served;
    }
    if (water_served > 0) {
      sim::remove_stock(state_, planet, catalog_->water(), water_served, account::kSinkCivilian, 0,
                        reason::kCauseCivilian);
      ps.consumed[static_cast<std::size_t>(catalog_->water())] += water_served;
    }

    const Bp food_bp = fulfilment_bp(food_served, food_demand);
    const Bp water_bp = fulfilment_bp(water_served, water_demand);
    const Bp power_bp = fulfilment_bp(ps.power.residential_served, ps.power.residential_demand);
    const Bp housing_bp = fulfilment_bp(ps.housing_capacity, planet.population);
    const Bp clinic_bp = fulfilment_bp(ps.clinic_capacity, planet.population);

    // Health target: min(F, W, P, Hs, clinic floor + span * coverage).
    const Bp clinic_term = checked_add(w.clinic_floor_bp, mul_div_floor(clinic_bp, w.clinic_span_bp, kBpOne));
    Bp target = food_bp;
    target = std::min(target, water_bp);
    target = std::min(target, power_bp);
    target = std::min(target, housing_bp);
    target = std::min(target, clinic_term);
    target = clamp_bp(target);
    if (target < planet.health_bp) {
      Bp step = std::min<Bp>(w.health_down_max, planet.health_bp - target);
      planet.health_bp -= step;
    } else if (target > planet.health_bp) {
      Bp step = std::min<Bp>(w.health_up_max, target - planet.health_bp);
      planet.health_bp += step;
    }

    // Fatigue follows the active policy's daily movement.
    const PolicyDef* policy = catalog_->find_policy(planet.policy_id);
    const Bp fatigue_delta = policy == nullptr ? -100 : policy->fatigue_delta_per_day;
    planet.fatigue_bp = clamp_bp(checked_add(planet.fatigue_bp, fatigue_delta));

    // Stability target: floor once after summing the integer numerators.
    std::int64_t numerator = checked_mul(w.stability_base_bp, kBpOne);
    numerator = checked_add(numerator, checked_mul(w.stability_food_weight_bp, food_bp));
    numerator = checked_add(numerator, checked_mul(w.stability_water_weight_bp, water_bp));
    numerator = checked_add(numerator, checked_mul(w.stability_housing_weight_bp, housing_bp));
    numerator = checked_add(numerator, checked_mul(w.stability_health_weight_bp, planet.health_bp));
    Bp stability_target = div_floor(numerator, kBpOne);
    std::int64_t adjust = 0;
    if (policy != nullptr) adjust = checked_add(adjust, policy->stability_target_adjust_bp);
    adjust = checked_add(adjust, sim::modifier_sum(planet.modifiers, ModifierTarget::PlanetStabilityTarget, state_.day));
    adjust = checked_add(adjust,
                         sim::modifier_sum(state_.political.modifiers, ModifierTarget::PlanetStabilityTarget, state_.day));
    stability_target = clamp_bp(checked_add(stability_target, adjust));
    if (stability_target < planet.stability_bp) {
      Bp step = std::min<Bp>(w.stability_move_max, planet.stability_bp - stability_target);
      planet.stability_bp -= step;
    } else if (stability_target > planet.stability_bp) {
      Bp step = std::min<Bp>(w.stability_move_max, stability_target - planet.stability_bp);
      planet.stability_bp += step;
    }

    if (policy != nullptr && !policy->is_default) planet.policy_effective_days += 1;

    rep.food_demand = food_demand;
    rep.food_served = food_served;
    rep.water_demand = water_demand;
    rep.water_served = water_served;
    rep.food_fulfilment_bp = food_bp;
    rep.water_fulfilment_bp = water_bp;
    rep.power_fulfilment_bp = power_bp;
    rep.housing_fulfilment_bp = housing_bp;
    rep.clinic_coverage_bp = clinic_bp;
    rep.housing_capacity = ps.housing_capacity;
    rep.clinic_capacity = ps.clinic_capacity;
    rep.health_target_bp = target;
    rep.stability_target_bp = stability_target;
    rep.power = ps.power;
  }
}

// ---------------------------------------------------------------------------
// Phase 7: construction. Advance funded construction with assigned effective
// workers and reserved power. Completed facilities become usable the next day.
// ---------------------------------------------------------------------------

void Session::phase_construction() {
  const auto& rules = catalog_->construction_rules();
  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    std::vector<InstanceId> jobs;
    for (const auto& f : state_.facilities) {
      if (f.planet_id != planet.planet_id) continue;
      if (f.lifecycle != FacilityLifecycle::UnderConstruction) continue;
      jobs.push_back(f.id);
    }
    std::sort(jobs.begin(), jobs.end());

    for (InstanceId id : jobs) {
      FacilityState& f = state_.facility(id);
      ConstructionJob& job = *f.construction;
      job.days_elapsed += 1;
      job.blockage.clear();

      if (job.assigned_workers <= 0) {
        job.blockage = reason::kNotEnoughWorkers;
        continue;
      }
      const Bp health_bp = sim::health_factor_bp(*catalog_, ps.prior_health_bp);
      const Bp fatigue_bp = sim::fatigue_factor_bp(*catalog_, ps.prior_fatigue_bp);
      WorkMilli effective = checked_mul(job.assigned_workers, kMilliOne);
      effective = mul_div_floor(effective, health_bp, kBpOne);
      effective = mul_div_floor(effective, fatigue_bp, kBpOne);

      const PowerMilli request = checked_mul(job.assigned_workers, rules.power_per_worker);
      const PowerMilli grant = ps.power_pool < request ? ps.power_pool : request;
      ps.power_pool = checked_sub(ps.power_pool, grant);
      ps.power.construction_granted = checked_add(ps.power.construction_granted, grant);
      const Bp power_ratio = request > 0 ? fulfilment_bp(grant, request) : kBpOne;
      if (power_ratio < kBpOne) job.blockage = reason::kPowerShortfall;

      WorkMilli work_today = mul_div_floor(effective, power_ratio, kBpOne);
      WorkMilli remaining = checked_sub(job.work_total, job.work_done);
      if (work_today > remaining) work_today = remaining;
      job.work_done = checked_add(job.work_done, work_today);

      // Consume the cumulative fraction of escrow matching cumulative work,
      // rounding the cumulative target down (TDD 9.2).
      for (const auto& [idx, original] : job.original_cost) {
        Milli target = job.work_total > 0 ? mul_div_floor(original, job.work_done, job.work_total) : original;
        Milli already = job.consumed.count(idx) != 0 ? job.consumed.at(idx) : 0;
        Milli delta = checked_sub(target, already);
        if (delta <= 0) continue;
        Milli held = job.escrow.count(idx) != 0 ? job.escrow.at(idx) : 0;
        if (delta > held) delta = held;
        job.escrow[idx] = checked_sub(held, delta);
        job.consumed[idx] = checked_add(already, delta);
        sim::record(state_, idx, delta, sim::escrow_account(f.id), account::kSinkConstruction, f.id,
                    reason::kCauseConstructionConsume);
      }

      const FacilityDef& fd = def_of(*catalog_, f);
      const Day min_days = std::max<Day>(fd.min_build_days, rules.min_days);
      if (job.work_done >= job.work_total && job.days_elapsed >= min_days) {
        // Consume the final remainder at completion.
        for (const auto& [idx, held] : job.escrow) {
          if (held <= 0) continue;
          job.consumed[idx] = checked_add(job.consumed.count(idx) != 0 ? job.consumed.at(idx) : 0, held);
          sim::record(state_, idx, held, sim::escrow_account(f.id), account::kSinkConstruction, f.id,
                      reason::kCauseConstructionConsume);
        }
        job.escrow.clear();
        const int crew = job.assigned_workers;
        job.assigned_workers = 0;
        f.construction.reset();
        f.lifecycle = FacilityLifecycle::Commissioning;
        f.activates_day = checked_add(state_.day, 1);
        f.assigned_workers = 0;   // completed facilities start unstaffed
        if (crew > 0) {
          WorkerTransfer t;
          t.id = state_.allocate_id();
          t.planet_id = planet.planet_id;
          t.from_facility = f.id;
          t.to_facility = 0;
          t.count = crew;
          t.ready_day = checked_add(state_.day, 1);
          state_.transfers.push_back(t);
          std::sort(state_.transfers.begin(), state_.transfers.end(),
                    [](const WorkerTransfer& a, const WorkerTransfer& b) { return a.id < b.id; });
        }
        InstanceId fact = sim::emit_fact(state_, "construction_completed", planet.planet_id, f.id,
                                         {{"workers_released", crew}}, {f.facility_id}, "", {}, "");
        sim::emit_news(state_, *catalog_, "news.construction_completed", planet.planet_id, {}, {f.facility_id}, {fact},
                       "construction_completed:" + to_decimal_string_u(f.id), 2);
      }
    }

    ps.power.used = checked_add(ps.power.residential_served,
                               checked_add(ps.power.facility_granted, ps.power.construction_granted));
    ps.power.unused = ps.power_pool;
    planet.last_day.power = ps.power;
  }
}

}  // namespace expansion
