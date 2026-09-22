#include "expansion/view_models.hpp"

#include <algorithm>
#include <cstddef>

#include "expansion/derived.hpp"
#include "expansion/text.hpp"

namespace expansion::view {

namespace {

Quantity milli(std::int64_t v) { return Quantity{v, format_milli(v)}; }

std::string label_of(const std::string& content_id) { return text::display_name(content_id); }

// A definition's authored name, falling back to words from its id.
std::string named(const std::string& display_key, const std::string& content_id) {
  return text::name_of(display_key, content_id);
}

// Days of cover at today's net drain. Only meaningful while stock is falling, so
// a rising or steady store reports -1 rather than a number that would be read as
// a countdown.
Day cover_days(Milli on_hand, std::int64_t net_today) {
  if (net_today >= 0) return -1;
  const Milli drain = -net_today;
  if (on_hand <= 0) return 0;
  return div_floor(on_hand, drain);
}

StockRow stock_row(const Catalog& catalog, const PlanetState& planet, int resource) {
  const auto index = static_cast<std::size_t>(resource);
  const DailyReport& d = planet.last_day;
  StockRow row;
  row.resource = resource;
  row.resource_id = catalog.resource(resource).id;
  row.label = named(catalog.resource(resource).display_key, row.resource_id);
  const Milli on_hand = planet.inventory.on_hand[index];
  row.on_hand = milli(on_hand);
  row.reserved = milli(planet.inventory.reserved(resource));
  row.available = milli(planet.inventory.available(resource));
  row.capacity = milli(planet.inventory.capacity_per_resource);
  row.fill_bp = planet.inventory.capacity_per_resource > 0
                    ? clamp_bp(mul_div_floor(on_hand, kBpOne, planet.inventory.capacity_per_resource))
                    : 0;
  row.full = planet.inventory.free_space(resource) == 0;
  const Milli produced = index < d.produced.size() ? d.produced[index] : 0;
  const Milli consumed = index < d.consumed.size() ? d.consumed[index] : 0;
  row.produced_today = milli(produced);
  row.consumed_today = milli(consumed);
  // Production and consumption alone do not explain a store's movement -- freight
  // and escrow move stock too -- so the net is measured against the day's own
  // opening and closing figures when the day has run.
  if (d.day >= 0 && index < d.opening_stock.size() && index < d.closing_stock.size()) {
    row.net_today = checked_sub(d.closing_stock[index], d.opening_stock[index]);
  } else {
    row.net_today = checked_sub(produced, consumed);
  }
  row.days_of_cover = cover_days(on_hand, row.net_today);
  auto it = planet.shortages.find(resource);
  row.shortage_open = it != planet.shortages.end() && it->second.open;
  return row;
}

NeedRow need_row(const char* id, Bp fulfilment, Milli demand, Milli served) {
  NeedRow row;
  row.need_id = id;
  row.label = label_of(id);
  row.fulfilment_bp = fulfilment;
  row.demand = milli(demand);
  row.served = milli(served);
  row.met = fulfilment >= kBpOne;
  return row;
}

const RecipeDef* active_recipe(const Catalog& catalog, const FacilityState& f) {
  const FacilityDef* def = catalog.find_facility(f.facility_id);
  if (def == nullptr) return nullptr;
  const int index = def->recipe_index(f.recipe_id);
  if (index < 0) return def->recipes.empty() ? nullptr : &def->recipes.front();
  return &def->recipes[static_cast<std::size_t>(index)];
}

std::string facility_state_label(const FacilityState& f) {
  switch (f.lifecycle) {
    case FacilityLifecycle::UnderConstruction: {
      Bp progress = 0;
      if (f.construction.has_value() && f.construction->work_total > 0) {
        progress = clamp_bp(mul_div_floor(f.construction->work_done, kBpOne, f.construction->work_total));
      }
      return "Under construction " + format_bp_percent(progress);
    }
    case FacilityLifecycle::Commissioning: return "Commissioning";
    case FacilityLifecycle::Cancelled: return "Cancelled";
    case FacilityLifecycle::Active: break;
  }
  if (f.service.has_value()) return "In service";
  if (f.idle) return "Idle";
  return "Active";
}

std::vector<std::string> resource_labels(const Catalog& catalog, const std::vector<int>& indices) {
  std::vector<std::string> out;
  out.reserve(indices.size());
  for (int r : indices) {
    if (r < 0 || r >= catalog.resource_count()) continue;
    out.push_back(named(catalog.resource(r).display_key, catalog.resource(r).id));
  }
  return out;
}

const char* mandate_status(const DemandState& d) {
  if (!d.issued) return "";
  switch (d.decision) {
    case MandateDecision::Pending: return "Awaiting a response";
    case MandateDecision::Honoured: return "Honoured";
    case MandateDecision::Negotiated: return "Negotiated";
    case MandateDecision::Declined: return "Declined";
    case MandateDecision::Failed: return "Failed";
  }
  return "";
}

SectorView build_sector(const Session& session) {
  const SessionState& s = session.state();
  const Catalog& catalog = session.catalog();
  SectorView out;
  out.day = s.day;
  out.revision = s.revision;
  out.lifecycle = s.lifecycle;
  out.lifecycle_label = label_of(session_lifecycle_id(s.lifecycle));
  out.scenario_id = s.scenario_id;
  out.faction_id = s.political.faction_id;
  const FactionDef* faction = nullptr;
  for (const auto& f : catalog.factions()) {
    if (f.id == s.faction_id) faction = &f;
  }
  out.faction_label = faction != nullptr ? named(faction->display_key, faction->id) : label_of(s.faction_id);
  out.adherence = s.political.adherence;
  const ScenarioDef& scenario = catalog.scenario(s.scenario_id);
  out.evaluation_day = scenario.completion.evaluation_day;
  out.days_to_evaluation = std::max<Day>(0, scenario.completion.evaluation_day - s.day);
  out.paused_for_decision = s.paused_for_decision;
  out.concerns = read::top_concerns(session, 3);
  for (const auto& p : s.planets) out.planet_ids.push_back(p.planet_id);
  for (const auto& e : s.events) {
    if (e.resolution == EventResolution::Open) out.open_decisions += 1;
  }
  out.mandate_issued = s.demand.issued;
  out.mandate_resolved = s.demand.resolved;
  out.mandate_status_label = mandate_status(s.demand);
  out.mandate_deadline_day = s.demand.deadline_day;
  return out;
}

PlanetView build_planet(const Session& session, const std::string& planet_id) {
  const SessionState& s = session.state();
  const Catalog& catalog = session.catalog();
  const PlanetState* p = s.find_planet(planet_id);
  if (p == nullptr) throw SimError(ErrorCode::NotFound, "view: no planet '" + planet_id + "'");
  const DailyReport& d = p->last_day;

  PlanetView out;
  out.planet_id = p->planet_id;
  const PlanetDef* planet_def = catalog.find_planet(p->planet_id);
  out.label = planet_def != nullptr ? named(planet_def->display_key, p->planet_id) : label_of(p->planet_id);
  out.colonised = p->colonised;
  out.population = p->population;
  out.workers_total = static_cast<int>(p->workers_total);
  out.workers_assigned = d.workers_assigned;
  out.workers_reserve = reserve_workers_of(s, *p);
  out.workers_transitioning = d.workers_transitioning;
  out.workers_crew = d.workers_crew;
  out.health_bp = p->health_bp;
  out.stability_bp = p->stability_bp;
  out.fatigue_bp = p->fatigue_bp;
  out.policy_id = p->policy_id;
  const PolicyDef* policy_def = p->policy_id.empty() ? nullptr : catalog.find_policy(p->policy_id);
  out.policy_label = policy_def != nullptr ? named(policy_def->display_key, p->policy_id)
                                           : (p->policy_id.empty() ? "" : label_of(p->policy_id));
  out.policy_effective_days = p->policy_effective_days;
  out.survival_emergency = p->survival_emergency_active;
  out.relief_pending_day = p->relief_pending_day;
  out.power_generated = milli(d.power.generated);
  out.power_used = milli(d.power.used);
  out.power_spare = milli(d.power.unused);
  out.port_handling_capacity = milli(d.port_handling_capacity);
  out.port_handling_used = milli(d.port_handling_used);

  out.needs.push_back(need_row("food", d.food_fulfilment_bp, d.food_demand, d.food_served));
  out.needs.push_back(need_row("water", d.water_fulfilment_bp, d.water_demand, d.water_served));
  out.needs.push_back(need_row("power", d.power_fulfilment_bp, d.power.residential_demand,
                               d.power.residential_served));
  NeedRow housing = need_row("housing", d.housing_fulfilment_bp, p->population * kMilliOne,
                             d.housing_capacity * kMilliOne);
  housing.label = "Housing";
  out.needs.push_back(housing);
  NeedRow clinic = need_row("clinic", d.clinic_coverage_bp, p->population * kMilliOne,
                            d.clinic_capacity * kMilliOne);
  clinic.label = "Clinic coverage";
  out.needs.push_back(clinic);

  for (int r = 0; r < catalog.resource_count(); ++r) out.stores.push_back(stock_row(catalog, *p, r));
  for (const auto& f : s.facilities) {
    if (f.planet_id == p->planet_id) out.facilities.push_back(f.id);
  }
  return out;
}

FacilityCard build_facility(const Session& session, InstanceId facility_id) {
  const SessionState& s = session.state();
  const Catalog& catalog = session.catalog();
  const FacilityState* f = s.find_facility(facility_id);
  if (f == nullptr) {
    throw SimError(ErrorCode::NotFound, "view: no facility " + to_decimal_string_u(facility_id));
  }
  const FacilityExplanation& x = f->last_explanation;
  const RecipeDef* recipe = active_recipe(catalog, *f);

  FacilityCard out;
  out.id = f->id;
  out.facility_id = f->facility_id;
  out.recipe_id = f->recipe_id;
  out.planet_id = f->planet_id;
  const FacilityDef* def = catalog.find_facility(f->facility_id);
  out.label = def != nullptr ? named(def->display_key, f->facility_id) : label_of(f->facility_id);
  // A multi-variant facility is ambiguous without its recipe, so name it.
  if (def != nullptr && def->recipes.size() > 1 && recipe != nullptr) {
    out.label += " -- " + named(recipe->display_key, recipe->id);
  }
  out.state_label = facility_state_label(*f);
  out.lifecycle = f->lifecycle;
  out.assigned_workers = f->assigned_workers;
  out.required_workers = recipe != nullptr ? recipe->staff : 0;
  out.staffing_bp = x.staffing_bp;
  out.condition_bp = f->condition_bp;
  out.priority_band = f->priority_band;
  out.idle = f->idle;
  out.servicing = f->service.has_value();
  out.under_construction = f->lifecycle == FacilityLifecycle::UnderConstruction;
  if (f->construction.has_value() && f->construction->work_total > 0) {
    out.construction_progress_bp =
        clamp_bp(mul_div_floor(f->construction->work_done, kBpOne, f->construction->work_total));
    out.assigned_workers = f->construction->assigned_workers;
  }
  out.throughput_bp = x.actual_throughput_bp;
  out.desired_throughput_bp = x.desired_throughput_bp;
  out.reason_id = x.primary_reason;
  out.reason_text = x.primary_reason.empty() ? "" : text::reason_text(x.primary_reason);
  out.power_limited = x.power_limited;
  out.labour_limited = x.labour_limited;
  out.missing_inputs = resource_labels(catalog, x.missing_inputs);
  out.blocked_outputs = resource_labels(catalog, x.blocked_outputs);
  out.modifier_tags = x.modifier_tags;
  out.power_requested = milli(x.power_requested);
  out.power_granted = milli(x.power_granted);
  for (const auto& [resource, quantity] : x.outputs_produced) {
    StockRow row;
    row.resource = resource;
    row.resource_id = catalog.resource(resource).id;
    row.label = named(catalog.resource(resource).display_key, row.resource_id);
    row.produced_today = milli(quantity);
    row.net_today = quantity;
    out.outputs.push_back(row);
  }
  return out;
}

FreightView build_freight(const Session& session) {
  const SessionState& s = session.state();
  const Catalog& catalog = session.catalog();
  const ShipState& ship = s.ship;

  FreightView out;
  out.phase = ship.phase;
  out.mission = ship.mission;
  out.phase_label = label_of(ship_phase_id(ship.phase));
  auto planet_label = [&](const std::string& id) {
    if (id.empty()) return std::string();
    const PlanetDef* def = catalog.find_planet(id);
    return def != nullptr ? named(def->display_key, id) : label_of(id);
  };
  out.location_label = planet_label(ship.location_planet);
  out.destination_label = planet_label(ship.destination_planet);
  out.departure_day = ship.departure_day;
  out.arrival_day = ship.arrival_day;
  out.days_remaining = ship.arrival_day >= 0 ? std::max<Day>(0, ship.arrival_day - s.day) : -1;
  out.earliest_departure_day = ship.earliest_departure_day;
  out.next_scheduled_departure_day = s.route.next_departure_day;
  out.route_enabled = s.route.enabled;
  out.departure_authorised = s.route.departure_requested;
  out.fuel_in_tank = milli(ship.tank);
  out.cargo_volume = milli(cargo_volume_of(catalog, ship.cargo));
  out.missed_manifests = s.history.missed_colonial_food_manifests;

  auto rows = [&](const ResourceMap& map) {
    std::vector<StockRow> v;
    for (const auto& [resource, quantity] : map) {
      StockRow row;
      row.resource = resource;
      row.resource_id = catalog.resource(resource).id;
      row.label = named(catalog.resource(resource).display_key, row.resource_id);
      row.on_hand = milli(quantity);
      v.push_back(row);
    }
    return v;
  };
  out.cargo = rows(ship.cargo);
  out.outbound_targets = rows(s.route.outbound_targets);
  out.return_targets = rows(s.route.return_targets);
  return out;
}

std::vector<DecisionCard> build_decisions(const Session& session) {
  const SessionState& s = session.state();
  const Catalog& catalog = session.catalog();
  std::vector<DecisionCard> out;
  for (const auto& e : s.events) {
    if (e.resolution != EventResolution::Open) continue;
    const EventDef* def = catalog.find_event(e.event_id);
    DecisionCard card;
    card.id = e.id;
    card.event_id = e.event_id;
    card.planet_id = e.planet_id;
    card.facility_id = e.facility_id;
    card.title = def != nullptr ? named(def->display_key, e.event_id) : label_of(e.event_id);
    card.opened_day = e.opened_day;
    card.deadline_day = e.deadline_day;
    card.days_remaining = e.deadline_day >= 0 ? std::max<Day>(0, e.deadline_day - s.day) : -1;
    card.critical = def != nullptr && def->pauses_simulation;
    if (def != nullptr) {
      // The event's own opening line, with the world named: a card must say what
      // happened before it offers remedies.
      card.body = def->news_template_open.empty()
                      ? ""
                      : text::with_planet(text::format(def->news_template_open, {}, {}), e.planet_id);
      const PlanetState* planet_state = s.find_planet(e.planet_id);
      for (const auto& choice : def->choices) {
        DecisionOption option;
        option.choice_id = choice.id;
        option.label = named(choice.display_key, choice.id);
        std::string cost;
        for (const auto& [resource, quantity] : choice.cost) {
          if (!cost.empty()) cost += ", ";
          cost += format_milli(quantity) + " " +
                  named(catalog.resource(resource).display_key, catalog.resource(resource).id);
          if (planet_state != nullptr && planet_state->inventory.available(resource) < quantity) {
            option.affordable = false;
          }
        }
        // A staffing requirement is part of what a choice costs, so it is always
        // stated; falling short of it makes the choice unaffordable for the same
        // reason falling short of stock does.
        if (choice.required_reserve_workers > 0) {
          if (!cost.empty()) cost += ", ";
          cost += std::to_string(choice.required_reserve_workers) + " workers in reserve";
          if (planet_state != nullptr &&
              reserve_workers_of(s, *planet_state) < choice.required_reserve_workers) {
            option.affordable = false;
          }
        }
        if (choice.required_assigned_workers > 0) {
          if (!cost.empty()) cost += ", ";
          cost += std::to_string(choice.required_assigned_workers) + " workers on site";
        }
        option.cost_summary = cost;
        card.options.push_back(option);
      }
    }
    // Name what the decision is about. A facility-scoped decision that reports
    // only its world leaves the player hunting for the site.
    if (e.facility_id != 0) {
      const FacilityState* subject = s.find_facility(e.facility_id);
      if (subject != nullptr) {
        const FacilityDef* subject_def = catalog.find_facility(subject->facility_id);
        card.subject_label = subject_def != nullptr ? named(subject_def->display_key, subject->facility_id)
                                                    : label_of(subject->facility_id);
        card.subject_label += " at " + label_of(subject->planet_id) +
                              " -- condition " + format_bp_percent(subject->condition_bp);
      }
    }
    if (card.subject_label.empty() && !e.planet_id.empty()) card.subject_label = label_of(e.planet_id);

    for (InstanceId fact_id : e.trigger_facts) {
      for (const auto& fact : s.facts) {
        if (fact.id == fact_id) card.because.push_back(text::fact_line(fact));
      }
    }
    out.push_back(card);
  }
  return out;
}

std::vector<HistoryEntry> build_history(const Session& session, int max_entries) {
  const SessionState& s = session.state();
  std::vector<HistoryEntry> out;
  if (max_entries <= 0) return out;
  // Newest first: an archive is read backwards.
  for (auto it = s.news.rbegin(); it != s.news.rend(); ++it) {
    if (static_cast<int>(out.size()) >= max_entries) break;
    HistoryEntry entry;
    entry.id = it->id;
    entry.day = it->day;
    entry.text = text::news_line(*it);
    entry.planet_id = it->planet_id;
    entry.priority = it->priority;
    entry.is_news = true;
    out.push_back(entry);
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The non-throwing boundary. Mirrors api.hpp's guard so a host compiled without
// exceptions can call every view.
// ---------------------------------------------------------------------------

namespace {

template <typename T, typename Fn>
api::Outcome<T> guard(Fn&& fn) {
  try {
    return api::Outcome<T>::success(fn());
  } catch (const SimError& e) {
    return api::Outcome<T>::failure(e.code() == ErrorCode::Internal ? ErrorCode::Internal : e.code(), e.what());
  } catch (const std::exception& e) {
    return api::Outcome<T>::failure(ErrorCode::Internal, std::string("view: ") + e.what());
  } catch (...) {
    return api::Outcome<T>::failure(ErrorCode::Internal, "view: unknown failure");
  }
}

}  // namespace

api::Outcome<SectorView> sector(const Session& session) {
  return guard<SectorView>([&] { return build_sector(session); });
}

api::Outcome<PlanetView> planet(const Session& session, const std::string& planet_id) {
  return guard<PlanetView>([&] { return build_planet(session, planet_id); });
}

api::Outcome<FacilityCard> facility(const Session& session, InstanceId facility_id) {
  return guard<FacilityCard>([&] { return build_facility(session, facility_id); });
}

api::Outcome<FreightView> freight(const Session& session) {
  return guard<FreightView>([&] { return build_freight(session); });
}

api::Outcome<std::vector<DecisionCard>> decisions(const Session& session) {
  return guard<std::vector<DecisionCard>>([&] { return build_decisions(session); });
}

api::Outcome<std::vector<HistoryEntry>> history(const Session& session, int max_entries) {
  return guard<std::vector<HistoryEntry>>([&] { return build_history(session, max_entries); });
}

}  // namespace expansion::view
