// Session creation, cloning, hashing and forecasting (TDD 19.2).
#include <algorithm>

#include "expansion/sha256.hpp"
#include "expansion/state_codec.hpp"
#include "sim_internal.hpp"

namespace expansion {

// Defined here, where DayScratch is a complete type.
Session::Session(const Catalog& catalog) : catalog_(&catalog) {}
Session::~Session() = default;

std::unique_ptr<Session> Session::create(const Catalog& catalog, const std::string& scenario_id,
                                         const std::string& faction_id, std::uint64_t seed) {
  const ScenarioDef& sc = catalog.scenario(scenario_id);
  const FactionDef& faction = catalog.faction(faction_id);

  std::unique_ptr<Session> session(new Session(catalog));
  SessionState& s = session->state_;
  s.simulation_version = catalog.simulation_version();
  s.catalog_hash = catalog.hash();
  s.scenario_id = sc.id;
  s.faction_id = faction.id;
  s.day = sc.start_day;
  s.revision = 0;
  s.seed = seed;
  s.lifecycle = SessionLifecycle::Running;
  s.political.faction_id = faction.id;
  s.political.adherence = faction.initial_adherence;

  for (const auto& sp : sc.planets) {
    const PlanetDef& pd = catalog.planet(sp.planet_id);
    PlanetState p;
    p.planet_id = sp.planet_id;
    p.colonised = sp.colonised;
    p.population = sp.population;
    p.workers_total = sp.workers;
    p.inventory.resize(catalog.resource_count(), pd.resource_capacity);
    for (const auto& [idx, qty] : sp.stocks) p.inventory.on_hand[static_cast<std::size_t>(idx)] = qty;
    p.health_bp = sp.health_bp;
    p.fatigue_bp = sp.fatigue_bp;
    p.stability_bp = sp.stability_bp;
    p.policy_id = catalog.default_policy_id();
    p.last_day.opening_stock.assign(static_cast<std::size_t>(catalog.resource_count()), 0);
    p.last_day.closing_stock = p.inventory.on_hand;
    p.last_day.produced.assign(static_cast<std::size_t>(catalog.resource_count()), 0);
    p.last_day.consumed.assign(static_cast<std::size_t>(catalog.resource_count()), 0);
    s.planets.push_back(p);
  }
  std::sort(s.planets.begin(), s.planets.end(),
            [](const PlanetState& a, const PlanetState& b) { return a.planet_id < b.planet_id; });

  for (const auto& sp : sc.planets) {
    for (const auto& sf : sp.facilities) {
      const FacilityDef& fd = catalog.facility(sf.facility_id);
      const int recipe_index = fd.recipe_index(sf.recipe_id);
      const RecipeDef& recipe = fd.recipes[static_cast<std::size_t>(recipe_index)];
      FacilityState f;
      f.id = s.allocate_id();
      f.facility_id = fd.id;
      f.recipe_id = recipe.id;
      f.planet_id = sp.planet_id;
      f.lifecycle = FacilityLifecycle::Active;
      f.assigned_workers = sf.assigned_workers < 0 ? recipe.staff : sf.assigned_workers;
      f.condition_bp = sf.condition_bp;
      f.priority_band = recipe.priority_band;
      f.activates_day = sc.start_day;
      s.facilities.push_back(f);
    }
  }
  std::sort(s.facilities.begin(), s.facilities.end(),
            [](const FacilityState& a, const FacilityState& b) { return a.id < b.id; });

  // One reusable freighter with a crew committed from its home world. The crew
  // still belongs to that population and consumes there, even while away.
  ShipState& ship = s.ship;
  ship.id = s.allocate_id();
  ship.phase = ShipPhase::Docked;
  ship.mission = ShipMission::None;
  ship.location_planet = sc.freight.origin_planet;
  ship.origin_planet = sc.freight.origin_planet;
  ship.crew = sc.freight.crew;
  ship.crew_home_planet = sc.freight.origin_planet;
  ship.earliest_departure_day = sc.start_day;
  s.planet(sc.freight.origin_planet).ship_crew_reserved = sc.freight.crew;

  s.route.id = "colony_route";
  s.route.origin_planet = sc.freight.origin_planet;
  s.route.destination_planet = sc.freight.destination_planet;
  s.route.enabled = false;
  s.route.next_departure_day = sc.start_day;
  s.route.departure_interval_days = checked_add(checked_mul(sc.freight.leg_days, 2),
                                                checked_mul(sc.freight.min_dwell_days, 2));

  sim::validate_invariants(s, catalog);
  return session;
}

std::unique_ptr<Session> Session::from_state(const Catalog& catalog, SessionState state) {
  if (state.catalog_hash != catalog.hash()) {
    throw SimError(ErrorCode::IncompatibleSave,
                   "session: the save was written against catalog " + state.catalog_hash + " but this catalog is " +
                       catalog.hash() + "; P1 requires an exact catalog match");
  }
  if (state.simulation_version != catalog.simulation_version()) {
    throw SimError(ErrorCode::IncompatibleSave,
                   "session: the save was written by simulation version " + state.simulation_version +
                       "; this build is " + catalog.simulation_version());
  }
  std::unique_ptr<Session> session(new Session(catalog));
  session->state_ = std::move(state);
  sim::validate_invariants(session->state_, catalog);
  return session;
}

std::string Session::canonical_hash() const {
  // The live payload carries a rolling digest of every archive entry ever
  // recorded, so hashing a committed day costs time proportional to the live
  // state rather than to the whole campaign.
  return sha256_hex(json::serialize_canonical(encode_live_state(state_, *catalog_)));
}

ForecastResult Session::forecast(const std::vector<Command>& candidate_commands, int days) const {
  ForecastResult result;
  result.snapshot_revision = state_.revision;
  result.from_day = state_.day;
  result.assumptions.push_back("new discretionary events are suppressed");
  result.assumptions.push_back("unknown future player choices are not predicted");
  result.assumptions.push_back("scheduled arrivals and deadlines already committed are included");
  if (days <= 0) return result;
  if (days > 30) days = 30;   // arrival-aware forecasts clone state for up to 30 days

  // Runs on a clone: the live state, its hash and its event scheduling are
  // untouched, and no random generator is advanced.
  Session clone(*catalog_);
  clone.state_ = state_;
  clone.forecast_mode_ = true;
  for (const auto& c : candidate_commands) clone.apply_command(c, std::nullopt);

  for (int i = 0; i < days; ++i) {
    if (clone.state_.lifecycle != SessionLifecycle::Running) break;
    clone.step_day();
    result.days_run += 1;
    if (result.first_missed_need_day < 0) {
      for (const auto& p : clone.state_.planets) {
        if (!p.colonised) continue;
        const char* which = nullptr;
        if (p.last_day.food_fulfilment_bp < kBpOne) {
          which = "food";
        } else if (p.last_day.water_fulfilment_bp < kBpOne) {
          which = "water";
        } else if (p.last_day.power_fulfilment_bp < kBpOne) {
          which = "residential_power";
        }
        if (which != nullptr) {
          result.first_missed_need_day = clone.state_.day;
          result.first_missed_planet = p.planet_id;
          result.first_missed_resource = which;
          break;
        }
      }
    }
  }
  for (const auto& p : clone.state_.planets) result.closing_stock[p.planet_id] = p.inventory.on_hand;
  return result;
}

}  // namespace expansion
