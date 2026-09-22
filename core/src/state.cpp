#include "expansion/state.hpp"

#include <algorithm>
#include <cstddef>

#include "expansion/sha256.hpp"

namespace expansion {

void extend_digest(std::string& digest, const std::string& entry) { digest = sha256_hex(digest + entry); }

namespace {

constexpr char kSep = '\x1f';

void field(std::string& out, std::int64_t v) {
  out += to_decimal_string(v);
  out.push_back(kSep);
}
void field(std::string& out, std::uint64_t v) {
  out += to_decimal_string_u(v);
  out.push_back(kSep);
}
void field(std::string& out, const std::string& v) {
  out += v;
  out.push_back(kSep);
}

}  // namespace

std::string digest_entry(const Transaction& t) {
  std::string out = "tx";
  out.push_back(kSep);
  field(out, t.id);
  field(out, t.day);
  field(out, static_cast<std::int64_t>(t.resource));
  field(out, t.quantity);
  field(out, t.from_account);
  field(out, t.to_account);
  field(out, t.operation_id);
  field(out, t.cause);
  return out;
}

std::string digest_entry(const FactRecord& f) {
  std::string out = "fact";
  out.push_back(kSep);
  field(out, f.id);
  field(out, f.day);
  field(out, f.kind);
  field(out, f.planet_id);
  field(out, f.entity_id);
  for (const auto& a : f.args) {
    field(out, a.key);
    field(out, a.value);
  }
  out.push_back(kSep);
  for (const auto& t : f.text_args) field(out, t);
  out.push_back(kSep);
  for (InstanceId p : f.causal_parents) field(out, p);
  out.push_back(kSep);
  field(out, f.reason_id);
  field(out, f.dedupe_key);
  return out;
}

std::string digest_entry(const NewsRecord& n) {
  std::string out = "news";
  out.push_back(kSep);
  field(out, n.id);
  field(out, n.day);
  field(out, n.template_key);
  field(out, n.planet_id);
  for (const auto& a : n.args) {
    field(out, a.key);
    field(out, a.value);
  }
  out.push_back(kSep);
  for (const auto& t : n.text_args) field(out, t);
  out.push_back(kSep);
  for (InstanceId p : n.source_facts) field(out, p);
  out.push_back(kSep);
  field(out, n.dedupe_key);
  field(out, static_cast<std::int64_t>(n.priority));
  return out;
}

std::string digest_entry(const MetricSample& s) {
  std::string out = "sample";
  out.push_back(kSep);
  field(out, s.day);
  field(out, s.planet_id);
  field(out, s.health_bp);
  field(out, s.stability_bp);
  field(out, s.fatigue_bp);
  field(out, s.food_fulfilment_bp);
  field(out, s.water_fulfilment_bp);
  field(out, s.power_fulfilment_bp);
  for (Milli v : s.closing_stock) field(out, v);
  return out;
}

std::string digest_entry(const WeeklySummary& w) {
  std::string out = "weekly";
  out.push_back(kSep);
  field(out, w.first_day);
  field(out, w.last_day);
  field(out, w.planet_id);
  field(out, w.min_health_bp);
  field(out, w.min_stability_bp);
  field(out, w.min_food_fulfilment_bp);
  field(out, w.min_water_fulfilment_bp);
  return out;
}

void InventoryState::resize(int resource_count, Milli capacity) {
  on_hand.assign(static_cast<std::size_t>(resource_count), 0);
  capacity_per_resource = capacity;
}

Milli InventoryState::reserved(int resource) const {
  Milli total = 0;
  for (const auto& r : reservations) {
    if (r.resource == resource) total = checked_add(total, r.quantity);
  }
  return total;
}

Milli InventoryState::available(int resource) const {
  Milli have = on_hand.at(static_cast<std::size_t>(resource));
  Milli res = reserved(resource);
  if (res > have) throw SimError("inventory: reservations exceed on-hand stock");
  return have - res;
}

Milli InventoryState::claimed_space(int resource) const {
  Milli total = 0;
  for (const auto& c : incoming_claims) {
    if (c.resource == resource) total = checked_add(total, c.quantity);
  }
  return total;
}

Milli InventoryState::free_space(int resource) const {
  Milli used = checked_add(on_hand.at(static_cast<std::size_t>(resource)), claimed_space(resource));
  Milli free = capacity_per_resource - used;
  return free < 0 ? 0 : free;
}

const char* facility_lifecycle_id(FacilityLifecycle l) {
  switch (l) {
    case FacilityLifecycle::UnderConstruction: return "under_construction";
    case FacilityLifecycle::Commissioning: return "commissioning";
    case FacilityLifecycle::Active: return "active";
    case FacilityLifecycle::Cancelled: return "cancelled";
  }
  return "active";
}

std::optional<FacilityLifecycle> parse_facility_lifecycle(const std::string& s) {
  if (s == "under_construction") return FacilityLifecycle::UnderConstruction;
  if (s == "commissioning") return FacilityLifecycle::Commissioning;
  if (s == "active") return FacilityLifecycle::Active;
  if (s == "cancelled") return FacilityLifecycle::Cancelled;
  return std::nullopt;
}

const char* ship_phase_id(ShipPhase p) {
  switch (p) {
    case ShipPhase::Docked: return "docked";
    case ShipPhase::Booked: return "booked";
    case ShipPhase::Transit: return "transit";
    case ShipPhase::ArrivedHolding: return "arrived_holding";
    case ShipPhase::Unloading: return "unloading";
  }
  return "docked";
}

std::optional<ShipPhase> parse_ship_phase(const std::string& s) {
  if (s == "docked") return ShipPhase::Docked;
  if (s == "booked") return ShipPhase::Booked;
  if (s == "transit") return ShipPhase::Transit;
  if (s == "arrived_holding") return ShipPhase::ArrivedHolding;
  if (s == "unloading") return ShipPhase::Unloading;
  return std::nullopt;
}

const char* ship_mission_id(ShipMission m) {
  switch (m) {
    case ShipMission::None: return "none";
    case ShipMission::StrategicOutbound: return "strategic_outbound";
    case ShipMission::StrategicReturn: return "strategic_return";
  }
  return "none";
}

std::optional<ShipMission> parse_ship_mission(const std::string& s) {
  if (s == "none") return ShipMission::None;
  if (s == "strategic_outbound") return ShipMission::StrategicOutbound;
  if (s == "strategic_return") return ShipMission::StrategicReturn;
  return std::nullopt;
}

const char* event_resolution_id(EventResolution r) {
  switch (r) {
    case EventResolution::Open: return "open";
    case EventResolution::Resolved: return "resolved";
    case EventResolution::Expired: return "expired";
    case EventResolution::Closed: return "closed";
  }
  return "open";
}

std::optional<EventResolution> parse_event_resolution(const std::string& s) {
  if (s == "open") return EventResolution::Open;
  if (s == "resolved") return EventResolution::Resolved;
  if (s == "expired") return EventResolution::Expired;
  if (s == "closed") return EventResolution::Closed;
  return std::nullopt;
}

const char* mandate_decision_id(MandateDecision d) {
  switch (d) {
    case MandateDecision::Pending: return "pending";
    case MandateDecision::Honoured: return "honoured";
    case MandateDecision::Negotiated: return "negotiated";
    case MandateDecision::Declined: return "declined";
    case MandateDecision::Failed: return "failed";
  }
  return "pending";
}

std::optional<MandateDecision> parse_mandate_decision(const std::string& s) {
  if (s == "pending") return MandateDecision::Pending;
  if (s == "honoured") return MandateDecision::Honoured;
  if (s == "negotiated") return MandateDecision::Negotiated;
  if (s == "declined") return MandateDecision::Declined;
  if (s == "failed") return MandateDecision::Failed;
  return std::nullopt;
}

const char* session_lifecycle_id(SessionLifecycle l) {
  switch (l) {
    case SessionLifecycle::Running: return "running";
    case SessionLifecycle::Complete: return "complete";
    case SessionLifecycle::Compromised: return "compromised";
    case SessionLifecycle::Failed: return "failed";
    case SessionLifecycle::Surrendered: return "surrendered";
  }
  return "running";
}

std::optional<SessionLifecycle> parse_session_lifecycle(const std::string& s) {
  if (s == "running") return SessionLifecycle::Running;
  if (s == "complete") return SessionLifecycle::Complete;
  if (s == "compromised") return SessionLifecycle::Compromised;
  if (s == "failed") return SessionLifecycle::Failed;
  if (s == "surrendered") return SessionLifecycle::Surrendered;
  return std::nullopt;
}

PlanetState* SessionState::find_planet(const std::string& id) {
  for (auto& p : planets) {
    if (p.planet_id == id) return &p;
  }
  return nullptr;
}

const PlanetState* SessionState::find_planet(const std::string& id) const {
  for (const auto& p : planets) {
    if (p.planet_id == id) return &p;
  }
  return nullptr;
}

PlanetState& SessionState::planet(const std::string& id) {
  PlanetState* p = find_planet(id);
  if (p == nullptr) throw SimError("state: unknown planet '" + id + "'");
  return *p;
}

const PlanetState& SessionState::planet(const std::string& id) const {
  const PlanetState* p = find_planet(id);
  if (p == nullptr) throw SimError("state: unknown planet '" + id + "'");
  return *p;
}

FacilityState* SessionState::find_facility(InstanceId id) {
  auto it = std::lower_bound(facilities.begin(), facilities.end(), id,
                             [](const FacilityState& f, InstanceId v) { return f.id < v; });
  if (it == facilities.end() || it->id != id) return nullptr;
  return &*it;
}

const FacilityState* SessionState::find_facility(InstanceId id) const {
  auto it = std::lower_bound(facilities.begin(), facilities.end(), id,
                             [](const FacilityState& f, InstanceId v) { return f.id < v; });
  if (it == facilities.end() || it->id != id) return nullptr;
  return &*it;
}

FacilityState& SessionState::facility(InstanceId id) {
  FacilityState* f = find_facility(id);
  if (f == nullptr) throw SimError("state: unknown facility instance " + to_decimal_string_u(id));
  return *f;
}

EventInstance* SessionState::find_event_instance(InstanceId id) {
  for (auto& e : events) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

InstanceId SessionState::allocate_id() {
  if (next_instance_id == 0 || next_instance_id == ~0ULL) throw SimError("state: instance id allocator exhausted");
  return next_instance_id++;
}

}  // namespace expansion
