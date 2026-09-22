// Services a presentation host needs, written without an engine so they can be
// tested here rather than discovered inside one.
//
// Two things the design asks of a host and that are easy to get wrong:
//   * Time: a fixed-step accumulator, never elapsed wall time as an economic
//     input, with no catch-up debt after a suspend (TDD 5.3).
//   * Updates: publish on committed change only, and invalidate just the views
//     that actually changed, rather than ticking every widget (TDD 17.3, E3).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "expansion/session.hpp"

namespace expansion::host {

// ---------------------------------------------------------------------------
// Fixed-step time
// ---------------------------------------------------------------------------

enum class SimSpeed { Paused, X1, X2, X4, X8 };
const char* sim_speed_id(SimSpeed s);
int sim_speed_multiplier(SimSpeed s);

struct TickConfig {
  // At 1x, one day is due every ten seconds (TDD 2.3, 5.3).
  double seconds_per_day_at_1x = 10.0;
  // Run at most four day steps per rendered frame and retain the excess.
  int max_steps_per_frame = 4;
  // A single delta longer than this is a stall, not elapsed play: a hitch, a
  // breakpoint, a dragged window. Clamp it instead of banking days of debt.
  double max_frame_seconds = 0.5;
};

// Converts frame deltas into whole day steps. It holds no simulation state and
// makes no decisions: the host asks how many steps are due and calls StepDay
// that many times.
class TickScheduler {
 public:
  explicit TickScheduler(TickConfig config = {}) : config_(config) {}

  void set_speed(SimSpeed speed);
  SimSpeed speed() const { return speed_; }

  // Focus loss, system suspend and debugger pauses stop accumulation. Time
  // passed while suspended is discarded, never replayed as catch-up.
  void suspend();
  void resume();
  bool suspended() const { return suspended_; }

  // A critical decision holds the calendar before the next day. The host clears
  // it when the player has answered or explicitly resumed.
  void hold_for_decision();
  void release_decision_hold();
  bool held_for_decision() const { return decision_hold_; }

  // True when nothing will advance: paused, suspended, or holding a decision.
  bool running() const { return speed_ != SimSpeed::Paused && !suspended_ && !decision_hold_; }

  // How many whole days are due this frame, never more than max_steps_per_frame.
  // Any remainder stays in the accumulator for the next frame.
  int advance(double delta_seconds);

  // 0..1 through the current day, for a progress indicator.
  double progress_to_next_day() const;

  // Drops the accumulator. Used on load, on a new session, and after any jump
  // that must not inherit a partial day.
  void reset();

  double accumulated_seconds() const { return static_cast<double>(accumulator_us_) / 1e6; }

 private:
  // Microseconds per day at the current speed, or 0 when nothing advances.
  std::int64_t microseconds_per_day() const;

  TickConfig config_;
  SimSpeed speed_ = SimSpeed::Paused;
  bool suspended_ = false;
  bool decision_hold_ = false;
  // Accumulated in integer microseconds, not seconds. A float accumulator drifts
  // as frame deltas are summed and subtracted, so "ten seconds is one day" holds
  // only approximately and differs between platforms; in whole microseconds it
  // holds exactly, and a frame sequence produces the same day boundaries
  // everywhere. Sub-microsecond truncation per frame is bounded and one-signed.
  std::int64_t accumulator_us_ = 0;
};

// ---------------------------------------------------------------------------
// Event-driven updates
// ---------------------------------------------------------------------------

// Which read models a committed change invalidated. A host rebinds only these.
struct ChangeSet {
  Revision revision = 0;
  Day day = 0;
  bool first_publish = false;
  bool sector = false;     // the civilization overview
  bool freight = false;    // the network map and the freight inspector
  bool decisions = false;  // the decision drawer
  bool history = false;    // the news and fact archive
  std::vector<std::string> planets;    // planet inspectors that changed
  std::vector<InstanceId> facilities;  // facility cards that changed
  bool any() const {
    return sector || freight || decisions || history || !planets.empty() || !facilities.empty();
  }
};

// Fingerprints each read model and reports which ones moved. Cheap: it hashes
// the handful of fields a view actually shows, not the whole state, so a host
// can call it on every committed change without re-serialising anything.
class ChangeTracker {
 public:
  // The first call reports every view as changed, which is what a freshly bound
  // interface needs.
  ChangeSet publish(const Session& session);
  void forget();

 private:
  bool seen_ = false;
  std::uint64_t sector_ = 0;
  std::uint64_t freight_ = 0;
  std::uint64_t decisions_ = 0;
  std::uint64_t history_ = 0;
  std::map<std::string, std::uint64_t> planets_;
  std::map<InstanceId, std::uint64_t> facilities_;
};

}  // namespace expansion::host
