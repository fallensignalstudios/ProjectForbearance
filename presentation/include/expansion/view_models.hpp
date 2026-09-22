// Structured views for a graphical host (TDD 15.2, 17.3).
//
// read_models.hpp renders panels as text, which is what the command-line host
// needs. A widget host needs the same information as fields it can bind: a label
// per row, a number per bar, a typed reason per warning. These structs are that,
// and they are deliberately engine-free so they can be built and tested in the
// headless build rather than inside an editor.
//
// Everything here is derived from committed state and holds no references into
// it: a view is a value a host can keep until the next change notification.
// Every string is display text already resolved through text.hpp, except the
// `*_id` fields, which are content identity and never shown raw.
#pragma once

#include <string>
#include <vector>

#include "expansion/read_models.hpp"
#include "expansion/session.hpp"

namespace expansion::view {

// A quantity as both the exact integer the simulation holds and the string a
// host should print. A host must never re-derive the text with a float.
struct Quantity {
  std::int64_t value = 0;      // milli-units, or basis points for a ratio
  std::string text;            // "12.5", "73.5%"
};

// One resource row in a store panel.
struct StockRow {
  int resource = 0;
  std::string resource_id;
  std::string label;
  Quantity on_hand;
  Quantity reserved;
  Quantity available;
  Quantity capacity;
  Bp fill_bp = 0;              // on_hand / capacity, for a bar
  Quantity produced_today;
  Quantity consumed_today;
  std::int64_t net_today = 0;  // produced - consumed, signed milli-units
  // Days of cover at today's net drain, or -1 when stock is not falling.
  Day days_of_cover = -1;
  bool full = false;
  bool shortage_open = false;
};

// A civilian need, with the fulfilment the day actually achieved.
struct NeedRow {
  std::string need_id;         // "food" | "water" | "power" | "housing" | "clinic"
  std::string label;
  Bp fulfilment_bp = kBpOne;
  Quantity demand;
  Quantity served;
  bool met = false;
};

// A facility card. `reason` is the typed cause of the day's throughput, never a
// summary word like "efficiency" (TDD 8.4).
struct FacilityCard {
  InstanceId id = 0;
  std::string facility_id;
  std::string recipe_id;
  std::string planet_id;
  std::string label;           // "Extraction Site -- Iron"
  std::string state_label;     // "Active", "Under construction 45%", "Idle"
  FacilityLifecycle lifecycle = FacilityLifecycle::Active;
  int assigned_workers = 0;
  int required_workers = 0;
  Bp staffing_bp = 0;
  Bp condition_bp = kBpOne;
  int priority_band = 30;
  bool idle = false;
  bool servicing = false;
  bool under_construction = false;
  Bp construction_progress_bp = 0;
  Bp throughput_bp = 0;        // what it actually ran at
  Bp desired_throughput_bp = 0;
  std::string reason_id;       // typed
  std::string reason_text;     // plain language for the same id
  // The limiting factors, so a card can show the real cause rather than a guess.
  bool power_limited = false;
  bool labour_limited = false;
  std::vector<std::string> missing_inputs;    // display labels
  std::vector<std::string> blocked_outputs;   // display labels
  std::vector<std::string> modifier_tags;
  Quantity power_requested;
  Quantity power_granted;
  std::vector<StockRow> outputs;  // resource, label and today's produced quantity
};

// A planet inspector.
struct PlanetView {
  std::string planet_id;
  std::string label;
  bool colonised = false;
  People population = 0;
  int workers_total = 0;
  int workers_assigned = 0;
  int workers_reserve = 0;
  int workers_transitioning = 0;
  int workers_crew = 0;
  Bp health_bp = kBpOne;
  Bp stability_bp = 0;
  Bp fatigue_bp = 0;
  std::string policy_id;
  std::string policy_label;
  Day policy_effective_days = 0;
  bool survival_emergency = false;
  Day relief_pending_day = -1;
  Quantity power_generated;
  Quantity power_used;
  Quantity power_spare;
  Quantity port_handling_capacity;
  Quantity port_handling_used;
  std::vector<NeedRow> needs;
  std::vector<StockRow> stores;
  std::vector<InstanceId> facilities;   // cards belonging to this world
};

// The freight leg: one ship, one route (TDD 10).
struct FreightView {
  ShipPhase phase = ShipPhase::Docked;
  ShipMission mission = ShipMission::None;
  std::string phase_label;
  std::string location_label;
  std::string destination_label;
  Day departure_day = -1;
  Day arrival_day = -1;
  Day days_remaining = -1;
  Day earliest_departure_day = 0;
  Day next_scheduled_departure_day = 0;
  bool route_enabled = false;
  bool departure_authorised = false;
  Quantity fuel_in_tank;
  Quantity cargo_volume;
  std::vector<StockRow> cargo;             // what is aboard
  std::vector<StockRow> outbound_targets;  // what the route plans to carry
  std::vector<StockRow> return_targets;
  int missed_manifests = 0;
};

// One open decision. `days_remaining` is -1 for a decision with no deadline.
struct DecisionOption {
  std::string choice_id;
  std::string label;
  std::string cost_summary;   // display text for the choice's cost, empty when free
  bool affordable = true;
};

struct DecisionCard {
  InstanceId id = 0;
  std::string event_id;
  std::string planet_id;
  // The facility the decision is about, 0 for a world-scoped decision.
  InstanceId facility_id = 0;
  std::string title;
  std::string body;
  // What the decision concerns: a facility name, or the world's name.
  std::string subject_label;
  Day opened_day = 0;
  Day deadline_day = -1;
  Day days_remaining = -1;
  bool critical = false;      // holds the calendar until answered
  std::vector<DecisionOption> options;
  // The recorded facts that opened it, as sentences. Empty for a decision a
  // standing condition opened rather than an earlier event: the simulation does
  // not record a fact for the metric that qualified
  // (docs/decisions/0011-condition-trigger-provenance.md). `subject_label` and
  // `body` carry the explanation in that case.
  std::vector<std::string> because;
};

// The archive, newest first.
struct HistoryEntry {
  InstanceId id = 0;
  Day day = 0;
  std::string text;
  std::string planet_id;
  int priority = 2;
  bool is_news = true;        // false for a raw fact
};

// The top-level civilization panel: what is at risk, when, and why.
struct SectorView {
  Day day = 0;
  Revision revision = 0;
  SessionLifecycle lifecycle = SessionLifecycle::Running;
  std::string lifecycle_label;
  std::string scenario_id;
  std::string faction_id;
  std::string faction_label;
  int adherence = 0;
  Day evaluation_day = 0;
  Day days_to_evaluation = 0;
  bool paused_for_decision = false;
  std::vector<read::Concern> concerns;
  std::vector<std::string> planet_ids;
  int open_decisions = 0;
  // Mandate status, empty when none has been issued.
  bool mandate_issued = false;
  bool mandate_resolved = false;
  std::string mandate_status_label;
  Day mandate_deadline_day = -1;
};

// ---------------------------------------------------------------------------
// Builders. Each takes a committed session and returns a value.
// ---------------------------------------------------------------------------

SectorView sector(const Session& session);
PlanetView planet(const Session& session, const std::string& planet_id);
FacilityCard facility(const Session& session, InstanceId facility_id);
FreightView freight(const Session& session);
std::vector<DecisionCard> decisions(const Session& session);
std::vector<HistoryEntry> history(const Session& session, int max_entries);

}  // namespace expansion::view
