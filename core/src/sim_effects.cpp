#include <algorithm>
#include <cstddef>

#include "expansion/metrics.hpp"
#include "sim_internal.hpp"

namespace expansion::sim {

namespace {

void sort_scheduled(SessionState& state) {
  std::sort(state.scheduled.begin(), state.scheduled.end(), [](const ScheduledEffect& a, const ScheduledEffect& b) {
    if (a.due_day != b.due_day) return a.due_day < b.due_day;
    if (a.priority != b.priority) return a.priority < b.priority;
    return a.id < b.id;
  });
}

std::vector<ActiveModifier>* modifier_list(SessionState& state, const EffectDef& e, const std::string& planet_id,
                                           InstanceId facility_id) {
  if (e.target == "sector") return &state.political.modifiers;
  if (e.target == "event_planet") {
    PlanetState* p = state.find_planet(planet_id);
    return p == nullptr ? nullptr : &p->modifiers;
  }
  // "event_target": the facility when the event opened on one, else the planet.
  if (facility_id != 0) {
    FacilityState* f = state.find_facility(facility_id);
    return f == nullptr ? nullptr : &f->modifiers;
  }
  PlanetState* p = state.find_planet(planet_id);
  return p == nullptr ? nullptr : &p->modifiers;
}

void add_modifier(SessionState& state, std::vector<ActiveModifier>& list, const EffectDef& e, Day now,
                  const std::string& event_id, InstanceId source_instance) {
  ActiveModifier m;
  m.id = state.allocate_id();
  m.tag = e.tag.empty() ? e.key : e.tag;
  m.target = e.modifier_target;
  m.factor_bp = e.factor_bp;
  m.amount = e.amount;
  // Active for `duration_days` days counted from the next day to resolve; 0
  // means until explicitly cleared.
  m.expires_day = e.duration_days > 0 ? checked_add(now, e.duration_days) : -1;
  m.source_event_id = event_id;
  m.source_event_instance = source_instance;
  list.push_back(m);
  std::sort(list.begin(), list.end(), [](const ActiveModifier& a, const ActiveModifier& b) { return a.id < b.id; });
}

}  // namespace

void request_event(SessionState& state, const std::string& event_id, const std::string& planet_id,
                   InstanceId facility_id, std::vector<InstanceId> trigger_facts) {
  for (const auto& r : state.event_requests) {
    if (r.event_id == event_id && r.planet_id == planet_id && r.facility_id == facility_id) return;
  }
  EventRequest r;
  r.id = state.allocate_id();
  r.event_id = event_id;
  r.planet_id = planet_id;
  r.facility_id = facility_id;
  r.requested_day = state.day;
  r.trigger_facts = std::move(trigger_facts);
  state.event_requests.push_back(r);
  std::sort(state.event_requests.begin(), state.event_requests.end(),
            [](const EventRequest& a, const EventRequest& b) { return a.id < b.id; });
}

bool chain_open_on_planet(const SessionState& state, const Catalog& cat, const std::string& chain_id,
                          const std::string& planet_id) {
  for (const auto& e : state.events) {
    if (e.resolution != EventResolution::Open) continue;
    if (e.planet_id != planet_id) continue;
    const EventDef* d = cat.find_event(e.event_id);
    if (d != nullptr && d->chain_id == chain_id) return true;
  }
  return false;
}

InstanceId open_event(SessionState& state, const Catalog& cat, const std::string& event_id,
                      const std::string& planet_id, InstanceId facility_id, std::vector<InstanceId> trigger_facts) {
  const EventDef* def = cat.find_event(event_id);
  if (def == nullptr) throw SimError("events: unknown event '" + event_id + "'");

  EventInstance inst;
  inst.id = state.allocate_id();
  inst.event_id = def->id;
  inst.chain_id = def->chain_id;
  inst.planet_id = planet_id;
  inst.facility_id = facility_id;
  inst.opened_day = state.day;
  inst.deadline_day = def->decision_deadline_days > 0 ? checked_add(state.day, def->decision_deadline_days) : -1;
  inst.resolution = (def->choices.empty() && def->decision_deadline_days <= 0) ? EventResolution::Closed
                                                                               : EventResolution::Open;
  inst.trigger_facts = std::move(trigger_facts);
  state.events.push_back(inst);
  std::sort(state.events.begin(), state.events.end(),
            [](const EventInstance& a, const EventInstance& b) { return a.id < b.id; });

  if (def->cooldown_days > 0) {
    state.cooldowns[event_id + "@" + planet_id] = checked_add(state.day, def->cooldown_days);
  }
  state.flag_days["event_opened:" + event_id + "@" + planet_id] = state.day;

  InstanceId fact = emit_fact(state, "event_opened", planet_id, facility_id, {}, {event_id}, event_id,
                              state.find_event_instance(inst.id)->trigger_facts, "");
  if (!def->news_template_open.empty()) {
    emit_news(state, cat, def->news_template_open, planet_id, {{"event_instance", static_cast<std::int64_t>(inst.id)}},
              {event_id}, {fact}, "event_open:" + to_decimal_string_u(inst.id), def->choices.empty() ? 2 : 1);
  }

  for (std::size_t i = 0; i < def->on_open.size(); ++i) {
    execute_effect(state, cat, def->on_open[i], def->id, "", "on_open", static_cast<int>(i), planet_id, facility_id,
                   inst.id);
  }
  if (!def->choices.empty()) {
    state.decisions_opened_today += 1;
    if (def->pauses_simulation) state.paused_for_decision = true;
  }
  return inst.id;
}

void close_events(SessionState& state, const std::string& event_id, const std::string& planet_id, bool whole_chain,
                  const Catalog& cat) {
  const EventDef* target = cat.find_event(event_id);
  for (auto& e : state.events) {
    if (e.resolution != EventResolution::Open) continue;
    if (e.planet_id != planet_id) continue;
    bool match = e.event_id == event_id;
    if (!match && whole_chain && target != nullptr) {
      const EventDef* d = cat.find_event(e.event_id);
      match = d != nullptr && d->chain_id == target->chain_id;
    }
    if (match) e.resolution = EventResolution::Closed;
  }
  // A closed decision window no longer holds the simulation paused.
  bool any_open = false;
  for (const auto& e : state.events) {
    if (e.resolution == EventResolution::Open && e.deadline_day >= 0) any_open = true;
  }
  if (!any_open) state.paused_for_decision = false;
}

void queue_effect_list(SessionState& state, const std::vector<EffectDef>& effects, const std::string& event_id,
                       const std::string& choice_id, const std::string& list_name, const std::string& planet_id,
                       InstanceId facility_id, InstanceId source_event_instance, Day base_day) {
  for (std::size_t i = 0; i < effects.size(); ++i) {
    const EffectDef& e = effects[i];
    if (e.kind != EffectKind::ScheduleEffect) continue;
    for (std::size_t n = 0; n < e.nested.size(); ++n) {
      ScheduledEffect s;
      s.id = state.allocate_id();
      s.due_day = checked_add(base_day, e.delay_days);
      s.priority = static_cast<int>(n);
      s.event_id = event_id;
      s.choice_id = choice_id;
      s.list_name = list_name;
      s.effect_index = static_cast<int>(i);
      s.nested_index = static_cast<int>(n);
      s.planet_id = planet_id;
      s.facility_id = facility_id;
      s.source_event_instance = source_event_instance;
      state.scheduled.push_back(s);
    }
  }
  sort_scheduled(state);
}

const EffectDef* resolve_scheduled_effect(const Catalog& cat, const ScheduledEffect& s) {
  const EventDef* def = cat.find_event(s.event_id);
  if (def == nullptr) return nullptr;
  const std::vector<EffectDef>* list = nullptr;
  if (s.list_name == "on_open") {
    list = &def->on_open;
  } else if (s.list_name == "on_resolve") {
    list = &def->on_resolve;
  } else if (s.list_name == "on_expire") {
    list = &def->on_expire;
  } else {
    for (const auto& c : def->choices) {
      if (c.id == s.choice_id) {
        list = &c.effects;
        break;
      }
    }
  }
  if (list == nullptr) return nullptr;
  if (s.effect_index < 0 || s.effect_index >= static_cast<int>(list->size())) return nullptr;
  const EffectDef& outer = (*list)[static_cast<std::size_t>(s.effect_index)];
  if (s.nested_index < 0) return &outer;
  if (s.nested_index >= static_cast<int>(outer.nested.size())) return nullptr;
  return &outer.nested[static_cast<std::size_t>(s.nested_index)];
}

void execute_effect(SessionState& state, const Catalog& cat, const EffectDef& e, const std::string& event_id,
                    const std::string& choice_id, const std::string& list_name, int effect_index,
                    const std::string& planet_id, InstanceId facility_id, InstanceId source_event_instance) {
  // Faction-specific consequences are authored as data; under another faction
  // the effect simply does not apply.
  if (!e.faction.empty() && e.faction != state.political.faction_id) return;
  switch (e.kind) {
    case EffectKind::ScheduleEffect: {
      // Preserve the authored index so the saved path resolves back correctly.
      for (std::size_t n = 0; n < e.nested.size(); ++n) {
        ScheduledEffect s;
        s.id = state.allocate_id();
        s.due_day = checked_add(state.day, e.delay_days);
        s.priority = static_cast<int>(n);
        s.event_id = event_id;
        s.choice_id = choice_id;
        s.list_name = list_name;
        s.effect_index = effect_index;
        s.nested_index = static_cast<int>(n);
        s.planet_id = planet_id;
        s.facility_id = facility_id;
        s.source_event_instance = source_event_instance;
        state.scheduled.push_back(s);
      }
      sort_scheduled(state);
      return;
    }
    case EffectKind::GrantExternal:
    case EffectKind::TransferResource: {
      const int r = cat.resource_index(e.resource);
      PlanetState* p = state.find_planet(planet_id);
      if (p == nullptr) throw SimError("effect: unknown planet '" + planet_id + "'");
      if (e.kind == EffectKind::GrantExternal || e.from_account == account::kExternal) {
        Milli added = add_stock(state, *p, r, e.quantity, account::kExternal, source_event_instance,
                                e.reason.empty() ? reason::kCauseReliefGrant : e.reason.c_str());
        if (added < e.quantity) {
          emit_fact(state, "grant_partially_blocked", planet_id, source_event_instance,
                    {{"requested", e.quantity}, {"delivered", added}}, {cat.resource(r).id}, reason::kOutputStoreFull,
                    {}, "");
        }
      } else {
        Milli have = p->inventory.available(r);
        Milli take = have < e.quantity ? have : e.quantity;
        remove_stock(state, *p, r, take, e.to_account, source_event_instance,
                     e.reason.empty() ? reason::kCauseEventCost : e.reason.c_str());
      }
      return;
    }
    case EffectKind::ConsumeResource: {
      const int r = cat.resource_index(e.resource);
      PlanetState* p = state.find_planet(planet_id);
      if (p == nullptr) throw SimError("effect: unknown planet '" + planet_id + "'");
      Milli have = p->inventory.available(r);
      Milli take = have < e.quantity ? have : e.quantity;
      remove_stock(state, *p, r, take, account::kSinkEvent, source_event_instance, reason::kCauseEventCost);
      return;
    }
    case EffectKind::ApplyModifier: {
      std::vector<ActiveModifier>* list = modifier_list(state, e, planet_id, facility_id);
      if (list == nullptr) throw SimError("effect: ApplyModifier has no resolvable target");
      add_modifier(state, *list, e, state.day, event_id, source_event_instance);
      return;
    }
    case EffectKind::AdjustStabilityTarget: {
      PlanetState* p = state.find_planet(planet_id);
      if (p == nullptr) throw SimError("effect: unknown planet '" + planet_id + "'");
      EffectDef m = e;
      m.modifier_target = ModifierTarget::PlanetStabilityTarget;
      m.factor_bp = kBpOne;
      if (m.tag.empty()) m.tag = "stability_adjust";
      add_modifier(state, p->modifiers, m, state.day, event_id, source_event_instance);
      return;
    }
    case EffectKind::SetFlag: {
      state.flags[e.key] = true;
      state.flag_days[e.key] = state.day;
      return;
    }
    case EffectKind::AdjustAdherence: {
      state.political.adherence =
          static_cast<int>(clamp_i64(checked_add(state.political.adherence, e.amount), 0, 100));
      emit_fact(state, "adherence_changed", planet_id, source_event_instance, {{"delta", e.amount},
                {"adherence", state.political.adherence}}, {event_id}, event_id, {}, "");
      return;
    }
    case EffectKind::OpenEvent: {
      request_event(state, e.key, planet_id, facility_id, {});
      return;
    }
    case EffectKind::CloseEvent: {
      close_events(state, e.key.empty() ? event_id : e.key, planet_id, e.key.empty(), cat);
      return;
    }
    case EffectKind::SetConditionAtLeast: {
      FacilityState* f = state.find_facility(facility_id);
      if (f == nullptr) throw SimError("effect: SetConditionAtLeast needs a facility target");
      if (f->condition_bp < e.condition_bp) {
        f->condition_bp = e.condition_bp;
        emit_fact(state, "condition_restored", planet_id, facility_id, {{"condition_bp", f->condition_bp}}, {},
                  event_id, {}, "");
      }
      return;
    }
    case EffectKind::ClearModifiersByTag: {
      const std::string tag = e.tag.empty() ? e.key : e.tag;
      FacilityState* f = state.find_facility(facility_id);
      if (f != nullptr) {
        auto& v = f->modifiers;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const ActiveModifier& m) { return m.tag == tag; }), v.end());
      }
      PlanetState* p = state.find_planet(planet_id);
      if (p != nullptr) {
        auto& v = p->modifiers;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const ActiveModifier& m) { return m.tag == tag; }), v.end());
      }
      return;
    }
    case EffectKind::ReplaceCrews: {
      // Swap equal numbers of workers. Never adds net workforce: the outgoing
      // staff stay in transition and reach Reserve only when the swap completes.
      FacilityState* f = state.find_facility(facility_id);
      if (f == nullptr) throw SimError("effect: ReplaceCrews needs a facility target");
      PlanetState* p = state.find_planet(f->planet_id);
      if (p == nullptr) throw SimError("effect: ReplaceCrews has no planet");
      const int n = e.workers;
      if (f->assigned_workers < n) throw SimError("effect: ReplaceCrews requires enough assigned staff");
      if (workers_reserve(state, *p) < n) throw SimError("effect: ReplaceCrews requires enough Reserve workers");
      WorkerTransfer t;
      t.id = state.allocate_id();
      t.planet_id = f->planet_id;
      t.from_facility = f->id;
      t.to_facility = 0;
      t.count = n;
      t.ready_day = checked_add(state.day, e.duration_days > 0 ? e.duration_days : 2);
      f->assigned_workers -= n;      // outgoing staff stop working now
      f->assigned_workers += n;      // incoming staff take their places now
      state.transfers.push_back(t);  // outgoing staff reach Reserve on the ready day
      std::sort(state.transfers.begin(), state.transfers.end(),
                [](const WorkerTransfer& a, const WorkerTransfer& b) { return a.id < b.id; });
      // Production stops for the swap window; the reason is visible as a tag.
      EffectDef downtime;
      downtime.kind = EffectKind::ApplyModifier;
      downtime.modifier_target = ModifierTarget::FacilityThroughput;
      downtime.factor_bp = 0;
      downtime.duration_days = e.duration_days > 0 ? e.duration_days : 2;
      downtime.tag = "crew_swap";
      add_modifier(state, f->modifiers, downtime, state.day, event_id, source_event_instance);
      emit_fact(state, "crews_replaced", f->planet_id, f->id, {{"workers", n}}, {}, event_id, {}, "");
      return;
    }
  }
}

}  // namespace expansion::sim
