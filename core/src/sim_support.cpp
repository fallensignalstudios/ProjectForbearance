#include <algorithm>

#include "expansion/derived.hpp"
#include "expansion/metrics.hpp"
#include "sim_internal.hpp"

namespace expansion::sim {

std::string planet_account(const std::string& planet_id) { return "planet:" + planet_id; }

// Sector-owned accounts. Owned quantity includes planet stocks, construction
// escrow, ship cargo, fuel tanks and expedition cargo (TDD 6.2).
bool is_owned_account(const std::string& name) {
  static const char* kPrefixes[] = {"planet:", "escrow:", "ship:", "expedition:"};
  for (const char* prefix : kPrefixes) {
    const std::size_t n = std::string(prefix).size();
    if (name.size() > n && name.compare(0, n, prefix) == 0) return true;
  }
  return false;
}
std::string escrow_account(InstanceId facility_id) { return "escrow:" + to_decimal_string_u(facility_id); }
std::string ship_account(InstanceId ship_id) { return "ship:" + to_decimal_string_u(ship_id); }
std::string expedition_account(InstanceId id) { return "expedition:" + to_decimal_string_u(id); }

namespace {
constexpr std::size_t kLedgerCap = 10000;
}

void record(SessionState& state, int resource, Milli quantity, const std::string& from_account,
            const std::string& to_account, InstanceId operation_id, const char* cause) {
  if (quantity == 0) return;
  Transaction t;
  t.id = state.allocate_id();
  t.day = state.day;
  t.resource = resource;
  t.quantity = quantity;
  t.from_account = from_account;
  t.to_account = to_account;
  t.operation_id = operation_id;
  t.cause = cause;
  extend_digest(state.archive_digests.ledger, digest_entry(t));
  state.ledger.push_back(t);
  if (state.ledger.size() > kLedgerCap) {
    state.ledger.erase(state.ledger.begin(), state.ledger.begin() + static_cast<long>(state.ledger.size() - kLedgerCap));
  }
}

Milli add_stock(SessionState& state, PlanetState& planet, int resource, Milli quantity,
                const std::string& from_account, InstanceId op, const char* cause) {
  if (quantity < 0) throw SimError("add_stock: negative quantity");
  if (quantity == 0) return 0;
  Milli room = planet.inventory.capacity_per_resource - planet.inventory.on_hand.at(static_cast<std::size_t>(resource));
  if (room < 0) room = 0;
  Milli added = quantity < room ? quantity : room;
  if (added == 0) return 0;
  planet.inventory.on_hand[static_cast<std::size_t>(resource)] =
      checked_add(planet.inventory.on_hand[static_cast<std::size_t>(resource)], added);
  record(state, resource, added, from_account, planet_account(planet.planet_id), op, cause);
  return added;
}

void remove_stock(SessionState& state, PlanetState& planet, int resource, Milli quantity,
                  const std::string& to_account, InstanceId op, const char* cause) {
  if (quantity < 0) throw SimError("remove_stock: negative quantity");
  if (quantity == 0) return;
  if (planet.inventory.available(resource) < quantity) {
    throw SimError("remove_stock: not enough unreserved stock of '" + std::to_string(resource) + "' on " +
                   planet.planet_id);
  }
  planet.inventory.on_hand[static_cast<std::size_t>(resource)] =
      checked_sub(planet.inventory.on_hand[static_cast<std::size_t>(resource)], quantity);
  record(state, resource, quantity, planet_account(planet.planet_id), to_account, op, cause);
}

InstanceId reserve_stock(SessionState& state, PlanetState& planet, int resource, Milli quantity,
                         const std::string& owner_kind, InstanceId owner_id, const char* reason) {
  if (quantity <= 0) throw SimError("reserve_stock: quantity must be positive");
  if (planet.inventory.available(resource) < quantity) {
    throw SimError("reserve_stock: insufficient available stock");
  }
  StockReservation r;
  r.id = state.allocate_id();
  r.resource = resource;
  r.quantity = quantity;
  r.owner_kind = owner_kind;
  r.owner_id = owner_id;
  r.reason = reason;
  planet.inventory.reservations.push_back(r);
  std::sort(planet.inventory.reservations.begin(), planet.inventory.reservations.end(),
            [](const StockReservation& a, const StockReservation& b) { return a.id < b.id; });
  return r.id;
}

void release_reservation(PlanetState& planet, InstanceId reservation_id) {
  auto& v = planet.inventory.reservations;
  auto it = std::find_if(v.begin(), v.end(), [&](const StockReservation& r) { return r.id == reservation_id; });
  if (it == v.end()) throw SimError("release_reservation: unknown reservation");
  v.erase(it);
}

void consume_reservation(SessionState& state, PlanetState& planet, InstanceId reservation_id,
                         const std::string& to_account, InstanceId op, const char* cause) {
  auto& v = planet.inventory.reservations;
  auto it = std::find_if(v.begin(), v.end(), [&](const StockReservation& r) { return r.id == reservation_id; });
  if (it == v.end()) throw SimError("consume_reservation: unknown reservation");
  const int resource = it->resource;
  const Milli qty = it->quantity;
  if (planet.inventory.on_hand.at(static_cast<std::size_t>(resource)) < qty) {
    throw SimError("consume_reservation: on-hand below the reserved quantity");
  }
  v.erase(it);
  planet.inventory.on_hand[static_cast<std::size_t>(resource)] =
      checked_sub(planet.inventory.on_hand[static_cast<std::size_t>(resource)], qty);
  record(state, resource, qty, planet_account(planet.planet_id), to_account, op, cause);
}

InstanceId claim_space(SessionState& state, PlanetState& planet, int resource, Milli quantity, InstanceId owner_id) {
  if (quantity <= 0) throw SimError("claim_space: quantity must be positive");
  if (planet.inventory.free_space(resource) < quantity) throw SimError("claim_space: insufficient free space");
  IncomingClaim c;
  c.id = state.allocate_id();
  c.resource = resource;
  c.quantity = quantity;
  c.owner_id = owner_id;
  planet.inventory.incoming_claims.push_back(c);
  std::sort(planet.inventory.incoming_claims.begin(), planet.inventory.incoming_claims.end(),
            [](const IncomingClaim& a, const IncomingClaim& b) { return a.id < b.id; });
  return c.id;
}

void release_claim(PlanetState& planet, InstanceId claim_id) {
  auto& v = planet.inventory.incoming_claims;
  auto it = std::find_if(v.begin(), v.end(), [&](const IncomingClaim& c) { return c.id == claim_id; });
  if (it == v.end()) throw SimError("release_claim: unknown claim");
  v.erase(it);
}

// ---------------------------------------------------------------------------
// Workforce
// ---------------------------------------------------------------------------

int workers_assigned(const SessionState& state, const std::string& planet_id) {
  int total = 0;
  for (const auto& f : state.facilities) {
    if (f.planet_id != planet_id) continue;
    if (f.lifecycle == FacilityLifecycle::Cancelled) continue;
    total += f.assigned_workers;
    if (f.construction.has_value()) total += f.construction->assigned_workers;
  }
  return total;
}

int workers_transitioning(const SessionState& state, const std::string& planet_id) {
  int total = 0;
  for (const auto& t : state.transfers) {
    if (t.planet_id == planet_id) total += t.count;
  }
  return total;
}

int workers_reserve(const SessionState& state, const PlanetState& planet) {
  std::int64_t r = planet.workers_total;
  r -= workers_assigned(state, planet.planet_id);
  r -= workers_transitioning(state, planet.planet_id);
  r -= planet.ship_crew_reserved;
  if (r < 0) throw SimError("workforce: more workers are committed than exist on " + planet.planet_id);
  return static_cast<int>(r);
}

// ---------------------------------------------------------------------------
// Facility maths
// ---------------------------------------------------------------------------

const FacilityDef& def_of(const Catalog& cat, const FacilityState& f) { return cat.facility(f.facility_id); }

const RecipeDef& recipe_of(const Catalog& cat, const FacilityState& f) {
  const FacilityDef& d = cat.facility(f.facility_id);
  int idx = d.recipe_index(f.recipe_id);
  if (idx < 0) throw SimError("facility " + to_decimal_string_u(f.id) + ": unknown recipe '" + f.recipe_id + "'");
  return d.recipes[static_cast<std::size_t>(idx)];
}

bool is_operable(const FacilityState& f, Day day) {
  if (f.lifecycle != FacilityLifecycle::Active) return false;
  if (day < f.activates_day) return false;
  return true;
}

Bp staffing_bp(const RecipeDef& r, int assigned) {
  if (r.staff <= 0) return kBpOne;
  if (assigned <= 0) return 0;
  if (assigned > r.staff) throw SimError("staffing: assigned workers exceed the recipe's full staff");
  return mul_div_floor(assigned, kBpOne, r.staff);
}

Bp health_factor_bp(const Catalog& cat, Bp health_bp) {
  const auto& w = cat.wellbeing();
  return checked_add(w.health_factor_base_bp, mul_div_floor(health_bp, w.health_factor_span_bp, kBpOne));
}

Bp fatigue_factor_bp(const Catalog& cat, Bp fatigue_bp) {
  const auto& w = cat.wellbeing();
  return checked_sub(kBpOne, mul_div_floor(fatigue_bp, w.fatigue_penalty_span_bp, kBpOne));
}

Bp modifier_product_bp(const std::vector<ActiveModifier>& mods, ModifierTarget target, Day day,
                       std::vector<std::string>* tags_out) {
  Bp acc = kBpOne;
  for (const auto& m : mods) {  // sorted by id: stable effect-id order
    if (m.target != target) continue;
    if (m.expires_day >= 0 && day > m.expires_day) continue;
    acc = bp_mul(acc, m.factor_bp);
    if (tags_out != nullptr) tags_out->push_back(m.tag);
  }
  return acc;
}

std::int64_t modifier_sum(const std::vector<ActiveModifier>& mods, ModifierTarget target, Day day) {
  std::int64_t acc = 0;
  for (const auto& m : mods) {
    if (m.target != target) continue;
    if (m.expires_day >= 0 && day > m.expires_day) continue;
    acc = checked_add(acc, m.amount);
  }
  return acc;
}

Bp desired_throughput_bp(const Catalog& cat, const SessionState& state, const PlanetState& planet,
                         const FacilityState& f, int assigned, Bp condition, Bp health, Bp fatigue,
                         FacilityExplanation* out) {
  const FacilityDef& fd = def_of(cat, f);
  const RecipeDef& r = recipe_of(cat, f);
  const Bp staff_bp = staffing_bp(r, assigned);
  const Bp health_bp = health_factor_bp(cat, health);
  const Bp fatigue_bp = fatigue_factor_bp(cat, fatigue);

  // Faction and policy throughput modifiers, by workforce category (TDD 12.2-12.3).
  const FactionDef& faction = cat.faction(state.political.faction_id);
  Bp faction_bp = kBpOne;
  if (fd.category == WorkerCategory::Industry) faction_bp = faction.industry_throughput_bp;

  Bp policy_bp = kBpOne;
  const PolicyDef* policy = cat.find_policy(planet.policy_id);
  if (policy != nullptr) {
    if (fd.category == WorkerCategory::Industry) policy_bp = bp_mul(policy_bp, policy->industry_throughput_bp);
    if (fd.category == WorkerCategory::Mining) policy_bp = bp_mul(policy_bp, policy->mining_throughput_bp);
  }
  // A sector-level oversight modifier applies to Industry throughput.
  if (fd.category == WorkerCategory::Industry) {
    policy_bp = bp_mul(policy_bp, modifier_product_bp(state.political.modifiers,
                                                      ModifierTarget::PlanetIndustryThroughput, state.day, nullptr));
    policy_bp = bp_mul(policy_bp, modifier_product_bp(planet.modifiers, ModifierTarget::PlanetIndustryThroughput,
                                                      state.day, nullptr));
  }

  std::vector<std::string> tags;
  const Bp effects_bp = modifier_product_bp(f.modifiers, ModifierTarget::FacilityThroughput, state.day, &tags);

  // Exact order from TDD 8.2, quantized down at every step.
  Bp acc = staff_bp;
  acc = bp_mul(acc, health_bp);
  acc = bp_mul(acc, fatigue_bp);
  acc = bp_mul(acc, condition);
  acc = bp_mul(acc, faction_bp);
  acc = bp_mul(acc, policy_bp);
  acc = bp_mul(acc, effects_bp);
  if (acc > kThroughputCapBp) acc = kThroughputCapBp;

  if (out != nullptr) {
    out->staffing_bp = staff_bp;
    out->health_bp = health_bp;
    out->fatigue_bp = fatigue_bp;
    out->condition_bp = condition;
    out->faction_bp = faction_bp;
    out->policy_bp = policy_bp;
    out->effects_bp = effects_bp;
    out->modifier_tags = tags;
    out->desired_throughput_bp = acc;
  }
  return acc;
}

std::vector<InstanceId> production_order(const SessionState& state, const std::string& planet_id, bool generators) {
  struct Entry {
    int band;
    InstanceId id;
  };
  std::vector<Entry> entries;
  for (const auto& f : state.facilities) {
    if (f.planet_id != planet_id) continue;
    if (f.lifecycle != FacilityLifecycle::Active) continue;
    entries.push_back({f.priority_band, f.id});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.band != b.band) return a.band < b.band;
    return a.id < b.id;
  });
  std::vector<InstanceId> out;
  out.reserve(entries.size());
  for (const auto& e : entries) out.push_back(e.id);
  (void)generators;
  return out;
}

// ---------------------------------------------------------------------------
// Facts and news
// ---------------------------------------------------------------------------

InstanceId emit_fact(SessionState& state, const std::string& kind, const std::string& planet_id, InstanceId entity,
                     std::vector<NamedValue> args, std::vector<std::string> text_args, const std::string& reason_id,
                     std::vector<InstanceId> parents, const std::string& dedupe_key) {
  FactRecord f;
  f.id = state.allocate_id();
  f.day = state.day;
  f.kind = kind;
  f.planet_id = planet_id;
  f.entity_id = entity;
  f.args = std::move(args);
  f.text_args = std::move(text_args);
  f.reason_id = reason_id;
  f.causal_parents = std::move(parents);
  f.dedupe_key = dedupe_key;
  extend_digest(state.archive_digests.facts, digest_entry(f));
  state.facts.push_back(f);
  return f.id;
}

InstanceId emit_news(SessionState& state, const Catalog& cat, const std::string& template_key,
                     const std::string& planet_id, std::vector<NamedValue> args, std::vector<std::string> text_args,
                     std::vector<InstanceId> source_facts, const std::string& dedupe_key, int priority) {
  if (!dedupe_key.empty()) {
    for (const auto& n : state.news) {
      if (n.dedupe_key == dedupe_key) return 0;
    }
  }
  NewsRecord n;
  n.id = state.allocate_id();
  n.day = state.day;
  n.template_key = template_key;
  n.planet_id = planet_id;
  n.args = std::move(args);
  n.text_args = std::move(text_args);
  n.source_facts = std::move(source_facts);
  n.dedupe_key = dedupe_key;
  n.priority = priority;
  extend_digest(state.archive_digests.news, digest_entry(n));
  state.news.push_back(n);

  // Bounded archive. Routine entries go first, then major choices; a mandatory
  // scenario milestone is never pruned. Survivors are marked compacted so a causal
  // link points at a summary rather than dangling (TDD 17.2).
  const int cap = cat.news_rules().max_entries;
  if (cap > 0 && static_cast<int>(state.news.size()) > cap) {
    for (int tier = 2; tier >= 1; --tier) {
      auto oldest = state.news.end();
      for (auto it = state.news.begin(); it != state.news.end(); ++it) {
        if (it->priority != tier) continue;
        if (oldest == state.news.end() || it->day < oldest->day) oldest = it;
      }
      if (oldest == state.news.end()) continue;
      state.news.erase(oldest);
      for (auto& survivor : state.news) survivor.detail_compacted = true;
      break;
    }
  }
  return n.id;
}

// ---------------------------------------------------------------------------
// Derived reads
// ---------------------------------------------------------------------------

People housing_capacity(const SessionState& state, const Catalog& cat, const std::string& planet_id) {
  People total = 0;
  for (const auto& f : state.facilities) {
    if (f.planet_id != planet_id) continue;
    if (f.lifecycle != FacilityLifecycle::Active) continue;
    // Passive housing remains available while condition is below maximum and
    // while the facility is idle or being serviced (TDD 9.1, 8.2).
    total = checked_add(total, def_of(cat, f).passive.housing);
  }
  return total;
}

int used_slots(const SessionState& state, const std::string& planet_id) {
  int total = 0;
  for (const auto& f : state.facilities) {
    if (f.planet_id != planet_id) continue;
    if (f.lifecycle == FacilityLifecycle::Cancelled) continue;
    total += 1;
  }
  return total;
}

bool has_operable_spaceport(const SessionState& state, const Catalog& cat, const std::string& planet_id, Day day) {
  for (const auto& f : state.facilities) {
    if (f.planet_id != planet_id) continue;
    if (!is_operable(f, day)) continue;
    if (def_of(cat, f).passive.docks > 0) return true;
  }
  return false;
}

Milli civilian_food_demand(const SessionState& state, const Catalog& cat, const PlanetState& p) {
  Milli base = checked_mul(p.population, cat.needs().food_per_resident);
  const PolicyDef* policy = cat.find_policy(p.policy_id);
  Bp ratio = policy == nullptr ? kBpOne : policy->food_demand_bp;
  (void)state;
  return mul_div_floor(base, ratio, kBpOne);
}

Milli civilian_water_demand(const Catalog& cat, const PlanetState& p) {
  return checked_mul(p.population, cat.needs().water_per_resident);
}

PowerMilli residential_power_demand(const Catalog& cat, const PlanetState& p) {
  return checked_mul(p.population, cat.needs().power_per_resident);
}

Milli default_reserve_floor(const SessionState& state, const Catalog& cat, const PlanetState& p, int resource) {
  if (resource == cat.food()) {
    return checked_mul(civilian_food_demand(state, cat, p), 3);
  }
  if (resource == cat.coal()) {
    Milli per_day = 0;
    for (const auto& f : state.facilities) {
      if (f.planet_id != p.planet_id) continue;
      if (f.lifecycle != FacilityLifecycle::Active) continue;
      const RecipeDef& r = recipe_of(cat, f);
      if (!r.is_generator()) continue;
      auto it = r.inputs.find(cat.coal());
      if (it != r.inputs.end()) per_day = checked_add(per_day, it->second);
    }
    return checked_mul(per_day, 3);
  }
  return 0;
}

Milli cargo_volume(const Catalog& cat, const ResourceMap& cargo) {
  Milli total = 0;
  for (const auto& [idx, qty] : cargo) {
    total = checked_add(total, mul_div_ceil(qty, cat.resource(idx).volume_per_unit, kMilliOne));
  }
  return total;
}

}  // namespace expansion::sim

namespace expansion {

Milli cargo_volume_of(const Catalog& catalog, const ResourceMap& cargo) { return sim::cargo_volume(catalog, cargo); }

Milli default_outbound_floor(const SessionState& state, const Catalog& catalog, const PlanetState& planet,
                             int resource) {
  return sim::default_reserve_floor(state, catalog, planet, resource);
}

bool is_sector_owned_account(const std::string& account) { return sim::is_owned_account(account); }

People housing_capacity_of(const SessionState& state, const Catalog& catalog, const std::string& planet_id) {
  return sim::housing_capacity(state, catalog, planet_id);
}

int reserve_workers_of(const SessionState& state, const PlanetState& planet) {
  return sim::workers_reserve(state, planet);
}

void validate_state(const SessionState& state, const Catalog& catalog) { sim::validate_invariants(state, catalog); }

}  // namespace expansion
