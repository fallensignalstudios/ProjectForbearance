// Internal helpers shared by the daily-resolver translation units. Not part of
// the public core API.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/commands.hpp"
#include "expansion/metrics.hpp"
#include "expansion/reasons.hpp"
#include "expansion/session.hpp"
#include "expansion/state.hpp"

namespace expansion {

// Per-day working set. Rebuilt by every StepDay and never persisted: nothing
// here may be the only record of something the player must be able to inspect.
struct Session::DayScratch {
  struct PlanetScratch {
    std::vector<Milli> opening_stock;
    std::vector<Milli> available_input;   // opening available stock, decremented as consumed
    std::vector<Milli> staged_output;     // committed at phase 6, never an input today
    std::vector<Milli> produced;
    std::vector<Milli> consumed;
    PowerMilli power_pool = 0;            // unallocated power after residential reservation
    PowerReport power;
    Bp prior_health_bp = kBpOne;
    Bp prior_fatigue_bp = 0;
    People housing_capacity = 0;
    People clinic_capacity = 0;
    Milli port_handling_capacity = 0;
    Milli port_handling_remaining = 0;
    int docks = 0;
    Milli maintenance_due = 0;
    Milli maintenance_paid = 0;
    std::map<InstanceId, int> frozen_workers;
    std::map<InstanceId, Bp> frozen_condition;
  };
  std::map<std::string, PlanetScratch> planets;
  PlanetScratch& at(const std::string& id) {
    auto it = planets.find(id);
    if (it == planets.end()) throw SimError("day scratch: unknown planet '" + id + "'");
    return it->second;
  }
};

namespace sim {

std::string planet_account(const std::string& planet_id);
std::string escrow_account(InstanceId facility_id);
std::string ship_account(InstanceId ship_id);
std::string expedition_account(InstanceId expedition_id);
bool is_owned_account(const std::string& name);

// Records a transaction. Every inventory change goes through one of the stock
// helpers below so the ledger cannot drift from the balances (TDD 6.2).
void record(SessionState& state, int resource, Milli quantity, const std::string& from_account,
            const std::string& to_account, InstanceId operation_id, const char* cause);

// Adds stock, refusing to exceed capacity. Returns the quantity actually added;
// overflow blocks rather than deleting cargo (TDD 6.1).
Milli add_stock(SessionState& state, PlanetState& planet, int resource, Milli quantity,
                const std::string& from_account, InstanceId op, const char* cause);
// Removes unreserved stock. Throws when the caller asked for more than available.
void remove_stock(SessionState& state, PlanetState& planet, int resource, Milli quantity,
                  const std::string& to_account, InstanceId op, const char* cause);

InstanceId reserve_stock(SessionState& state, PlanetState& planet, int resource, Milli quantity,
                         const std::string& owner_kind, InstanceId owner_id, const char* reason);
void release_reservation(PlanetState& planet, InstanceId reservation_id);
void consume_reservation(SessionState& state, PlanetState& planet, InstanceId reservation_id,
                         const std::string& to_account, InstanceId op, const char* cause);
InstanceId claim_space(SessionState& state, PlanetState& planet, int resource, Milli quantity, InstanceId owner_id);
void release_claim(PlanetState& planet, InstanceId claim_id);

// --- workforce -------------------------------------------------------------
int workers_assigned(const SessionState& state, const std::string& planet_id);
int workers_transitioning(const SessionState& state, const std::string& planet_id);
int workers_reserve(const SessionState& state, const PlanetState& planet);

// --- facility maths --------------------------------------------------------
const RecipeDef& recipe_of(const Catalog& cat, const FacilityState& f);
const FacilityDef& def_of(const Catalog& cat, const FacilityState& f);
bool is_operable(const FacilityState& f, Day day);

Bp staffing_bp(const RecipeDef& r, int assigned);
Bp health_factor_bp(const Catalog& cat, Bp health_bp);
Bp fatigue_factor_bp(const Catalog& cat, Bp fatigue_bp);
// Product of every multiplicative factor, quantized down to one basis point at
// each step, in the order fixed by TDD 8.2, and capped at 20000 bp.
Bp desired_throughput_bp(const Catalog& cat, const SessionState& state, const PlanetState& planet,
                         const FacilityState& f, int assigned, Bp condition, Bp health, Bp fatigue,
                         FacilityExplanation* out);
Bp modifier_product_bp(const std::vector<ActiveModifier>& mods, ModifierTarget target, Day day,
                       std::vector<std::string>* tags_out);
std::int64_t modifier_sum(const std::vector<ActiveModifier>& mods, ModifierTarget target, Day day);

// Facilities on a planet in resolution order: priority band ascending, then
// stable instance id ascending (TDD 8.2).
std::vector<InstanceId> production_order(const SessionState& state, const std::string& planet_id, bool generators);

// --- events / effects ------------------------------------------------------
bool evaluate_condition(const SessionState& state, const Catalog& cat, const ConditionNode& node,
                        const MetricContext& ctx);
// Executes one authored effect now. Scheduled payloads are queued, not run.
void execute_effect(SessionState& state, const Catalog& cat, const EffectDef& effect, const std::string& event_id,
                    const std::string& choice_id, const std::string& list_name, int effect_index,
                    const std::string& planet_id, InstanceId facility_id, InstanceId source_event_instance);

// Queues an event-opening request for the decision scheduler.
void request_event(SessionState& state, const std::string& event_id, const std::string& planet_id,
                   InstanceId facility_id, std::vector<InstanceId> trigger_facts);
// Opens an event immediately, runs its on_open effects and emits its news.
InstanceId open_event(SessionState& state, const Catalog& cat, const std::string& event_id,
                      const std::string& planet_id, InstanceId facility_id, std::vector<InstanceId> trigger_facts);
// Closes every open instance of `event_id` (or of the whole chain when
// `whole_chain`) on the given planet.
void close_events(SessionState& state, const std::string& event_id, const std::string& planet_id, bool whole_chain,
                  const Catalog& cat);
bool chain_open_on_planet(const SessionState& state, const Catalog& cat, const std::string& chain_id,
                          const std::string& planet_id);
void queue_effect_list(SessionState& state, const std::vector<EffectDef>& effects, const std::string& event_id,
                       const std::string& choice_id, const std::string& list_name, const std::string& planet_id,
                       InstanceId facility_id, InstanceId source_event_instance, Day base_day);

const EffectDef* resolve_scheduled_effect(const Catalog& cat, const ScheduledEffect& s);

// --- facts and news --------------------------------------------------------
InstanceId emit_fact(SessionState& state, const std::string& kind, const std::string& planet_id, InstanceId entity,
                     std::vector<NamedValue> args, std::vector<std::string> text_args, const std::string& reason_id,
                     std::vector<InstanceId> parents, const std::string& dedupe_key);
// Emits news only when no entry with the same deduplication key already exists.
InstanceId emit_news(SessionState& state, const Catalog& cat, const std::string& template_key,
                     const std::string& planet_id, std::vector<NamedValue> args, std::vector<std::string> text_args,
                     std::vector<InstanceId> source_facts, const std::string& dedupe_key, int priority);

// --- derived read helpers --------------------------------------------------
People housing_capacity(const SessionState& state, const Catalog& cat, const std::string& planet_id);
int used_slots(const SessionState& state, const std::string& planet_id);
bool has_operable_spaceport(const SessionState& state, const Catalog& cat, const std::string& planet_id, Day day);
Milli civilian_food_demand(const SessionState& state, const Catalog& cat, const PlanetState& p);
Milli civilian_water_demand(const Catalog& cat, const PlanetState& p);
PowerMilli residential_power_demand(const Catalog& cat, const PlanetState& p);
// Default outbound reserve floor: three days of civilian food demand, or three
// days of thermal-generator coal demand (TDD 6.3).
Milli default_reserve_floor(const SessionState& state, const Catalog& cat, const PlanetState& p, int resource);
Milli cargo_volume(const Catalog& cat, const ResourceMap& cargo);

void validate_invariants(const SessionState& state, const Catalog& cat);

}  // namespace sim
}  // namespace expansion
