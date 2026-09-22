// Phase 9 of TDD 5.2: shortage counters, objectives, mandate states, conditional
// events, fact summaries and news. Effects opened here begin no earlier than the
// next day.
#include <algorithm>
#include <cstddef>
#include <limits>

#include "expansion/metrics.hpp"
#include "sim_internal.hpp"

namespace expansion {

using sim::def_of;
using sim::recipe_of;

void Session::phase_consequences() {
  update_shortages();
  for (auto& planet : state_.planets) update_survival(planet);
  update_mandate();
  update_political();
  update_events();
  update_milestones();

  // Record the day's worker accounting and closing stocks for explanation.
  for (auto& planet : state_.planets) {
    auto& ps = scratch_->at(planet.planet_id);
    planet.last_day.closing_stock = planet.inventory.on_hand;
    planet.last_day.produced = ps.produced;
    planet.last_day.consumed = ps.consumed;
    planet.last_day.workers_assigned = sim::workers_assigned(state_, planet.planet_id);
    planet.last_day.workers_transitioning = sim::workers_transitioning(state_, planet.planet_id);
    planet.last_day.workers_reserve = sim::workers_reserve(state_, planet);
    planet.last_day.workers_crew = planet.ship_crew_reserved;
  }

  evaluate_completion();
}

// ---------------------------------------------------------------------------
// Shortages. A shortage is a real missed need or missed planned operation, not
// merely a low stock number. Open after two missed days, close after three
// fulfilled days, and report on transition (TDD 13.3).
// ---------------------------------------------------------------------------

void Session::update_shortages() {
  const auto& rules = catalog_->news_rules();
  for (auto& planet : state_.planets) {
    std::set<int> missed;
    if (planet.last_day.food_fulfilment_bp < kBpOne) missed.insert(catalog_->food());
    if (planet.last_day.water_fulfilment_bp < kBpOne) missed.insert(catalog_->water());
    for (const auto& f : state_.facilities) {
      if (f.planet_id != planet.planet_id) continue;
      if (f.last_explanation.day != state_.day) continue;
      for (int r : f.last_explanation.missing_inputs) missed.insert(r);
    }
    if (planet.last_day.power_fulfilment_bp < kBpOne) {
      // Residential power is not a stored commodity; report it through its own fact.
      sim::emit_fact(state_, "residential_power_shortfall", planet.planet_id, 0,
                     {{"served", planet.last_day.power.residential_served},
                      {"demand", planet.last_day.power.residential_demand}},
                     {}, reason::kPowerShortfall, {}, "");
    }

    for (int r = 0; r < catalog_->resource_count(); ++r) {
      ShortageTracker& t = planet.shortages[r];
      if (missed.count(r) != 0) {
        t.missed_days += 1;
        t.fulfilled_days = 0;
        if (!t.open && t.missed_days >= rules.shortage_open_days) {
          t.open = true;
          InstanceId fact = sim::emit_fact(state_, "shortage_opened", planet.planet_id, 0,
                                           {{"missed_days", t.missed_days},
                                            {"stock", planet.inventory.on_hand[static_cast<std::size_t>(r)]}},
                                           {catalog_->resource(r).id}, reason::kMissingInput, {}, "");
          sim::emit_news(state_, *catalog_, "news.shortage_opened", planet.planet_id,
                         {{"missed_days", t.missed_days}}, {catalog_->resource(r).id}, {fact},
                         "shortage_open:" + planet.planet_id + ":" + catalog_->resource(r).id + ":" +
                             to_decimal_string(state_.day - t.missed_days + 1),
                         1);
        }
      } else {
        t.fulfilled_days += 1;
        if (t.open && t.fulfilled_days >= rules.shortage_close_days) {
          t.open = false;
          t.missed_days = 0;
          InstanceId fact = sim::emit_fact(state_, "shortage_closed", planet.planet_id, 0,
                                           {{"fulfilled_days", t.fulfilled_days}}, {catalog_->resource(r).id},
                                           reason::kOk, {}, "");
          sim::emit_news(state_, *catalog_, "news.recovery", planet.planet_id, {}, {catalog_->resource(r).id}, {fact},
                         "shortage_closed:" + planet.planet_id + ":" + catalog_->resource(r).id + ":" +
                             to_decimal_string(state_.day),
                         1);
        }
        if (!t.open) t.missed_days = 0;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Survival emergency and the explicit loss path (TDD 7.2, 14.4). P1 does not
// simulate citizen deaths and never silently removes population.
// ---------------------------------------------------------------------------

void Session::update_survival(PlanetState& planet) {
  if (!planet.colonised) return;
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  const Bp threshold = sc.survival.emergency_fulfilment_bp;

  if (planet.last_day.food_fulfilment_bp < threshold) {
    planet.food_emergency_streak += 1;
  } else {
    planet.food_emergency_streak = 0;   // each counter resets independently
  }
  if (planet.last_day.water_fulfilment_bp < threshold) {
    planet.water_emergency_streak += 1;
  } else {
    planet.water_emergency_streak = 0;
  }
  const int worst = std::max(planet.food_emergency_streak, planet.water_emergency_streak);

  if (!planet.survival_emergency_active && worst >= sc.survival.emergency_days) {
    planet.survival_emergency_active = true;
    planet.survival_emergency_opened_day = state_.day;
    InstanceId fact = sim::emit_fact(state_, "survival_emergency_declared", planet.planet_id, 0,
                                     {{"food_days", planet.food_emergency_streak},
                                      {"water_days", planet.water_emergency_streak}},
                                     {}, reason::kMissingInput, {}, "");
    sim::emit_news(state_, *catalog_, "news.survival_emergency", planet.planet_id, {}, {}, {fact},
                   "survival_emergency:" + planet.planet_id + ":" + to_decimal_string(state_.day), 0);
  }
  if (planet.survival_emergency_active && worst == 0) {
    planet.survival_emergency_active = false;
    planet.survival_emergency_days = 0;
    InstanceId fact = sim::emit_fact(state_, "survival_emergency_lifted", planet.planet_id, 0, {}, {}, reason::kOk, {},
                                     "");
    sim::emit_news(state_, *catalog_, "news.survival_emergency_lifted", planet.planet_id, {}, {}, {fact},
                   "survival_lifted:" + planet.planet_id + ":" + to_decimal_string(state_.day), 1);
  }
  planet.survival_emergency_days = worst;

  // Failure is evaluated after any pending relief has been delivered and after
  // that day's needs (TDD 14.4).
  if (worst >= sc.survival.failure_days && planet.relief_pending_day < 0) {
    state_.lifecycle = SessionLifecycle::Failed;
    InstanceId fact = sim::emit_fact(state_, "scenario_failed", planet.planet_id, 0, {{"days", worst}}, {},
                                     reason::kMissingInput, {}, "");
    sim::emit_news(state_, *catalog_, "news.scenario_failed", planet.planet_id, {{"days", worst}}, {}, {fact},
                   "scenario_failed", 0);
  }
}

// ---------------------------------------------------------------------------
// Strategic demand (TDD 14.1-14.3)
// ---------------------------------------------------------------------------

void Session::update_mandate() {
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);
  DemandState& d = state_.demand;

  if (!d.issued && state_.day >= sc.mandate.issue_day) {
    d.issued = true;
    d.issue_day = state_.day;
    d.deadline_day = sc.mandate.deadline_day;
    d.required = sc.mandate.requirement;
    std::vector<NamedValue> args;
    for (const auto& [idx, qty] : d.required) args.push_back({catalog_->resource(idx).id, qty});
    InstanceId fact = sim::emit_fact(state_, "mandate_issued", "", 0, args, {sc.mandate.id}, "", {}, "");
    sim::emit_news(state_, *catalog_, "news.mandate_issued", "", args, {sc.mandate.id}, {fact}, "mandate_issued", 0);
  }
  if (!d.issued || d.resolved) return;

  bool complete = true;
  for (const auto& [idx, need] : d.required) {
    Milli have = d.delivered.count(idx) != 0 ? d.delivered.at(idx) : 0;
    if (have < need) complete = false;
  }
  if (complete) {
    d.resolved = true;
    d.decision = d.negotiated ? MandateDecision::Negotiated : MandateDecision::Honoured;
    state_.political.adherence =
        static_cast<int>(clamp_i64(checked_add(state_.political.adherence, sc.mandate.honour_adherence), 0, 100));
    InstanceId fact = sim::emit_fact(state_, "mandate_fulfilled", "", 0,
                                     {{"adherence", state_.political.adherence}}, {sc.mandate.id}, "", {}, "");
    sim::emit_news(state_, *catalog_, "news.mandate_fulfilled", "", {}, {sc.mandate.id}, {fact}, "mandate_resolved", 0);
    return;
  }
  if (state_.day >= d.deadline_day) {
    d.resolved = true;
    d.decision = MandateDecision::Failed;
    state_.political.adherence =
        static_cast<int>(clamp_i64(checked_add(state_.political.adherence, sc.mandate.fail_adherence), 0, 100));
    InstanceId fact = sim::emit_fact(state_, "mandate_failed", "", 0, {{"adherence", state_.political.adherence}},
                                     {sc.mandate.id}, reason::kDepartureTooLate, {}, "");
    sim::emit_news(state_, *catalog_, "news.mandate_failed", "", {}, {sc.mandate.id}, {fact}, "mandate_resolved", 0);
  }
}

// ---------------------------------------------------------------------------
// Faction pressure (TDD 12.4). Adherence below 30 for seven consecutive days
// opens one Faction Review; stability is not additionally drained every day.
// ---------------------------------------------------------------------------

void Session::update_political() {
  if (state_.political.adherence < 30) {
    state_.political.low_adherence_streak += 1;
  } else {
    state_.political.low_adherence_streak = 0;
  }
}

// ---------------------------------------------------------------------------
// Conditional events (TDD 13). Queue order is survival, existing-chain
// deadline, faction review, then new warning. One new decision window per day.
// ---------------------------------------------------------------------------

void Session::update_events() {
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);

  // 1. Resolve or expire open instances.
  for (auto& inst : state_.events) {
    if (inst.resolution != EventResolution::Open) continue;
    const EventDef* def = catalog_->find_event(inst.event_id);
    if (def == nullptr) continue;
    MetricContext ctx{inst.planet_id, inst.facility_id};
    if (def->has_condition_resolved && sim::evaluate_condition(state_, *catalog_, def->condition_resolved, ctx)) {
      inst.resolution = EventResolution::Closed;
      for (std::size_t i = 0; i < def->on_resolve.size(); ++i) {
        sim::execute_effect(state_, *catalog_, def->on_resolve[i], def->id, "", "on_resolve", static_cast<int>(i),
                            inst.planet_id, inst.facility_id, inst.id);
      }
      InstanceId fact = sim::emit_fact(state_, "event_prevented", inst.planet_id, inst.facility_id, {}, {def->id},
                                       reason::kOk, inst.trigger_facts, "");
      if (!def->news_template_resolved.empty()) {
        sim::emit_news(state_, *catalog_, def->news_template_resolved, inst.planet_id, {}, {def->id}, {fact},
                       "event_prevented:" + to_decimal_string_u(inst.id), 1);
      }
      continue;
    }
    if (inst.deadline_day >= 0 && state_.day >= inst.deadline_day) {
      for (std::size_t i = 0; i < def->on_expire.size(); ++i) {
        sim::execute_effect(state_, *catalog_, def->on_expire[i], def->id, "", "on_expire", static_cast<int>(i),
                            inst.planet_id, inst.facility_id, inst.id);
      }
      InstanceId fact = sim::emit_fact(state_, "event_expired", inst.planet_id, inst.facility_id, {}, {def->id},
                                       reason::kOk, inst.trigger_facts, "");
      if (!def->news_template_expire.empty()) {
        sim::emit_news(state_, *catalog_, def->news_template_expire, inst.planet_id, {}, {def->id}, {fact},
                       "event_expired:" + to_decimal_string_u(inst.id), 1);
      }
      if (def->expire_keeps_open) {
        inst.deadline_day = -1;   // remedies stay selectable
      } else {
        inst.resolution = EventResolution::Expired;
      }
    }
  }
  bool any_window = false;
  for (const auto& e : state_.events) {
    if (e.resolution == EventResolution::Open && e.deadline_day >= 0) any_window = true;
  }
  if (!any_window) state_.paused_for_decision = false;

  // 2. Evaluate authored trigger conditions and record consecutive-day streaks.
  struct Candidate {
    int queue_class;
    int streak;
    std::string event_id;
    std::string planet_id;
    InstanceId facility_id;
  };
  std::vector<Candidate> candidates;

  for (const std::string& event_id : sc.event_ids) {
    const EventDef* def = catalog_->find_event(event_id);
    if (def == nullptr || !def->has_condition) continue;

    auto consider = [&](const std::string& planet_id, InstanceId facility_id, const std::string& streak_suffix) {
      MetricContext ctx{planet_id, facility_id};
      const std::string key = event_id + "@" + streak_suffix;
      bool holds = sim::evaluate_condition(state_, *catalog_, def->condition, ctx);
      int& streak = state_.condition_streaks[key];
      streak = holds ? streak + 1 : 0;
      if (!holds || streak < std::max(def->consecutive_days, 1)) return;
      auto cd = state_.cooldowns.find(event_id + "@" + planet_id);
      if (cd != state_.cooldowns.end() && state_.day < cd->second) return;
      if (sim::chain_open_on_planet(state_, *catalog_, def->chain_id, planet_id)) return;
      if (def->sector_unique) {
        for (const auto& inst : state_.events) {
          if (inst.event_id == event_id && inst.resolution == EventResolution::Open) return;
        }
      }
      candidates.push_back({static_cast<int>(def->queue_class), streak, event_id, planet_id, facility_id});
    };

    for (const auto& planet : state_.planets) {
      if (!planet.colonised) continue;
      if (def->scope == "planet") {
        consider(planet.planet_id, 0, planet.planet_id);
      } else {
        for (const auto& f : state_.facilities) {
          if (f.planet_id != planet.planet_id) continue;
          if (!def->applies_to_facility.empty() && f.facility_id != def->applies_to_facility) continue;
          consider(planet.planet_id, f.id, "f" + to_decimal_string_u(f.id));
        }
      }
    }
  }

  // Open one Safety Warning for the oldest eligible site, tie-breaking by
  // instance id: longest streak first, then lowest id.
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.queue_class != b.queue_class) return a.queue_class < b.queue_class;
    if (a.streak != b.streak) return a.streak > b.streak;
    if (a.event_id != b.event_id) return a.event_id < b.event_id;
    if (a.planet_id != b.planet_id) return a.planet_id < b.planet_id;
    return a.facility_id < b.facility_id;
  });
  for (const auto& c : candidates) {
    sim::request_event(state_, c.event_id, c.planet_id, c.facility_id, {});
  }

  // 3. Drain the request queue in scheduler order.
  std::vector<EventRequest> requests = state_.event_requests;
  std::sort(requests.begin(), requests.end(), [&](const EventRequest& a, const EventRequest& b) {
    const EventDef* da = catalog_->find_event(a.event_id);
    const EventDef* db = catalog_->find_event(b.event_id);
    int ca = da == nullptr ? 99 : static_cast<int>(da->queue_class);
    int cb = db == nullptr ? 99 : static_cast<int>(db->queue_class);
    if (ca != cb) return ca < cb;
    if (a.requested_day != b.requested_day) return a.requested_day < b.requested_day;
    return a.id < b.id;
  });

  std::vector<EventRequest> deferred;
  for (const auto& r : requests) {
    const EventDef* def = catalog_->find_event(r.event_id);
    if (def == nullptr) continue;
    const bool is_decision = !def->choices.empty();
    // A forecast clone suppresses new discretionary events and discloses it.
    if (forecast_mode_ && is_decision) {
      deferred.push_back(r);
      continue;
    }
    auto cd = state_.cooldowns.find(r.event_id + "@" + r.planet_id);
    if (cd != state_.cooldowns.end() && state_.day < cd->second) continue;   // drop, do not queue forever
    if (def->sector_unique) {
      bool already = false;
      for (const auto& inst : state_.events) {
        if (inst.event_id == r.event_id && inst.resolution == EventResolution::Open) already = true;
      }
      if (already) continue;
    }
    if (is_decision) {
      // Do not spawn new pressure decisions while a survival emergency is
      // unresolved, and open at most one new decision window per day.
      bool emergency = false;
      for (const auto& p : state_.planets) {
        if (p.survival_emergency_active) emergency = true;
      }
      if (emergency && def->queue_class != EventQueueClass::Survival) {
        deferred.push_back(r);
        continue;
      }
      if (state_.decisions_opened_today >= 1) {
        deferred.push_back(r);
        continue;
      }
    }
    // A chain escalates only once its previous decision window has closed.
    if (is_decision) {
      bool window_open = false;
      for (const auto& inst : state_.events) {
        if (inst.resolution != EventResolution::Open) continue;
        if (inst.planet_id != r.planet_id) continue;
        if (inst.deadline_day < 0) continue;
        const EventDef* d = catalog_->find_event(inst.event_id);
        if (d != nullptr && d->chain_id == def->chain_id) window_open = true;
      }
      if (window_open) {
        deferred.push_back(r);
        continue;
      }
    }
    sim::open_event(state_, *catalog_, r.event_id, r.planet_id, r.facility_id, r.trigger_facts);
  }
  state_.event_requests = deferred;

  // Faction Review is opened by the same queue through its authored condition;
  // nothing here drains stability additionally.
}

// ---------------------------------------------------------------------------
// Milestones (TDD 13.3). A repeat milestone needs a genuine new high at least
// 10% above the previous recorded high and a seven-day news cooldown.
// ---------------------------------------------------------------------------

void Session::update_milestones() {
  const auto& rules = catalog_->news_rules();
  for (auto& planet : state_.planets) {
    if (!planet.colonised) continue;
    auto& ps = scratch_->at(planet.planet_id);
    for (int r = 0; r < catalog_->resource_count(); ++r) {
      const Milli made = ps.produced[static_cast<std::size_t>(r)];
      if (made <= 0) continue;
      Milli& high = planet.milestone_high[r];
      const bool first = high == 0;
      const Milli threshold = first ? 0 : checked_add(high, mul_div_ceil(high, rules.milestone_min_increase_bp, kBpOne));
      if (made <= threshold) continue;
      Day& last = planet.milestone_last_news_day[r];
      const bool cooldown_ok = first || (state_.day - last) >= rules.milestone_cooldown_days;
      high = made;
      if (!cooldown_ok) continue;
      last = state_.day;
      InstanceId fact = sim::emit_fact(state_, "production_high", planet.planet_id, 0, {{"quantity", made}},
                                       {catalog_->resource(r).id}, reason::kOk, {}, "");
      sim::emit_news(state_, *catalog_, first ? "news.first_surplus" : "news.production_high", planet.planet_id,
                     {{"quantity", made}}, {catalog_->resource(r).id}, {fact},
                     "milestone:" + planet.planet_id + ":" + catalog_->resource(r).id + ":" +
                         to_decimal_string(state_.day),
                     first ? 0 : 2);
    }
  }
}

// ---------------------------------------------------------------------------
// Completion predicates (TDD 14.5)
// ---------------------------------------------------------------------------

void Session::evaluate_completion() {
  const ScenarioDef& sc = catalog_->scenario(state_.scenario_id);

  // Ten consecutive final days with full Food, Water and residential power
  // fulfilment, no active survival emergency, and no missed colonial Food
  // manifest due to strategic diversion.
  bool clean_today = true;
  for (const auto& planet : state_.planets) {
    if (!planet.colonised) continue;
    if (planet.last_day.food_fulfilment_bp < kBpOne) clean_today = false;
    if (planet.last_day.water_fulfilment_bp < kBpOne) clean_today = false;
    if (planet.last_day.power_fulfilment_bp < kBpOne) clean_today = false;
    if (planet.survival_emergency_active) clean_today = false;
  }
  static_cast<void>(0);
  for (auto& planet : state_.planets) {
    planet.clean_day_streak = clean_today ? planet.clean_day_streak + 1 : 0;
  }

  if (state_.lifecycle != SessionLifecycle::Running) return;
  if (state_.day < sc.completion.evaluation_day) return;

  std::vector<std::string> failures;
  const ColonizationState& col = state_.colonisation;
  if (!col.launched || col.launch_day > sc.completion.colony_launch_by_day) {
    failures.push_back("colony_not_launched_in_time");
  }
  if (!col.founded || (state_.day - col.founded_day) < sc.completion.colony_founded_min_days_before_end) {
    failures.push_back("colony_founded_too_late");
  }
  if (!state_.flags.count("delivery_outbound") || !state_.flags.count("delivery_inbound")) {
    failures.push_back("two_way_delivery_missing");
  }
  if (state_.demand.decision != MandateDecision::Honoured && state_.demand.decision != MandateDecision::Negotiated) {
    failures.push_back("mandate_unfulfilled");
  }
  for (const auto& planet : state_.planets) {
    if (!planet.colonised) continue;
    if (planet.health_bp < sc.completion.min_health_bp) failures.push_back("health_below_floor:" + planet.planet_id);
    if (planet.stability_bp < sc.completion.min_stability_bp) {
      failures.push_back("stability_below_floor:" + planet.planet_id);
    }
    if (planet.clean_day_streak < sc.completion.final_clean_days) {
      failures.push_back("final_days_not_clean:" + planet.planet_id);
    }
  }
  if (state_.history.missed_colonial_food_manifests > 0) failures.push_back("missed_colonial_food_manifest");

  state_.lifecycle = failures.empty() ? SessionLifecycle::Complete : SessionLifecycle::Compromised;
  std::vector<std::string> text_args = failures;
  bool relief_used = false;
  for (const auto& p : state_.planets) {
    if (p.relief_used) relief_used = true;
  }
  InstanceId fact = sim::emit_fact(state_, "scenario_evaluated", "", 0,
                                   {{"relief_used", relief_used ? 1 : 0},
                                    {"adherence", state_.political.adherence},
                                    {"failed_predicates", static_cast<std::int64_t>(failures.size())}},
                                   text_args, session_lifecycle_id(state_.lifecycle), {}, "");
  sim::emit_news(state_, *catalog_,
                 failures.empty() ? "news.scenario_complete" : "news.scenario_compromised", "",
                 {{"relief_used", relief_used ? 1 : 0}}, text_args, {fact}, "scenario_evaluated", 0);
}

}  // namespace expansion
