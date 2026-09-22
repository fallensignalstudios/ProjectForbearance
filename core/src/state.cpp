#include "expansion/state.hpp"

#include <algorithm>

namespace expansion {

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
    case ShipMission::ColonyRoute: return "colony_route";
    case ShipMission::StrategicOutbound: return "strategic_outbound";
    case ShipMission::StrategicReturn: return "strategic_return";
  }
  return "none";
}

std::optional<ShipMission> parse_ship_mission(const std::string& s) {
  if (s == "none") return ShipMission::None;
  if (s == "colony_route") return ShipMission::ColonyRoute;
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
