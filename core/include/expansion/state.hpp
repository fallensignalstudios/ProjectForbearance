// Authoritative mutable session state (TDD 4.2).
//
// One authoritative mutable state; views, forecasts, news and host adapters
// cannot bypass it (TDD 3.1). Every container here is either a sorted vector or
// an ordered map, so no iteration order in this file depends on hashing.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/units.hpp"

namespace expansion {

// ---------------------------------------------------------------------------
// Inventory
// ---------------------------------------------------------------------------

// Reserved stock remains part of on-hand (TDD 4.2). A reservation has one owner
// and is released or consumed exactly once (TDD 6.1).
struct StockReservation {
  InstanceId id = 0;
  int resource = 0;
  Milli quantity = 0;
  std::string owner_kind;  // "construction" | "ship_booking" | "expedition" | "mission"
  InstanceId owner_id = 0;
  std::string reason;
};

// Destination capacity promised to an inbound shipment. Separate from on-hand
// reservations (TDD 6.1).
struct IncomingClaim {
  InstanceId id = 0;
  int resource = 0;
  Milli quantity = 0;
  InstanceId owner_id = 0;
};

struct InventoryState {
  std::vector<Milli> on_hand;                     // indexed by catalog resource index
  std::vector<StockReservation> reservations;     // sorted by id
  std::vector<IncomingClaim> incoming_claims;     // sorted by id
  Milli capacity_per_resource = 0;

  void resize(int resource_count, Milli capacity);
  Milli reserved(int resource) const;
  Milli available(int resource) const;
  Milli claimed_space(int resource) const;
  // capacity - on_hand - claimed_space, never negative.
  Milli free_space(int resource) const;
};

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------

struct ActiveModifier {
  InstanceId id = 0;
  std::string tag;
  ModifierTarget target = ModifierTarget::FacilityThroughput;
  Bp factor_bp = kBpOne;
  std::int64_t amount = 0;
  Day expires_day = -1;  // -1 == until explicitly cleared
  std::string source_event_id;
  InstanceId source_event_instance = 0;
};

// ---------------------------------------------------------------------------
// Facilities
// ---------------------------------------------------------------------------

enum class FacilityLifecycle { UnderConstruction, Commissioning, Active, Cancelled };
const char* facility_lifecycle_id(FacilityLifecycle l);
std::optional<FacilityLifecycle> parse_facility_lifecycle(const std::string& s);

struct ConstructionJob {
  WorkMilli work_total = 0;
  WorkMilli work_done = 0;
  ResourceMap escrow;          // materials still held
  ResourceMap original_cost;
  ResourceMap consumed;        // cumulative consumed from escrow
  int assigned_workers = 0;
  Day started_day = 0;
  Day days_elapsed = 0;
  std::string blockage;        // reason id for the most recent stalled day
};

struct ServiceJob {
  Day started_day = 0;
  Day ready_day = 0;
};

// Per-facility production explanation for one day (TDD 8.4). Never summarise a
// labour shortfall as "efficiency" or as missing material.
struct FacilityExplanation {
  Day day = -1;
  Bp desired_throughput_bp = 0;
  Bp actual_throughput_bp = 0;
  Bp staffing_bp = 0;
  Bp health_bp = 0;
  Bp fatigue_bp = 0;
  Bp condition_bp = 0;
  Bp faction_bp = kBpOne;
  Bp policy_bp = kBpOne;
  Bp effects_bp = kBpOne;
  PowerMilli power_requested = 0;
  PowerMilli power_granted = 0;
  ResourceMap inputs_consumed;
  ResourceMap outputs_produced;
  std::vector<int> missing_inputs;     // resource indices that limited the run
  std::vector<int> blocked_outputs;    // resource indices whose store limited the run
  bool power_limited = false;
  bool labour_limited = false;
  bool idle = false;
  bool servicing = false;
  std::vector<std::string> modifier_tags;
  std::string primary_reason;          // reason id, see reasons.hpp
};

struct FacilityState {
  InstanceId id = 0;
  std::string facility_id;      // catalog FacilityDef id
  std::string recipe_id;        // the fixed recipe variant of this instance
  std::string planet_id;
  FacilityLifecycle lifecycle = FacilityLifecycle::Active;
  int assigned_workers = 0;
  Bp condition_bp = kBpOne;
  int priority_band = 30;
  bool idle = false;                   // intentionally idle but still maintained
  Day activates_day = 0;               // usable from this day onward
  int low_condition_streak = 0;        // consecutive operating days below the warning threshold
  Day last_service_completed_day = -1;
  std::optional<ConstructionJob> construction;
  std::optional<ServiceJob> service;
  std::vector<ActiveModifier> modifiers;   // sorted by id
  FacilityExplanation last_explanation;
};

// ---------------------------------------------------------------------------
// Workforce
// ---------------------------------------------------------------------------

// Workers in transition cannot work twice (TDD 4.2). facility id 0 means Reserve.
struct WorkerTransfer {
  InstanceId id = 0;
  std::string planet_id;
  InstanceId from_facility = 0;
  InstanceId to_facility = 0;
  int count = 0;
  Day ready_day = 0;
};

// ---------------------------------------------------------------------------
// Ship
// ---------------------------------------------------------------------------

enum class ShipPhase { Docked, Booked, Transit, ArrivedHolding, Unloading };
const char* ship_phase_id(ShipPhase p);
std::optional<ShipPhase> parse_ship_phase(const std::string& s);

enum class ShipMission { None, ColonyRoute, StrategicOutbound, StrategicReturn };
const char* ship_mission_id(ShipMission m);
std::optional<ShipMission> parse_ship_mission(const std::string& s);

struct ShipState {
  InstanceId id = 0;
  ShipPhase phase = ShipPhase::Docked;
  ShipMission mission = ShipMission::None;
  std::string location_planet;      // where docked / holding / unloading
  std::string origin_planet;
  std::string destination_planet;   // empty for an off-sector front
  Day departure_day = -1;
  Day arrival_day = -1;
  Day earliest_departure_day = 0;
  ResourceMap cargo;
  Milli tank = 0;
  int crew = 0;
  std::string crew_home_planet;
  // Booked but not yet loaded manifest, and the claims backing it.
  ResourceMap booked_manifest;
  std::vector<InstanceId> booking_reservations;
  std::vector<InstanceId> booking_claims;
  // Per-shipment unload progress, so a partial unload cannot be replayed
  // (TDD 10.3).
  ResourceMap unloaded_so_far;
  InstanceId shipment_id = 0;
  // Strategic mission bookkeeping.
  Day mission_delivery_day = -1;
  Day mission_return_departure_day = -1;
  bool mission_delivered = false;
};

struct RoutePlan {
  std::string id = "colony_route";
  std::string origin_planet;
  std::string destination_planet;
  ResourceMap outbound_targets;
  ResourceMap return_targets;
  std::map<int, Milli> source_floors;     // explicit per-resource overrides
  std::set<int> floor_overridden;         // resources whose default floor the player waived
  bool enabled = false;
  Day next_departure_day = 0;             // scheduled departure for missed-manifest accounting
  Day departure_interval_days = 6;
  // An explicit player authorisation, consumed by the next freight phase so the
  // departure uses that day's real port handling capacity.
  bool departure_requested = false;
  bool departure_allow_empty = false;
};

// ---------------------------------------------------------------------------
// Colonisation
// ---------------------------------------------------------------------------

struct ColonizationState {
  bool launched = false;
  bool founded = false;
  InstanceId expedition_id = 0;
  std::string target_planet;
  People residents = 0;
  People workers = 0;
  ResourceMap cargo;
  ResourceMap consumed_transit;
  Day launch_day = -1;
  Day arrival_day = -1;
  Day founded_day = -1;
};

// ---------------------------------------------------------------------------
// Politics
// ---------------------------------------------------------------------------

struct PoliticalState {
  std::string faction_id;
  int adherence = 70;                     // 0..100, sector-level
  std::set<std::string> resolved_choices; // "<event instance>:<choice id>"
  int low_adherence_streak = 0;
  Day faction_review_cooldown_until = -1;
  std::vector<ActiveModifier> modifiers;  // sector-level modifiers (e.g. oversight)
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

enum class EventResolution { Open, Resolved, Expired, Closed };
const char* event_resolution_id(EventResolution r);
std::optional<EventResolution> parse_event_resolution(const std::string& s);

struct EventInstance {
  InstanceId id = 0;
  std::string event_id;
  std::string chain_id;
  std::string planet_id;
  InstanceId facility_id = 0;
  Day opened_day = 0;
  Day deadline_day = -1;
  EventResolution resolution = EventResolution::Open;
  std::string chosen_choice;
  Day chosen_day = -1;
  std::vector<InstanceId> trigger_facts;
};

// A request to open an event, queued so the decision scheduler can order it by
// class and honour the one-new-decision-window-per-day cap (TDD 13.2).
struct EventRequest {
  InstanceId id = 0;
  std::string event_id;
  std::string planet_id;
  InstanceId facility_id = 0;
  Day requested_day = 0;
  std::vector<InstanceId> trigger_facts;
};

// A scheduled consequence. The effect is referenced by its catalog path rather
// than copied, so every active future effect provably references an existing
// definition (TDD 18.3) and the save stays exact-catalog-matched (TDD 16.3).
struct ScheduledEffect {
  InstanceId id = 0;
  Day due_day = 0;
  int priority = 0;
  std::string event_id;       // catalog event
  std::string choice_id;      // empty for on_open / on_expire
  std::string list_name;      // "choice" | "on_open" | "on_expire"
  int effect_index = 0;
  int nested_index = -1;      // index inside a ScheduleEffect payload
  std::string planet_id;
  InstanceId facility_id = 0;
  InstanceId source_event_instance = 0;
};

// ---------------------------------------------------------------------------
// Strategic demand
// ---------------------------------------------------------------------------

struct MandateMission {
  InstanceId id = 0;
  Day departure_day = 0;
  Day delivery_day = 0;
  ResourceMap manifest;
  bool delivered = false;
};

enum class MandateDecision { Pending, Honoured, Negotiated, Declined, Failed };
const char* mandate_decision_id(MandateDecision d);
std::optional<MandateDecision> parse_mandate_decision(const std::string& s);

struct DemandState {
  bool issued = false;
  Day issue_day = -1;
  Day deadline_day = -1;
  ResourceMap required;
  ResourceMap delivered;
  bool negotiated = false;
  bool resolved = false;
  MandateDecision decision = MandateDecision::Pending;
  std::vector<MandateMission> missions;
  bool freighter_committed = false;
  // A committed strategic manifest waiting for the next freight phase. It is
  // loaded atomically or not at all; P1 leaves no half-loaded mission.
  ResourceMap pending_manifest;
  bool mission_requested = false;
  Day mission_requested_day = -1;
};

// ---------------------------------------------------------------------------
// Ledger, facts and news
// ---------------------------------------------------------------------------

struct Transaction {
  InstanceId id = 0;
  Day day = 0;
  int resource = 0;
  Milli quantity = 0;          // positive magnitude; direction comes from the accounts
  std::string from_account;
  std::string to_account;
  InstanceId operation_id = 0;
  std::string cause;           // reason id
};

struct NamedValue {
  std::string key;
  std::int64_t value = 0;
};

struct FactRecord {
  InstanceId id = 0;
  Day day = 0;
  std::string kind;
  std::string planet_id;
  InstanceId entity_id = 0;
  std::vector<NamedValue> args;
  std::vector<std::string> text_args;
  std::vector<InstanceId> causal_parents;
  std::string reason_id;
  std::string dedupe_key;
};

// 0 == mandatory scenario milestone, 1 == major choice, 2 == routine.
struct NewsRecord {
  InstanceId id = 0;
  Day day = 0;
  std::string template_key;
  std::string planet_id;
  std::vector<NamedValue> args;
  std::vector<std::string> text_args;
  std::vector<InstanceId> source_facts;
  std::string dedupe_key;
  int priority = 2;
  bool detail_compacted = false;
};

// ---------------------------------------------------------------------------
// Per-day reports
// ---------------------------------------------------------------------------

struct PowerReport {
  PowerMilli generated = 0;
  PowerMilli residential_demand = 0;
  PowerMilli residential_served = 0;
  PowerMilli facility_granted = 0;
  PowerMilli construction_granted = 0;
  PowerMilli used = 0;
  PowerMilli unused = 0;
};

struct DailyReport {
  Day day = -1;
  Bp food_fulfilment_bp = kBpOne;
  Bp water_fulfilment_bp = kBpOne;
  Bp power_fulfilment_bp = kBpOne;
  Bp housing_fulfilment_bp = kBpOne;
  Bp clinic_coverage_bp = kBpOne;
  Milli food_demand = 0;
  Milli food_served = 0;
  Milli water_demand = 0;
  Milli water_served = 0;
  People housing_capacity = 0;
  People clinic_capacity = 0;
  PowerReport power;
  Bp health_target_bp = kBpOne;
  Bp stability_target_bp = 0;
  Milli maintenance_machinery_paid = 0;
  Milli maintenance_machinery_due = 0;
  std::vector<Milli> opening_stock;
  std::vector<Milli> closing_stock;
  std::vector<Milli> produced;
  std::vector<Milli> consumed;
  Milli port_handling_capacity = 0;
  Milli port_handling_used = 0;
  int workers_assigned = 0;
  int workers_transitioning = 0;
  int workers_reserve = 0;
  int workers_crew = 0;
};

struct ShortageTracker {
  int missed_days = 0;
  int fulfilled_days = 0;
  bool open = false;
};

struct PlanetState {
  std::string planet_id;
  bool colonised = false;
  People population = 0;
  People workers_total = 0;
  InventoryState inventory;
  Bp health_bp = kBpOne;
  Bp fatigue_bp = 0;
  Bp stability_bp = 7000;
  std::string policy_id;
  Day policy_started_day = -1;
  Day policy_effective_days = 0;              // affected daily steps since entry
  std::map<std::string, Day> policy_cooldown_until;
  int food_emergency_streak = 0;
  int water_emergency_streak = 0;
  int survival_emergency_days = 0;
  bool survival_emergency_active = false;
  Day survival_emergency_opened_day = -1;
  Day relief_pending_day = -1;
  bool relief_used = false;
  int ship_crew_reserved = 0;
  std::vector<ActiveModifier> modifiers;      // sorted by id
  std::map<int, ShortageTracker> shortages;   // resource index -> hysteresis state
  DailyReport last_day;
  int clean_day_streak = 0;                   // completion predicate streak
  std::map<int, Milli> milestone_high;        // resource index -> recorded production high
  std::map<int, Day> milestone_last_news_day;
};

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

struct MetricSample {
  Day day = 0;
  std::string planet_id;
  Bp health_bp = 0;
  Bp stability_bp = 0;
  Bp fatigue_bp = 0;
  Bp food_fulfilment_bp = 0;
  Bp water_fulfilment_bp = 0;
  Bp power_fulfilment_bp = 0;
  std::vector<Milli> closing_stock;
};

struct WeeklySummary {
  Day first_day = 0;
  Day last_day = 0;
  std::string planet_id;
  Bp min_health_bp = 0;
  Bp min_stability_bp = 0;
  Bp min_food_fulfilment_bp = 0;
  Bp min_water_fulfilment_bp = 0;
};

struct HistoryState {
  std::vector<MetricSample> samples;      // bounded ring, oldest first
  std::vector<WeeklySummary> weekly;
  std::vector<std::string> day_hashes;    // canonical day hash, most recent last
  Day first_retained_hash_day = 0;
  int missed_colonial_food_manifests = 0;
  std::set<std::string> milestone_flags;
};

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

enum class SessionLifecycle { Running, Complete, Compromised, Failed, Surrendered };
const char* session_lifecycle_id(SessionLifecycle l);
std::optional<SessionLifecycle> parse_session_lifecycle(const std::string& s);

struct RecordedCommandResult {
  bool accepted = false;
  Revision revision = 0;
  std::string reason;
  std::string detail;
};

struct SessionState {
  std::string simulation_version;
  std::string catalog_hash;
  std::string scenario_id;
  std::string faction_id;
  Day day = 0;
  Revision revision = 0;
  std::uint64_t command_sequence = 0;
  InstanceId next_instance_id = 1;
  // Reserved for future seeded event variants. P1 adds no random-number
  // generator (TDD 5.3); this value is saved and otherwise unused.
  std::uint64_t seed = 0;
  SessionLifecycle lifecycle = SessionLifecycle::Running;

  std::vector<PlanetState> planets;         // sorted by planet_id
  std::vector<FacilityState> facilities;    // sorted by instance id
  std::vector<WorkerTransfer> transfers;    // sorted by instance id
  ShipState ship;
  RoutePlan route;
  ColonizationState colonisation;
  PoliticalState political;
  std::vector<EventInstance> events;        // sorted by instance id
  std::vector<ScheduledEffect> scheduled;   // sorted by (due_day, priority, id)
  std::vector<EventRequest> event_requests; // sorted by id
  DemandState demand;
  HistoryState history;

  std::map<std::string, bool> flags;
  std::map<std::string, Day> flag_days;             // flag id -> day set (DaysSince)
  std::map<std::string, Day> cooldowns;             // "<event id>@<planet>" -> first available day
  std::map<std::string, int> condition_streaks;     // "<event id>@<key>" -> consecutive qualifying days
  std::map<std::string, RecordedCommandResult> applied_commands;

  std::vector<Transaction> ledger;          // bounded, most recent last
  std::vector<FactRecord> facts;
  std::vector<NewsRecord> news;
  bool paused_for_decision = false;
  int decisions_opened_today = 0;

  PlanetState* find_planet(const std::string& id);
  const PlanetState* find_planet(const std::string& id) const;
  PlanetState& planet(const std::string& id);
  const PlanetState& planet(const std::string& id) const;
  FacilityState* find_facility(InstanceId id);
  const FacilityState* find_facility(InstanceId id) const;
  FacilityState& facility(InstanceId id);
  EventInstance* find_event_instance(InstanceId id);
  InstanceId allocate_id();
};

}  // namespace expansion
