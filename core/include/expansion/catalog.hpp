// Immutable validated content catalog (TDD 4.3).
//
// JSON definitions + scenario seed -> validated immutable catalog. Nothing in
// this file is live state: the catalog is built once, hashed, and then read-only
// for the lifetime of a session. Conditions and effects are data, never
// executable code.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "expansion/json.hpp"
#include "expansion/units.hpp"

namespace expansion {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

// Eight pooled workforce categories (TDD 7.1). Research and Military deferred.
enum class WorkerCategory { Agriculture, Mining, Industry, Energy, Logistics, Services, Administration, Reserve };
constexpr int kWorkerCategoryCount = 8;
const char* worker_category_id(WorkerCategory c);
std::optional<WorkerCategory> parse_worker_category(const std::string& s);

// World output-efficiency classes (TDD 11.4). Modifiers scale outputs only.
enum class EfficiencyClass { None, Agriculture, Mining };
const char* efficiency_class_id(EfficiencyClass c);
std::optional<EfficiencyClass> parse_efficiency_class(const std::string& s);

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

struct ResourceDef {
  std::string id;
  std::string display_key;
  // Cargo volume in milli-volume occupied by 1000 milli-units of the resource.
  Milli volume_per_unit = kMilliOne;
};

// A sparse per-resource quantity map keyed by catalog resource index.
using ResourceMap = std::map<int, Milli>;

// ---------------------------------------------------------------------------
// Recipes and facilities
// ---------------------------------------------------------------------------

struct RecipeDef {
  std::string id;
  std::string display_key;
  int staff = 0;                     // full-staff worker count
  PowerMilli power_per_day = 0;      // operating power demand at full throughput
  ResourceMap inputs;                // commodity units per day at full throughput
  ResourceMap outputs;               // commodity units per day at full throughput
  EfficiencyClass efficiency_class = EfficiencyClass::None;
  int priority_band = 30;            // TDD 8.2 default bands
  // Generators are resolved in the power phase, not the production phase.
  PowerMilli power_output_per_day = 0;
  // Service capacities, scaled by the same run factor as a commodity recipe.
  People clinic_capacity = 0;
  Milli cargo_handling_per_day = 0;  // milli cargo-volume per day
  bool is_generator() const { return power_output_per_day > 0; }
};

struct FacilityPassive {
  People housing = 0;              // independent of staffing and condition
  PowerMilli power_output = 0;     // e.g. Colony Hub solar
  int docks = 0;
};

struct FacilityDef {
  std::string id;
  std::string display_key;
  WorkerCategory category = WorkerCategory::Industry;
  std::vector<RecipeDef> recipes;   // 1, or 2 fixed variants for Extraction Site
  ResourceMap build_cost;
  WorkMilli build_work = 0;         // total milli person-days
  Day min_build_days = 2;
  Milli maintenance_machinery_per_day = 0;
  FacilityPassive passive;
  bool buildable = true;            // Colony Hub is a founding asset, not buildable
  bool one_per_world = false;
  int slots = 1;
  int recipe_index(const std::string& recipe_id) const;
};

// ---------------------------------------------------------------------------
// Planets
// ---------------------------------------------------------------------------

struct PlanetDef {
  std::string id;
  std::string display_key;
  int slot_count = 0;
  Milli resource_capacity = 0;              // per resource (TDD 6.1)
  std::map<std::string, Bp> efficiency;     // efficiency class id -> output modifier bp
  Bp efficiency_for(EfficiencyClass c) const;
};

// ---------------------------------------------------------------------------
// Factions and policies
// ---------------------------------------------------------------------------

struct FactionDef {
  std::string id;
  std::string display_key;
  Bp industry_throughput_bp = kBpOne;   // applies to Industry-category recipes
  Bp clinic_capacity_bp = kBpOne;       // applied after staffing/power limits
  int initial_adherence = 70;           // 0..100
  // A verification fixture profile with no modifiers at all. It exists so the
  // neutral one-day calibration of TDD 9.3 can be checked without a faction,
  // and a host must not offer it as a playable choice.
  bool fixture_only = false;
};

struct PolicyDef {
  std::string id;
  std::string display_key;
  bool is_default = false;
  Bp food_demand_bp = kBpOne;
  Bp mining_throughput_bp = kBpOne;
  Bp industry_throughput_bp = kBpOne;
  Bp stability_target_adjust_bp = 0;
  // Signed daily fatigue movement while active. Normal is -100 (recovery).
  Bp fatigue_delta_per_day = -100;
  Day min_days = 7;
  Day cooldown_days = 7;
  std::map<std::string, int> adherence_on_entry;  // faction id -> one-time delta
};

// ---------------------------------------------------------------------------
// Conditions (TDD 4.3 whitelist: All, Any, Not, CompareMetric, HasFlag,
// DaysSince, HasActiveEffect). Data only.
// ---------------------------------------------------------------------------

enum class CompareOp { Lt, Lte, Gt, Gte, Eq, Neq };
std::optional<CompareOp> parse_compare_op(const std::string& s);

enum class ConditionKind { All, Any, Not, CompareMetric, HasFlag, DaysSince, HasActiveEffect };

struct ConditionNode {
  ConditionKind kind = ConditionKind::All;
  std::vector<ConditionNode> children;
  std::string metric;    // CompareMetric: see metrics.hpp for the whitelist
  std::string scope;     // "planet", "sector", "facility"
  CompareOp op = CompareOp::Gte;
  std::int64_t value = 0;
  std::string key;       // HasFlag flag id, DaysSince marker id, HasActiveEffect tag
};

// ---------------------------------------------------------------------------
// Effects. The first eight kinds are the TDD 4.3 whitelist. The remaining five
// are PROPOSED P1 additions recorded in docs/decisions/0002-effect-whitelist.md;
// the accident chain cannot be authored without them.
// ---------------------------------------------------------------------------

enum class EffectKind {
  TransferResource,
  ConsumeResource,
  ApplyModifier,
  ScheduleEffect,
  SetFlag,
  AdjustAdherence,
  AdjustStabilityTarget,
  OpenEvent,
  // P1 additions:
  SetConditionAtLeast,
  ClearModifiersByTag,
  ReplaceCrews,
  CloseEvent,
  GrantExternal,
};
std::optional<EffectKind> parse_effect_kind(const std::string& s);
const char* effect_kind_id(EffectKind k);

// What an ApplyModifier changes.
enum class ModifierTarget { FacilityThroughput, PlanetStabilityTarget, PlanetIndustryThroughput, PlanetClinicCapacity };
std::optional<ModifierTarget> parse_modifier_target(const std::string& s);
const char* modifier_target_id(ModifierTarget t);

struct EffectDef {
  EffectKind kind = EffectKind::SetFlag;
  // Resolution scope for the effect's subject: "event_target" (the facility or
  // planet the event opened on), "event_planet", "sector", or an explicit id.
  std::string target = "event_target";
  std::string resource;       // Transfer/Consume/Grant
  Milli quantity = 0;
  std::string from_account;   // Transfer: "planet", "external"
  std::string to_account;
  ModifierTarget modifier_target = ModifierTarget::FacilityThroughput;
  Bp factor_bp = kBpOne;      // ApplyModifier multiplicative factor
  std::int64_t amount = 0;    // AdjustAdherence points / AdjustStabilityTarget bp
  Day duration_days = 0;      // 0 == until explicitly cleared
  Day delay_days = 0;         // ScheduleEffect
  std::string tag;            // modifier tag, e.g. "accident", "strike"
  std::string key;            // SetFlag flag id / OpenEvent event id / CloseEvent
  Bp condition_bp = 0;        // SetConditionAtLeast
  int workers = 0;            // ReplaceCrews
  std::vector<EffectDef> nested;  // ScheduleEffect payload
  std::string reason;         // ledger reason id
  // When set, the effect applies only under this faction. Keeps faction-specific
  // consequences authored as data rather than branching in code (TDD 12.4).
  std::string faction;
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// What choosing this option does to the event instance. `Resolve` closes it;
// `KeepOpen` drops the decision deadline but leaves the other remedies
// selectable, which is how Deferral works (TDD 13.2).
enum class ChoiceResolution { Resolve, KeepOpen };
std::optional<ChoiceResolution> parse_choice_resolution(const std::string& s);

struct EventChoiceDef {
  std::string id;
  std::string display_key;
  ResourceMap cost;              // paid from local available stock
  int required_reserve_workers = 0;
  int required_assigned_workers = 0;
  std::vector<EffectDef> effects;
  std::string news_template;
  ChoiceResolution resolution = ChoiceResolution::Resolve;
  // Cancels not-yet-due scheduled effects queued by these earlier choices.
  std::vector<std::string> cancels_scheduled_from;
};

// Priority classes for the decision scheduler (TDD 13.2):
// survival, existing-chain deadline, faction review, then new warning.
enum class EventQueueClass { Survival = 0, ChainDeadline = 1, FactionReview = 2, NewWarning = 3 };
std::optional<EventQueueClass> parse_queue_class(const std::string& s);

struct EventDef {
  std::string id;
  std::string display_key;
  std::string chain_id;
  std::string scope = "planet";        // "planet" or "facility"
  // For facility-scope events, restricts the trigger to one facility type.
  std::string applies_to_facility;
  EventQueueClass queue_class = EventQueueClass::NewWarning;
  bool triggered_by_system = false;    // opened by a named system, not by polling
  ConditionNode condition;
  bool has_condition = false;
  int consecutive_days = 0;            // condition must hold this many days
  // Evaluated every day while the instance is open. When it holds, the instance
  // closes through on_resolve instead of expiring (e.g. a restored condition
  // preventing an accident, TDD 13.1).
  ConditionNode condition_resolved;
  bool has_condition_resolved = false;
  Day decision_deadline_days = 0;      // 0 == no player decision window
  Day cooldown_days = 0;
  bool pauses_simulation = false;
  // At most one instance across the whole sector at a time (e.g. Faction Review).
  bool sector_unique = false;
  // On deadline expiry, keep the instance open without a deadline so its
  // remedies stay selectable.
  bool expire_keeps_open = false;
  std::vector<EventChoiceDef> choices;
  std::vector<EffectDef> on_open;
  std::vector<EffectDef> on_resolve;
  std::vector<EffectDef> on_expire;    // deadline passed with no decision
  std::string news_template_open;
  std::string news_template_resolved;
  std::string news_template_expire;
};

// ---------------------------------------------------------------------------
// Scenario
// ---------------------------------------------------------------------------

struct StartingFacility {
  std::string facility_id;
  std::string recipe_id;      // empty == first variant
  int assigned_workers = -1;  // -1 == full staff
  Bp condition_bp = kBpOne;
};

struct ScenarioPlanet {
  std::string planet_id;
  bool colonised = false;
  People population = 0;
  People workers = 0;
  ResourceMap stocks;
  std::vector<StartingFacility> facilities;
  Bp health_bp = kBpOne;
  Bp fatigue_bp = 0;
  Bp stability_bp = 7000;
};

struct ExpeditionSpec {
  std::string target_planet;
  People residents = 100;
  People workers = 50;
  ResourceMap launch_cost;        // total debited at launch
  ResourceMap cargo;              // the portion that travels
  ResourceMap transit_per_day;    // consumed from cargo each transit day
  Day transit_days = 2;
  std::string hub_facility_id = "colony_hub";
  Day launch_deadline_day = 60;
};

struct FreightSpec {
  std::string origin_planet;
  std::string destination_planet;
  Milli cargo_capacity = 120 * kMilliOne;   // milli cargo-volume
  Milli fuel_tank_capacity = 4 * kMilliOne;
  int crew = 10;
  Day leg_days = 2;
  Day min_dwell_days = 1;
  Milli round_trip_fuel = 4 * kMilliOne;        // reserved at home departure
  Milli outbound_fuel_burn = 2 * kMilliOne;
  Milli return_fuel_burn = 2 * kMilliOne;
  Milli round_trip_machinery = 100;             // 0.1 machinery
  Day default_food_reserve_days = 3;
  Day default_coal_reserve_days = 3;
};

struct MandateSpec {
  std::string id = "first_dependency_mandate";
  Day issue_day = 75;
  Day deadline_day = 100;
  ResourceMap requirement;             // fuel 180, steel 40
  ResourceMap negotiated_requirement;  // fuel 90, steel 20
  Day negotiate_before_day = 85;
  std::map<std::string, int> negotiate_adherence;
  int honour_adherence = 5;
  int fail_adherence = -10;
  Day mission_outbound_days = 5;
  Day mission_service_days = 1;
  Day mission_return_days = 4;
  Milli mission_fuel = 8 * kMilliOne;
  Milli mission_machinery = 100;
  Day last_departure_day = 95;
};

struct ReliefSpec {
  ResourceMap grant;                  // food 100, water 150
  Day delivery_delay_days = 2;
  int adherence_cost = -10;
  std::string flag = "relief_used";
};

struct CompletionSpec {
  Day evaluation_day = 120;
  Day colony_launch_by_day = 60;
  Day colony_founded_min_days_before_end = 20;
  Bp min_health_bp = 6000;
  Bp min_stability_bp = 4000;
  int final_clean_days = 10;
};

struct SurvivalSpec {
  Bp emergency_fulfilment_bp = 2500;   // below 25%
  int emergency_days = 5;
  int failure_days = 10;
};

struct ScenarioDef {
  std::string id;
  std::string display_key;
  Day start_day = 0;
  std::vector<ScenarioPlanet> planets;
  ExpeditionSpec expedition;
  FreightSpec freight;
  MandateSpec mandate;
  ReliefSpec relief;
  CompletionSpec completion;
  SurvivalSpec survival;
  std::vector<std::string> event_ids;  // events active in this scenario
};

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

struct CivilianNeeds {
  Milli food_per_resident = 100;      // 0.100 unit/resident/day
  Milli water_per_resident = 150;     // 0.150
  PowerMilli power_per_resident = 20; // 0.020 power unit/resident/day
};

struct WellbeingRules {
  Bp health_start = kBpOne;
  Bp health_down_max = 500;
  Bp health_up_max = 100;
  Bp stability_start = 7000;
  Bp stability_move_max = 100;
  Bp stability_base_bp = 2500;
  Bp stability_food_weight_bp = 2500;
  Bp stability_water_weight_bp = 2000;
  Bp stability_housing_weight_bp = 1500;
  Bp stability_health_weight_bp = 1500;
  Bp clinic_floor_bp = 8000;   // health target factor 0.8 + 0.2*coverage
  Bp clinic_span_bp = 2000;
  Bp health_factor_base_bp = 5000;
  Bp health_factor_span_bp = 5000;
  Bp fatigue_penalty_span_bp = 2500;
};

struct ConditionRules {
  Bp min_bp = 2500;
  Bp max_bp = kBpOne;
  Bp maintained_gain_per_day = 50;
  Bp unmaintained_loss_per_day = 100;
  Milli service_machinery_cost = 4 * kMilliOne;
  Day service_downtime_days = 2;
  Bp service_gain_bp = 2000;
  Bp service_max_condition_bp = 9500;
  Day service_cooldown_days = 5;
};

struct ConstructionRules {
  PowerMilli power_per_worker = 100;   // 0.1 power per assigned worker per day
  Day min_days = 2;
};

struct WorkforceRules {
  Day reserve_to_job_days = 1;
  Day job_to_job_days = 2;
};

struct NewsRules {
  int shortage_open_days = 2;
  int shortage_close_days = 3;
  Bp milestone_min_increase_bp = 1000;  // 10% above previous recorded high
  Day milestone_cooldown_days = 7;
  int max_entries = 2000;
  int max_metric_samples = 360;
};

class Catalog {
 public:
  // Loads and validates every definition file under `content_dir`. Throws
  // SimError with a specific message on any structural or semantic violation.
  static Catalog load_from_directory(const std::string& content_dir);

  int resource_count() const { return static_cast<int>(resources_.size()); }
  const ResourceDef& resource(int index) const;
  int resource_index(const std::string& id) const;   // throws when unknown
  int find_resource(const std::string& id) const;    // -1 when unknown
  const std::vector<ResourceDef>& resources() const { return resources_; }

  const FacilityDef& facility(const std::string& id) const;
  const FacilityDef* find_facility(const std::string& id) const;
  const std::vector<FacilityDef>& facilities() const { return facilities_; }

  const PlanetDef& planet(const std::string& id) const;
  const PlanetDef* find_planet(const std::string& id) const;

  const FactionDef& faction(const std::string& id) const;
  const std::vector<FactionDef>& factions() const { return factions_; }
  // The playable profiles, excluding verification fixtures.
  std::vector<const FactionDef*> playable_factions() const;
  // The neutral verification profile, or nullptr when the catalog has none.
  const FactionDef* fixture_faction() const;

  const PolicyDef& policy(const std::string& id) const;
  const PolicyDef* find_policy(const std::string& id) const;
  const std::vector<PolicyDef>& policies() const { return policies_; }
  const std::string& default_policy_id() const { return default_policy_id_; }

  const EventDef* find_event(const std::string& id) const;
  const std::vector<EventDef>& events() const { return events_; }

  const ScenarioDef& scenario(const std::string& id) const;
  const std::vector<ScenarioDef>& scenarios() const { return scenarios_; }

  const CivilianNeeds& needs() const { return needs_; }
  const WellbeingRules& wellbeing() const { return wellbeing_; }
  const ConditionRules& condition_rules() const { return condition_; }
  const ConstructionRules& construction_rules() const { return construction_; }
  const WorkforceRules& workforce_rules() const { return workforce_; }
  const NewsRules& news_rules() const { return news_; }

  // SHA-256 over the canonical serialisation of every loaded definition file
  // (TDD 16.1). An exact catalog match is required to load a save in P1.
  const std::string& hash() const { return hash_; }
  const std::string& simulation_version() const { return simulation_version_; }

  // Cached resource indices for the seven P1 commodities.
  int food() const { return food_; }
  int water() const { return water_; }
  int iron_ore() const { return iron_ore_; }
  int coal() const { return coal_; }
  int steel() const { return steel_; }
  int machinery() const { return machinery_; }
  int fuel() const { return fuel_; }

 private:
  void load_resources(const json::Value& root);
  void load_facilities(const json::Value& root);
  void load_planets(const json::Value& root);
  void load_factions(const json::Value& root);
  void load_events(const json::Value& root);
  void load_scenarios(const json::Value& root);
  void load_rules(const json::Value& root);
  void validate();

  ResourceMap parse_resource_map(const json::Value& v, const std::string& context) const;
  RecipeDef parse_recipe(const json::Value& v, const std::string& context) const;
  ConditionNode parse_condition(const json::Value& v, const std::string& context) const;
  EffectDef parse_effect(const json::Value& v, const std::string& context) const;
  std::vector<EffectDef> parse_effects(const json::Value* v, const std::string& context) const;

  std::vector<ResourceDef> resources_;
  std::map<std::string, int> resource_by_id_;
  std::vector<FacilityDef> facilities_;
  std::map<std::string, int> facility_by_id_;
  std::vector<PlanetDef> planets_;
  std::map<std::string, int> planet_by_id_;
  std::vector<FactionDef> factions_;
  std::map<std::string, int> faction_by_id_;
  std::vector<PolicyDef> policies_;
  std::map<std::string, int> policy_by_id_;
  std::string default_policy_id_;
  std::vector<EventDef> events_;
  std::map<std::string, int> event_by_id_;
  std::vector<ScenarioDef> scenarios_;
  std::map<std::string, int> scenario_by_id_;
  CivilianNeeds needs_;
  WellbeingRules wellbeing_;
  ConditionRules condition_;
  ConstructionRules construction_;
  WorkforceRules workforce_;
  NewsRules news_;
  std::string hash_;
  std::string simulation_version_ = "0.1.0";
  int food_ = -1;
  int water_ = -1;
  int iron_ore_ = -1;
  int coal_ = -1;
  int steel_ = -1;
  int machinery_ = -1;
  int fuel_ = -1;
};

// Content IDs are stable lowercase ASCII identifiers (TDD 4.1).
bool is_valid_content_id(const std::string& s);
void require_content_id(const std::string& s, const std::string& context);

}  // namespace expansion
