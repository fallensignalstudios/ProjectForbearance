// The authoritative session: command application and the daily resolver.
// Core API contract per TDD 19.2.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/commands.hpp"
#include "expansion/state.hpp"

namespace expansion {

struct DayResult {
  Day day = 0;
  std::string day_hash;
  std::vector<InstanceId> new_facts;
  std::vector<InstanceId> new_news;
  bool paused_for_decision = false;
  SessionLifecycle lifecycle = SessionLifecycle::Running;
};

struct ForecastResult {
  Revision snapshot_revision = 0;
  Day from_day = 0;
  int days_run = 0;
  // First day on which any civilian need is missed, or -1 when none within the
  // horizon. Unknown future player choices are not predicted.
  Day first_missed_need_day = -1;
  std::string first_missed_planet;
  std::string first_missed_resource;
  // Projected closing stock per planet per resource at the end of the horizon.
  std::map<std::string, std::vector<Milli>> closing_stock;
  bool suppressed_discretionary_events = true;
  std::vector<std::string> assumptions;
};

class Session {
 public:
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // CreateSession(catalog, scenario, faction, seed) -> Session | Errors
  static std::unique_ptr<Session> create(const Catalog& catalog, const std::string& scenario_id,
                                         const std::string& faction_id, std::uint64_t seed);

  // ApplyCommand(session, command, expected_revision) -> CommandResult.
  // A mismatch against `expected_revision` rejects without touching state.
  CommandResult apply_command(const Command& command, std::optional<Revision> expected_revision = std::nullopt);

  // StepDay(session) -> DayResult. Produces day D+1 through the ten phases of
  // TDD 5.2 and commits only at a complete boundary.
  DayResult step_day();

  const SessionState& state() const { return state_; }
  const Catalog& catalog() const { return *catalog_; }

  // Canonical hash of the economic state, excluding presentation-only fields
  // (TDD 16.3).
  std::string canonical_hash() const;

  // Forecast(snapshot, candidate_commands, days). Runs on a clone: it changes
  // neither live state nor event scheduling, and suppresses new discretionary
  // events (TDD 6.3, 18.2 T24).
  ForecastResult forecast(const std::vector<Command>& candidate_commands, int days) const;

  // Restores a validated state. Used by the save codec and by replay.
  static std::unique_ptr<Session> from_state(const Catalog& catalog, SessionState state);

  // Diagnostics: accepted/rejected command counts by reason (TDD 17.2).
  const std::map<std::string, std::int64_t>& command_reason_counts() const { return reason_counts_; }

  // True while the resolver is running inside a forecast clone.
  bool is_forecast() const { return forecast_mode_; }

 private:
  explicit Session(const Catalog& catalog);

  // --- daily phases (TDD 5.2) ---
  void phase_due_work();
  void phase_maintenance();
  void phase_opening_snapshot();
  void phase_power();
  void phase_production();
  void phase_commit_and_needs();
  void phase_construction();
  void phase_freight();
  void phase_consequences();
  void phase_commit();

  // --- command handlers ---
  CommandResult do_assign_workers(const Command& c);
  CommandResult do_set_priority(const Command& c);
  CommandResult do_set_idle(const Command& c);
  CommandResult do_start_construction(const Command& c);
  CommandResult do_cancel_construction(const Command& c);
  CommandResult do_service_facility(const Command& c);
  CommandResult do_select_policy(const Command& c);
  CommandResult do_update_route(const Command& c);
  CommandResult do_authorise_departure(const Command& c);
  CommandResult do_launch_colonization(const Command& c);
  CommandResult do_resolve_event(const Command& c);
  CommandResult do_respond_to_demand(const Command& c);
  CommandResult do_launch_strategic_mission(const Command& c);
  CommandResult do_request_relief(const Command& c);
  CommandResult do_surrender(const Command& c);

  // --- freight and colonisation helpers ---
  void found_colony();
  void credit_strategic_delivery();
  // Loads and departs the freighter on its colony route. `scheduled` marks the
  // automatic departure driven by the route plan rather than by a command.
  CommandResult depart_colony_leg(bool allow_empty, bool scheduled);
  void unload_arrivals();
  void depart_strategic_mission();

  // --- consequence helpers ---
  void update_shortages();
  void update_survival(PlanetState& planet);
  void update_events();
  void update_mandate();
  void update_political();
  void update_milestones();
  void evaluate_completion();

  // --- helpers ---
  friend struct SimAccess;
  const Catalog* catalog_;
  SessionState state_;
  bool forecast_mode_ = false;
  std::map<std::string, std::int64_t> reason_counts_;

  // Per-day working set, rebuilt every StepDay and never persisted.
  struct DayScratch;
  std::unique_ptr<DayScratch> scratch_;
  std::vector<InstanceId> day_facts_;
  std::vector<InstanceId> day_news_;
};

}  // namespace expansion
