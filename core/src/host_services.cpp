#include "expansion/host_services.hpp"

#include <algorithm>
#include <cmath>

namespace expansion::host {

const char* sim_speed_id(SimSpeed s) {
  switch (s) {
    case SimSpeed::Paused: return "paused";
    case SimSpeed::X1: return "1x";
    case SimSpeed::X2: return "2x";
    case SimSpeed::X4: return "4x";
    case SimSpeed::X8: return "8x";
  }
  return "paused";
}

int sim_speed_multiplier(SimSpeed s) {
  switch (s) {
    case SimSpeed::Paused: return 0;
    case SimSpeed::X1: return 1;
    case SimSpeed::X2: return 2;
    case SimSpeed::X4: return 4;
    case SimSpeed::X8: return 8;
  }
  return 0;
}

void TickScheduler::set_speed(SimSpeed speed) {
  if (speed == speed_) return;
  speed_ = speed;
  // A speed change does not hand the player a partial day they did not watch.
  if (speed == SimSpeed::Paused) accumulator_us_ = 0;
}

void TickScheduler::suspend() {
  suspended_ = true;
  // Time spent suspended is not play. Dropping the partial day is what keeps a
  // focus loss from turning into catch-up debt.
  accumulator_us_ = 0;
}

void TickScheduler::resume() { suspended_ = false; }

void TickScheduler::hold_for_decision() {
  decision_hold_ = true;
  accumulator_us_ = 0;
}

void TickScheduler::release_decision_hold() { decision_hold_ = false; }

void TickScheduler::reset() { accumulator_us_ = 0; }

std::int64_t TickScheduler::microseconds_per_day() const {
  const int multiplier = sim_speed_multiplier(speed_);
  if (multiplier <= 0) return 0;
  if (!(config_.seconds_per_day_at_1x > 0.0)) return 0;
  const double at_1x = config_.seconds_per_day_at_1x * 1e6;
  // A configuration this large is nonsense, but it must not overflow the cast.
  if (!(at_1x < 9.0e18)) return 0;
  const std::int64_t per_day = static_cast<std::int64_t>(at_1x) / multiplier;
  return per_day > 0 ? per_day : 1;
}

int TickScheduler::advance(double delta_seconds) {
  if (!running()) return 0;
  if (!(delta_seconds > 0.0)) return 0;   // also rejects NaN
  const std::int64_t per_day = microseconds_per_day();
  if (per_day <= 0) return 0;

  // A very long frame is a stall, not elapsed play. Clamping before the cast
  // also keeps the conversion in range whatever the engine handed us.
  const double clamped = std::min(delta_seconds, std::max(0.0, config_.max_frame_seconds));
  // Round rather than truncate: truncation is a systematic slow bias that a
  // frame rate whose period is not a whole microsecond would accumulate all in
  // one direction. Rounding leaves an error under half a microsecond per frame
  // with no preferred sign.
  const std::int64_t delta_us = static_cast<std::int64_t>(std::llround(clamped * 1e6));
  if (delta_us <= 0) return 0;

  accumulator_us_ += delta_us;
  int steps = 0;
  const int cap = std::max(1, config_.max_steps_per_frame);
  while (accumulator_us_ >= per_day && steps < cap) {
    accumulator_us_ -= per_day;
    ++steps;
  }
  // Excess beyond this frame's cap is retained, but never more than one further
  // day's worth: a host that stalls for a minute resumes where it was, not
  // sixty days later.
  if (accumulator_us_ > per_day) accumulator_us_ = per_day;
  return steps;
}

double TickScheduler::progress_to_next_day() const {
  const std::int64_t per_day = microseconds_per_day();
  if (per_day <= 0) return 0.0;
  const double p = static_cast<double>(accumulator_us_) / static_cast<double>(per_day);
  return p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
}

// ---------------------------------------------------------------------------
// Change tracking
// ---------------------------------------------------------------------------

namespace {

// A small FNV-1a mixer. Not a checksum: it only has to separate states that a
// view would draw differently.
struct Mix {
  std::uint64_t h = 1469598103934665603ULL;
  void add(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      h ^= (v >> (i * 8)) & 0xFF;
      h *= 1099511628211ULL;
    }
  }
  void add(std::int64_t v) { add(static_cast<std::uint64_t>(v)); }
  void add(int v) { add(static_cast<std::int64_t>(v)); }
  void add(bool v) { add(static_cast<std::int64_t>(v ? 1 : 0)); }
  void add(const std::string& s) {
    for (char c : s) {
      h ^= static_cast<unsigned char>(c);
      h *= 1099511628211ULL;
    }
    h ^= 0xFF;
    h *= 1099511628211ULL;
  }
};

std::uint64_t facility_fingerprint(const FacilityState& f) {
  Mix m;
  m.add(f.facility_id);
  m.add(f.recipe_id);
  m.add(static_cast<int>(f.lifecycle));
  m.add(f.assigned_workers);
  m.add(f.condition_bp);
  m.add(f.priority_band);
  m.add(f.idle);
  m.add(f.activates_day);
  m.add(f.service.has_value());
  if (f.service.has_value()) m.add(f.service->ready_day);
  if (f.construction.has_value()) {
    m.add(f.construction->work_done);
    m.add(f.construction->assigned_workers);
    m.add(f.construction->blockage);
  }
  for (const auto& mod : f.modifiers) {
    m.add(static_cast<std::int64_t>(mod.id));
    m.add(mod.factor_bp);
    m.add(mod.expires_day);
  }
  const auto& x = f.last_explanation;
  m.add(x.day);
  m.add(x.desired_throughput_bp);
  m.add(x.actual_throughput_bp);
  m.add(x.power_granted);
  m.add(x.primary_reason);
  for (const auto& [idx, qty] : x.outputs_produced) {
    m.add(idx);
    m.add(qty);
  }
  for (const auto& [idx, qty] : x.inputs_consumed) {
    m.add(idx);
    m.add(qty);
  }
  return m.h;
}

std::uint64_t planet_fingerprint(const PlanetState& p) {
  Mix m;
  m.add(p.colonised);
  m.add(p.population);
  m.add(p.workers_total);
  m.add(p.health_bp);
  m.add(p.stability_bp);
  m.add(p.fatigue_bp);
  m.add(p.policy_id);
  m.add(p.policy_effective_days);
  m.add(p.ship_crew_reserved);
  m.add(p.survival_emergency_active);
  m.add(p.relief_pending_day);
  for (Milli v : p.inventory.on_hand) m.add(v);
  for (const auto& r : p.inventory.reservations) {
    m.add(static_cast<std::int64_t>(r.id));
    m.add(r.quantity);
  }
  const auto& d = p.last_day;
  m.add(d.day);
  m.add(d.food_fulfilment_bp);
  m.add(d.water_fulfilment_bp);
  m.add(d.power_fulfilment_bp);
  m.add(d.housing_capacity);
  m.add(d.clinic_capacity);
  m.add(d.power.generated);
  m.add(d.power.used);
  m.add(d.workers_reserve);
  m.add(d.workers_transitioning);
  return m.h;
}

std::uint64_t freight_fingerprint(const SessionState& s) {
  Mix m;
  const auto& sh = s.ship;
  m.add(static_cast<int>(sh.phase));
  m.add(static_cast<int>(sh.mission));
  m.add(sh.location_planet);
  m.add(sh.destination_planet);
  m.add(sh.departure_day);
  m.add(sh.arrival_day);
  m.add(sh.earliest_departure_day);
  m.add(sh.tank);
  for (const auto& [idx, qty] : sh.cargo) {
    m.add(idx);
    m.add(qty);
  }
  const auto& r = s.route;
  m.add(r.enabled);
  m.add(r.next_departure_day);
  m.add(r.departure_requested);
  for (const auto& [idx, qty] : r.outbound_targets) {
    m.add(idx);
    m.add(qty);
  }
  for (const auto& [idx, qty] : r.return_targets) {
    m.add(idx);
    m.add(qty);
  }
  m.add(s.demand.issued);
  m.add(s.demand.resolved);
  m.add(static_cast<int>(s.demand.decision));
  m.add(s.demand.mission_requested);
  for (const auto& [idx, qty] : s.demand.delivered) {
    m.add(idx);
    m.add(qty);
  }
  m.add(static_cast<std::int64_t>(s.demand.missions.size()));
  m.add(s.history.missed_colonial_food_manifests);
  return m.h;
}

std::uint64_t decisions_fingerprint(const SessionState& s) {
  Mix m;
  m.add(s.paused_for_decision);
  for (const auto& e : s.events) {
    if (e.resolution != EventResolution::Open) continue;
    m.add(static_cast<std::int64_t>(e.id));
    m.add(e.event_id);
    m.add(e.deadline_day);
    m.add(e.chosen_choice);
  }
  return m.h;
}

std::uint64_t history_fingerprint(const SessionState& s) {
  Mix m;
  m.add(static_cast<std::int64_t>(s.news.size()));
  m.add(static_cast<std::int64_t>(s.facts.size()));
  if (!s.news.empty()) m.add(static_cast<std::int64_t>(s.news.back().id));
  if (!s.facts.empty()) m.add(static_cast<std::int64_t>(s.facts.back().id));
  m.add(s.archive_digests.news);
  return m.h;
}

std::uint64_t sector_fingerprint(const SessionState& s) {
  Mix m;
  m.add(s.day);
  m.add(static_cast<std::int64_t>(s.revision));
  m.add(static_cast<int>(s.lifecycle));
  m.add(s.political.adherence);
  m.add(s.political.faction_id);
  m.add(static_cast<std::int64_t>(s.flags.size()));
  m.add(static_cast<std::int64_t>(s.transfers.size()));
  return m.h;
}

}  // namespace

ChangeSet ChangeTracker::publish(const Session& session) {
  const SessionState& s = session.state();
  ChangeSet out;
  out.revision = s.revision;
  out.day = s.day;
  out.first_publish = !seen_;

  auto moved = [&](std::uint64_t& stored, std::uint64_t now) {
    const bool changed = !seen_ || stored != now;
    stored = now;
    return changed;
  };
  out.sector = moved(sector_, sector_fingerprint(s));
  out.freight = moved(freight_, freight_fingerprint(s));
  out.decisions = moved(decisions_, decisions_fingerprint(s));
  out.history = moved(history_, history_fingerprint(s));

  for (const auto& p : s.planets) {
    const std::uint64_t now = planet_fingerprint(p);
    auto it = planets_.find(p.planet_id);
    if (!seen_ || it == planets_.end() || it->second != now) out.planets.push_back(p.planet_id);
    planets_[p.planet_id] = now;
  }
  std::map<InstanceId, std::uint64_t> current;
  for (const auto& f : s.facilities) {
    const std::uint64_t now = facility_fingerprint(f);
    current[f.id] = now;
    auto it = facilities_.find(f.id);
    if (!seen_ || it == facilities_.end() || it->second != now) out.facilities.push_back(f.id);
  }
  // A facility that no longer exists is a change too: its card must go.
  for (const auto& [id, unused] : facilities_) {
    (void)unused;
    if (current.count(id) == 0) out.facilities.push_back(id);
  }
  std::sort(out.facilities.begin(), out.facilities.end());
  out.facilities.erase(std::unique(out.facilities.begin(), out.facilities.end()), out.facilities.end());
  facilities_ = current;

  seen_ = true;
  return out;
}

void ChangeTracker::forget() {
  seen_ = false;
  planets_.clear();
  facilities_.clear();
}

}  // namespace expansion::host
