#include "expansion/catalog.hpp"

#include <algorithm>
#include <functional>
#include <set>

#include "expansion/metrics.hpp"
#include "expansion/sha256.hpp"

namespace expansion {

namespace {
const char* const kWorkerCategoryIds[kWorkerCategoryCount] = {
    "agriculture", "mining", "industry", "energy", "logistics", "services", "administration", "reserve"};
}  // namespace

const char* worker_category_id(WorkerCategory c) { return kWorkerCategoryIds[static_cast<int>(c)]; }

std::optional<WorkerCategory> parse_worker_category(const std::string& s) {
  for (int i = 0; i < kWorkerCategoryCount; ++i) {
    if (s == kWorkerCategoryIds[i]) return static_cast<WorkerCategory>(i);
  }
  return std::nullopt;
}

const char* efficiency_class_id(EfficiencyClass c) {
  switch (c) {
    case EfficiencyClass::Agriculture: return "agriculture";
    case EfficiencyClass::Mining: return "mining";
    default: return "none";
  }
}

std::optional<EfficiencyClass> parse_efficiency_class(const std::string& s) {
  if (s == "none") return EfficiencyClass::None;
  if (s == "agriculture") return EfficiencyClass::Agriculture;
  if (s == "mining") return EfficiencyClass::Mining;
  return std::nullopt;
}

std::optional<CompareOp> parse_compare_op(const std::string& s) {
  if (s == "lt") return CompareOp::Lt;
  if (s == "lte") return CompareOp::Lte;
  if (s == "gt") return CompareOp::Gt;
  if (s == "gte") return CompareOp::Gte;
  if (s == "eq") return CompareOp::Eq;
  if (s == "neq") return CompareOp::Neq;
  return std::nullopt;
}

std::optional<EventQueueClass> parse_queue_class(const std::string& s) {
  if (s == "survival") return EventQueueClass::Survival;
  if (s == "chain_deadline") return EventQueueClass::ChainDeadline;
  if (s == "faction_review") return EventQueueClass::FactionReview;
  if (s == "new_warning") return EventQueueClass::NewWarning;
  return std::nullopt;
}

namespace {
struct EffectName {
  const char* id;
  EffectKind kind;
};
const EffectName kEffectNames[] = {
    {"TransferResource", EffectKind::TransferResource},
    {"ConsumeResource", EffectKind::ConsumeResource},
    {"ApplyModifier", EffectKind::ApplyModifier},
    {"ScheduleEffect", EffectKind::ScheduleEffect},
    {"SetFlag", EffectKind::SetFlag},
    {"AdjustAdherence", EffectKind::AdjustAdherence},
    {"AdjustStabilityTarget", EffectKind::AdjustStabilityTarget},
    {"OpenEvent", EffectKind::OpenEvent},
    {"SetConditionAtLeast", EffectKind::SetConditionAtLeast},
    {"ClearModifiersByTag", EffectKind::ClearModifiersByTag},
    {"ReplaceCrews", EffectKind::ReplaceCrews},
    {"CloseEvent", EffectKind::CloseEvent},
    {"GrantExternal", EffectKind::GrantExternal},
};
}  // namespace

std::optional<EffectKind> parse_effect_kind(const std::string& s) {
  for (const auto& e : kEffectNames) {
    if (s == e.id) return e.kind;
  }
  return std::nullopt;
}

const char* effect_kind_id(EffectKind k) {
  for (const auto& e : kEffectNames) {
    if (e.kind == k) return e.id;
  }
  return "Unknown";
}

std::optional<ChoiceResolution> parse_choice_resolution(const std::string& s) {
  if (s == "resolve") return ChoiceResolution::Resolve;
  if (s == "keep_open") return ChoiceResolution::KeepOpen;
  return std::nullopt;
}

std::optional<ModifierTarget> parse_modifier_target(const std::string& s) {
  if (s == "facility_throughput") return ModifierTarget::FacilityThroughput;
  if (s == "planet_stability_target") return ModifierTarget::PlanetStabilityTarget;
  if (s == "planet_industry_throughput") return ModifierTarget::PlanetIndustryThroughput;
  if (s == "planet_clinic_capacity") return ModifierTarget::PlanetClinicCapacity;
  return std::nullopt;
}

const char* modifier_target_id(ModifierTarget t) {
  switch (t) {
    case ModifierTarget::FacilityThroughput: return "facility_throughput";
    case ModifierTarget::PlanetStabilityTarget: return "planet_stability_target";
    case ModifierTarget::PlanetIndustryThroughput: return "planet_industry_throughput";
    case ModifierTarget::PlanetClinicCapacity: return "planet_clinic_capacity";
  }
  return "facility_throughput";
}

bool is_valid_content_id(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  if (s.front() == '_' || s.back() == '_') return false;
  for (char c : s) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

void require_content_id(const std::string& s, const std::string& context) {
  if (!is_valid_content_id(s)) {
    throw SimError("catalog: " + context + ": '" + s +
                   "' is not a valid content id (lowercase ASCII letters, digits and underscore)");
  }
}

int FacilityDef::recipe_index(const std::string& recipe_id) const {
  for (std::size_t i = 0; i < recipes.size(); ++i) {
    if (recipes[i].id == recipe_id) return static_cast<int>(i);
  }
  return -1;
}

Bp PlanetDef::efficiency_for(EfficiencyClass c) const {
  if (c == EfficiencyClass::None) return kBpOne;
  auto it = efficiency.find(efficiency_class_id(c));
  return it == efficiency.end() ? kBpOne : it->second;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const ResourceDef& Catalog::resource(int index) const {
  if (index < 0 || index >= static_cast<int>(resources_.size())) throw SimError("catalog: resource index out of range");
  return resources_[static_cast<std::size_t>(index)];
}

int Catalog::find_resource(const std::string& id) const {
  auto it = resource_by_id_.find(id);
  return it == resource_by_id_.end() ? -1 : it->second;
}

int Catalog::resource_index(const std::string& id) const {
  int i = find_resource(id);
  if (i < 0) throw SimError("catalog: unknown resource id '" + id + "'");
  return i;
}

const FacilityDef* Catalog::find_facility(const std::string& id) const {
  auto it = facility_by_id_.find(id);
  return it == facility_by_id_.end() ? nullptr : &facilities_[static_cast<std::size_t>(it->second)];
}

const FacilityDef& Catalog::facility(const std::string& id) const {
  const FacilityDef* f = find_facility(id);
  if (f == nullptr) throw SimError("catalog: unknown facility id '" + id + "'");
  return *f;
}

const PlanetDef* Catalog::find_planet(const std::string& id) const {
  auto it = planet_by_id_.find(id);
  return it == planet_by_id_.end() ? nullptr : &planets_[static_cast<std::size_t>(it->second)];
}

const PlanetDef& Catalog::planet(const std::string& id) const {
  const PlanetDef* p = find_planet(id);
  if (p == nullptr) throw SimError("catalog: unknown planet id '" + id + "'");
  return *p;
}

const FactionDef& Catalog::faction(const std::string& id) const {
  auto it = faction_by_id_.find(id);
  if (it == faction_by_id_.end()) throw SimError("catalog: unknown faction id '" + id + "'");
  return factions_[static_cast<std::size_t>(it->second)];
}

const PolicyDef* Catalog::find_policy(const std::string& id) const {
  auto it = policy_by_id_.find(id);
  return it == policy_by_id_.end() ? nullptr : &policies_[static_cast<std::size_t>(it->second)];
}

const PolicyDef& Catalog::policy(const std::string& id) const {
  const PolicyDef* p = find_policy(id);
  if (p == nullptr) throw SimError("catalog: unknown policy id '" + id + "'");
  return *p;
}

const EventDef* Catalog::find_event(const std::string& id) const {
  auto it = event_by_id_.find(id);
  return it == event_by_id_.end() ? nullptr : &events_[static_cast<std::size_t>(it->second)];
}

std::vector<const FactionDef*> Catalog::playable_factions() const {
  std::vector<const FactionDef*> out;
  for (const auto& f : factions_) {
    if (!f.fixture_only) out.push_back(&f);
  }
  return out;
}

const FactionDef* Catalog::fixture_faction() const {
  for (const auto& f : factions_) {
    if (f.fixture_only) return &f;
  }
  return nullptr;
}

const ScenarioDef& Catalog::scenario(const std::string& id) const {
  auto it = scenario_by_id_.find(id);
  if (it == scenario_by_id_.end()) throw SimError("catalog: unknown scenario id '" + id + "'");
  return scenarios_[static_cast<std::size_t>(it->second)];
}

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

ResourceMap Catalog::parse_resource_map(const json::Value& v, const std::string& context) const {
  ResourceMap out;
  if (!v.is_object()) throw SimError("catalog: " + context + ": expected a resource map object");
  for (const auto& [key, val] : v.as_object()) {
    int idx = find_resource(key);
    if (idx < 0) throw SimError("catalog: " + context + ": unknown resource '" + key + "'");
    if (!val.is_int()) throw SimError("catalog: " + context + ": resource '" + key + "' must be an integer");
    if (val.as_int() < 0) throw SimError("catalog: " + context + ": resource '" + key + "' must be nonnegative");
    if (val.as_int() == 0) continue;
    out[idx] = val.as_int();
  }
  return out;
}

RecipeDef Catalog::parse_recipe(const json::Value& v, const std::string& context) const {
  v.reject_unknown_keys({"id", "display_key", "staff", "power_per_day", "inputs", "outputs", "efficiency_class",
                         "priority_band", "power_output_per_day", "clinic_capacity", "cargo_handling_per_day"},
                        context);
  RecipeDef r;
  r.id = v.require_string("id", context);
  require_content_id(r.id, context + ".id");
  r.display_key = v.string_or("display_key", "recipe." + r.id);
  r.staff = static_cast<int>(v.int_or("staff", 0));
  if (r.staff < 0) throw SimError("catalog: " + context + ": staff must be nonnegative");
  r.power_per_day = v.int_or("power_per_day", 0);
  if (const json::Value* in = v.find("inputs")) r.inputs = parse_resource_map(*in, context + ".inputs");
  if (const json::Value* out = v.find("outputs")) r.outputs = parse_resource_map(*out, context + ".outputs");
  std::string ec = v.string_or("efficiency_class", "none");
  auto parsed_ec = parse_efficiency_class(ec);
  if (!parsed_ec) throw SimError("catalog: " + context + ": unknown efficiency_class '" + ec + "'");
  r.efficiency_class = *parsed_ec;
  r.priority_band = static_cast<int>(v.int_or("priority_band", 30));
  r.power_output_per_day = v.int_or("power_output_per_day", 0);
  r.clinic_capacity = v.int_or("clinic_capacity", 0);
  r.cargo_handling_per_day = v.int_or("cargo_handling_per_day", 0);
  if (r.power_output_per_day > 0 && r.power_per_day > 0) {
    throw SimError("catalog: " + context + ": a generator recipe cannot also consume operating power in P1");
  }
  return r;
}

ConditionNode Catalog::parse_condition(const json::Value& v, const std::string& context) const {
  if (!v.is_object()) throw SimError("catalog: " + context + ": expected a condition object");
  const std::string kind = v.require_string("kind", context);
  ConditionNode n;
  if (kind == "All" || kind == "Any" || kind == "Not") {
    n.kind = kind == "All" ? ConditionKind::All : (kind == "Any" ? ConditionKind::Any : ConditionKind::Not);
    v.reject_unknown_keys({"kind", "children"}, context);
    const auto& kids = v.require_array("children", context);
    if (kind == "Not" && kids.size() != 1) throw SimError("catalog: " + context + ": Not takes exactly one child");
    if (kids.empty()) throw SimError("catalog: " + context + ": " + kind + " requires at least one child");
    for (std::size_t i = 0; i < kids.size(); ++i) {
      n.children.push_back(parse_condition(kids[i], context + ".children[" + std::to_string(i) + "]"));
    }
    return n;
  }
  if (kind == "CompareMetric") {
    n.kind = ConditionKind::CompareMetric;
    v.reject_unknown_keys({"kind", "metric", "scope", "op", "value"}, context);
    n.metric = v.require_string("metric", context);
    n.scope = v.string_or("scope", "planet");
    auto op = parse_compare_op(v.require_string("op", context));
    if (!op) throw SimError("catalog: " + context + ": unknown compare op");
    n.op = *op;
    n.value = v.require_int("value", context);
    return n;
  }
  if (kind == "HasFlag") {
    n.kind = ConditionKind::HasFlag;
    v.reject_unknown_keys({"kind", "key", "scope"}, context);
    n.key = v.require_string("key", context);
    n.scope = v.string_or("scope", "sector");
    return n;
  }
  if (kind == "DaysSince") {
    n.kind = ConditionKind::DaysSince;
    v.reject_unknown_keys({"kind", "key", "op", "value", "scope"}, context);
    n.key = v.require_string("key", context);
    auto op = parse_compare_op(v.require_string("op", context));
    if (!op) throw SimError("catalog: " + context + ": unknown compare op");
    n.op = *op;
    n.value = v.require_int("value", context);
    n.scope = v.string_or("scope", "planet");
    return n;
  }
  if (kind == "HasActiveEffect") {
    n.kind = ConditionKind::HasActiveEffect;
    v.reject_unknown_keys({"kind", "key", "scope"}, context);
    n.key = v.require_string("key", context);
    n.scope = v.string_or("scope", "facility");
    return n;
  }
  throw SimError("catalog: " + context + ": condition kind '" + kind +
                 "' is not in the P1 whitelist (All, Any, Not, CompareMetric, HasFlag, DaysSince, HasActiveEffect)");
}

EffectDef Catalog::parse_effect(const json::Value& v, const std::string& context) const {
  if (!v.is_object()) throw SimError("catalog: " + context + ": expected an effect object");
  const std::string kind_text = v.require_string("kind", context);
  auto kind = parse_effect_kind(kind_text);
  if (!kind) throw SimError("catalog: " + context + ": effect kind '" + kind_text + "' is not in the whitelist");
  v.reject_unknown_keys({"kind", "target", "resource", "quantity", "from_account", "to_account", "modifier_target",
                         "factor_bp", "amount", "duration_days", "delay_days", "tag", "key", "condition_bp",
                         "workers", "effects", "reason", "faction"},
                        context);
  EffectDef e;
  e.kind = *kind;
  e.target = v.string_or("target", "event_target");
  e.resource = v.string_or("resource", "");
  e.quantity = v.int_or("quantity", 0);
  e.from_account = v.string_or("from_account", "planet");
  e.to_account = v.string_or("to_account", "sink");
  if (const json::Value* mt = v.find("modifier_target")) {
    auto parsed = parse_modifier_target(mt->as_string());
    if (!parsed) throw SimError("catalog: " + context + ": unknown modifier_target");
    e.modifier_target = *parsed;
  }
  e.factor_bp = v.int_or("factor_bp", kBpOne);
  e.amount = v.int_or("amount", 0);
  e.duration_days = v.int_or("duration_days", 0);
  e.delay_days = v.int_or("delay_days", 0);
  e.tag = v.string_or("tag", "");
  e.key = v.string_or("key", "");
  e.condition_bp = v.int_or("condition_bp", 0);
  e.workers = static_cast<int>(v.int_or("workers", 0));
  e.reason = v.string_or("reason", "");
  e.faction = v.string_or("faction", "");
  if (const json::Value* nested = v.find("effects")) {
    for (std::size_t i = 0; i < nested->as_array().size(); ++i) {
      e.nested.push_back(parse_effect(nested->as_array()[i], context + ".effects[" + std::to_string(i) + "]"));
    }
  }
  // Per-kind required fields. Each effect validates its target, bounds, and the
  // source of any created resources (TDD 4.3).
  switch (e.kind) {
    case EffectKind::TransferResource:
    case EffectKind::ConsumeResource:
    case EffectKind::GrantExternal:
      if (e.resource.empty()) throw SimError("catalog: " + context + ": effect requires 'resource'");
      if (find_resource(e.resource) < 0) throw SimError("catalog: " + context + ": unknown resource '" + e.resource + "'");
      if (e.quantity <= 0) throw SimError("catalog: " + context + ": effect requires a positive 'quantity'");
      break;
    case EffectKind::ApplyModifier:
      if (e.duration_days < 0) throw SimError("catalog: " + context + ": duration_days must be nonnegative");
      if (e.factor_bp < 0) throw SimError("catalog: " + context + ": factor_bp must be nonnegative");
      if (e.tag.empty()) throw SimError("catalog: " + context + ": ApplyModifier requires a 'tag'");
      break;
    case EffectKind::ScheduleEffect:
      if (e.delay_days <= 0) throw SimError("catalog: " + context + ": ScheduleEffect requires a positive delay_days");
      if (e.nested.empty()) throw SimError("catalog: " + context + ": ScheduleEffect requires nested 'effects'");
      break;
    case EffectKind::SetFlag:
    case EffectKind::OpenEvent:
    case EffectKind::CloseEvent:
    case EffectKind::ClearModifiersByTag:
      if (e.key.empty() && e.tag.empty()) throw SimError("catalog: " + context + ": effect requires 'key' or 'tag'");
      break;
    case EffectKind::SetConditionAtLeast:
      if (e.condition_bp <= 0 || e.condition_bp > kBpOne) {
        throw SimError("catalog: " + context + ": condition_bp must be in (0, 10000]");
      }
      break;
    case EffectKind::ReplaceCrews:
      if (e.workers <= 0) throw SimError("catalog: " + context + ": ReplaceCrews requires positive 'workers'");
      break;
    case EffectKind::AdjustAdherence:
    case EffectKind::AdjustStabilityTarget:
      break;
  }
  return e;
}

std::vector<EffectDef> Catalog::parse_effects(const json::Value* v, const std::string& context) const {
  std::vector<EffectDef> out;
  if (v == nullptr) return out;
  if (!v->is_array()) throw SimError("catalog: " + context + ": expected an array of effects");
  for (std::size_t i = 0; i < v->as_array().size(); ++i) {
    out.push_back(parse_effect(v->as_array()[i], context + "[" + std::to_string(i) + "]"));
  }
  return out;
}

// ---------------------------------------------------------------------------
// File loaders
// ---------------------------------------------------------------------------

void Catalog::load_resources(const json::Value& root) {
  for (const auto& r : root.require_array("resources", "resources")) {
    r.reject_unknown_keys({"id", "display_key", "volume_per_unit"}, "resources.resource");
    ResourceDef def;
    def.id = r.require_string("id", "resources.resource");
    require_content_id(def.id, "resources.resource.id");
    if (def.id == "power") {
      throw SimError("catalog: 'power' must not be a stored commodity; it is local operating capacity (TDD 6)");
    }
    def.display_key = r.string_or("display_key", "resource." + def.id);
    def.volume_per_unit = r.int_or("volume_per_unit", kMilliOne);
    if (def.volume_per_unit < 0) throw SimError("catalog: resource '" + def.id + "': volume_per_unit must be nonnegative");
    if (resource_by_id_.count(def.id) != 0) throw SimError("catalog: duplicate resource id '" + def.id + "'");
    resource_by_id_[def.id] = static_cast<int>(resources_.size());
    resources_.push_back(def);
  }
}

void Catalog::load_facilities(const json::Value& root) {
  for (const auto& f : root.require_array("facilities", "facilities")) {
    f.reject_unknown_keys({"id", "display_key", "category", "recipes", "build_cost", "build_work", "min_build_days",
                           "maintenance_machinery_per_day", "passive", "buildable", "one_per_world", "slots"},
                          "facilities.facility");
    FacilityDef def;
    def.id = f.require_string("id", "facilities.facility");
    require_content_id(def.id, "facilities.facility.id");
    const std::string ctx = "facility '" + def.id + "'";
    def.display_key = f.string_or("display_key", "facility." + def.id);
    const std::string cat = f.require_string("category", ctx);
    auto parsed_cat = parse_worker_category(cat);
    if (!parsed_cat) throw SimError("catalog: " + ctx + ": unknown workforce category '" + cat + "'");
    if (*parsed_cat == WorkerCategory::Reserve) {
      throw SimError("catalog: " + ctx + ": Reserve is not an assignable facility category");
    }
    def.category = *parsed_cat;
    const auto& recipes = f.require_array("recipes", ctx);
    if (recipes.empty()) throw SimError("catalog: " + ctx + ": at least one recipe is required");
    if (recipes.size() > 2) throw SimError("catalog: " + ctx + ": P1 allows at most two fixed recipe variants");
    for (std::size_t i = 0; i < recipes.size(); ++i) {
      def.recipes.push_back(parse_recipe(recipes[i], ctx + ".recipes[" + std::to_string(i) + "]"));
    }
    if (const json::Value* bc = f.find("build_cost")) def.build_cost = parse_resource_map(*bc, ctx + ".build_cost");
    def.build_work = f.int_or("build_work", 0);
    if (def.build_work < 0) throw SimError("catalog: " + ctx + ": build_work must be nonnegative");
    def.min_build_days = f.int_or("min_build_days", 2);
    if (def.min_build_days < 1) throw SimError("catalog: " + ctx + ": min_build_days must be at least 1");
    def.maintenance_machinery_per_day = f.int_or("maintenance_machinery_per_day", 0);
    if (def.maintenance_machinery_per_day < 0) throw SimError("catalog: " + ctx + ": maintenance must be nonnegative");
    if (const json::Value* p = f.find("passive")) {
      p->reject_unknown_keys({"housing", "power_output", "docks"}, ctx + ".passive");
      def.passive.housing = p->int_or("housing", 0);
      def.passive.power_output = p->int_or("power_output", 0);
      def.passive.docks = static_cast<int>(p->int_or("docks", 0));
    }
    def.buildable = f.bool_or("buildable", true);
    def.one_per_world = f.bool_or("one_per_world", false);
    def.slots = static_cast<int>(f.int_or("slots", 1));
    if (def.slots < 0) throw SimError("catalog: " + ctx + ": slots must be nonnegative");
    if (facility_by_id_.count(def.id) != 0) throw SimError("catalog: duplicate facility id '" + def.id + "'");
    facility_by_id_[def.id] = static_cast<int>(facilities_.size());
    facilities_.push_back(def);
  }
}

void Catalog::load_planets(const json::Value& root) {
  for (const auto& p : root.require_array("planets", "planets")) {
    p.reject_unknown_keys({"id", "display_key", "slot_count", "resource_capacity", "efficiency"}, "planets.planet");
    PlanetDef def;
    def.id = p.require_string("id", "planets.planet");
    require_content_id(def.id, "planets.planet.id");
    def.display_key = p.string_or("display_key", "planet." + def.id);
    def.slot_count = static_cast<int>(p.require_int("slot_count", "planet '" + def.id + "'"));
    if (def.slot_count < 0) throw SimError("catalog: planet '" + def.id + "': slot_count must be nonnegative");
    def.resource_capacity = p.require_int("resource_capacity", "planet '" + def.id + "'");
    if (def.resource_capacity < 0) throw SimError("catalog: planet '" + def.id + "': resource_capacity negative");
    if (const json::Value* eff = p.find("efficiency")) {
      for (const auto& [key, val] : eff->as_object()) {
        if (!parse_efficiency_class(key)) {
          throw SimError("catalog: planet '" + def.id + "': unknown efficiency class '" + key + "'");
        }
        if (!val.is_int() || val.as_int() < 0) {
          throw SimError("catalog: planet '" + def.id + "': efficiency '" + key + "' must be nonnegative bp");
        }
        def.efficiency[key] = val.as_int();
      }
    }
    if (planet_by_id_.count(def.id) != 0) throw SimError("catalog: duplicate planet id '" + def.id + "'");
    planet_by_id_[def.id] = static_cast<int>(planets_.size());
    planets_.push_back(def);
  }
}

void Catalog::load_factions(const json::Value& root) {
  if (const json::Value* fs = root.find("factions")) {
    for (const auto& f : fs->as_array()) {
      f.reject_unknown_keys({"id", "display_key", "industry_throughput_bp", "clinic_capacity_bp",
                             "initial_adherence", "fixture_only"},
                            "factions.faction");
      FactionDef def;
      def.id = f.require_string("id", "factions.faction");
      require_content_id(def.id, "factions.faction.id");
      def.display_key = f.string_or("display_key", "faction." + def.id);
      def.industry_throughput_bp = f.int_or("industry_throughput_bp", kBpOne);
      def.clinic_capacity_bp = f.int_or("clinic_capacity_bp", kBpOne);
      def.initial_adherence = static_cast<int>(f.int_or("initial_adherence", 70));
      def.fixture_only = f.bool_or("fixture_only", false);
      if (def.fixture_only && (def.industry_throughput_bp != kBpOne || def.clinic_capacity_bp != kBpOne)) {
        throw SimError("catalog: faction '" + def.id + "': a fixture profile must carry no modifiers");
      }
      if (def.industry_throughput_bp < 0 || def.clinic_capacity_bp < 0) {
        throw SimError("catalog: faction '" + def.id + "': modifiers must be nonnegative");
      }
      if (def.initial_adherence < 0 || def.initial_adherence > 100) {
        throw SimError("catalog: faction '" + def.id + "': initial_adherence must be 0..100");
      }
      if (faction_by_id_.count(def.id) != 0) throw SimError("catalog: duplicate faction id '" + def.id + "'");
      faction_by_id_[def.id] = static_cast<int>(factions_.size());
      factions_.push_back(def);
    }
  }
  if (const json::Value* ps = root.find("policies")) {
    for (const auto& p : ps->as_array()) {
      p.reject_unknown_keys({"id", "display_key", "is_default", "food_demand_bp", "mining_throughput_bp",
                             "industry_throughput_bp", "stability_target_adjust_bp", "fatigue_delta_per_day",
                             "min_days", "cooldown_days", "adherence_on_entry"},
                            "policies.policy");
      PolicyDef def;
      def.id = p.require_string("id", "policies.policy");
      require_content_id(def.id, "policies.policy.id");
      def.display_key = p.string_or("display_key", "policy." + def.id);
      def.is_default = p.bool_or("is_default", false);
      def.food_demand_bp = p.int_or("food_demand_bp", kBpOne);
      def.mining_throughput_bp = p.int_or("mining_throughput_bp", kBpOne);
      def.industry_throughput_bp = p.int_or("industry_throughput_bp", kBpOne);
      def.stability_target_adjust_bp = p.int_or("stability_target_adjust_bp", 0);
      def.fatigue_delta_per_day = p.int_or("fatigue_delta_per_day", -100);
      def.min_days = p.int_or("min_days", 7);
      def.cooldown_days = p.int_or("cooldown_days", 7);
      if (def.food_demand_bp < 0 || def.mining_throughput_bp < 0 || def.industry_throughput_bp < 0) {
        throw SimError("catalog: policy '" + def.id + "': throughput and demand modifiers must be nonnegative");
      }
      if (const json::Value* a = p.find("adherence_on_entry")) {
        for (const auto& [key, val] : a->as_object()) def.adherence_on_entry[key] = static_cast<int>(val.as_int());
      }
      if (policy_by_id_.count(def.id) != 0) throw SimError("catalog: duplicate policy id '" + def.id + "'");
      if (def.is_default) {
        if (!default_policy_id_.empty()) throw SimError("catalog: more than one default policy");
        default_policy_id_ = def.id;
      }
      policy_by_id_[def.id] = static_cast<int>(policies_.size());
      policies_.push_back(def);
    }
  }
}

void Catalog::load_events(const json::Value& root) {
  for (const auto& e : root.require_array("events", "events")) {
    e.reject_unknown_keys({"id", "display_key", "chain_id", "scope", "applies_to_facility", "queue_class",
                           "condition",
                           "consecutive_days", "condition_resolved", "decision_deadline_days", "cooldown_days",
                           "pauses_simulation", "sector_unique", "expire_keeps_open", "choices", "on_open",
                           "on_resolve", "on_expire", "news_template_open", "news_template_resolved",
                           "news_template_expire"},
                          "events.event");
    EventDef def;
    def.id = e.require_string("id", "events.event");
    require_content_id(def.id, "events.event.id");
    const std::string ctx = "event '" + def.id + "'";
    def.display_key = e.string_or("display_key", "event." + def.id);
    def.chain_id = e.string_or("chain_id", def.id);
    def.scope = e.string_or("scope", "planet");
    if (def.scope != "planet" && def.scope != "facility") {
      throw SimError("catalog: " + ctx + ": scope must be 'planet' or 'facility'");
    }
    if (def.consecutive_days < 0) throw SimError("catalog: " + ctx + ": consecutive_days must be nonnegative");
    def.applies_to_facility = e.string_or("applies_to_facility", "");
    auto qc = parse_queue_class(e.string_or("queue_class", "new_warning"));
    if (!qc) throw SimError("catalog: " + ctx + ": unknown queue_class");
    def.queue_class = *qc;
    if (const json::Value* c = e.find("condition")) {
      def.condition = parse_condition(*c, ctx + ".condition");
      def.has_condition = true;
    }
    def.consecutive_days = static_cast<int>(e.int_or("consecutive_days", 0));
    if (const json::Value* c = e.find("condition_resolved")) {
      def.condition_resolved = parse_condition(*c, ctx + ".condition_resolved");
      def.has_condition_resolved = true;
    }
    def.decision_deadline_days = e.int_or("decision_deadline_days", 0);
    def.cooldown_days = e.int_or("cooldown_days", 0);
    def.pauses_simulation = e.bool_or("pauses_simulation", false);
    def.sector_unique = e.bool_or("sector_unique", false);
    def.expire_keeps_open = e.bool_or("expire_keeps_open", false);
    def.on_open = parse_effects(e.find("on_open"), ctx + ".on_open");
    def.on_resolve = parse_effects(e.find("on_resolve"), ctx + ".on_resolve");
    def.on_expire = parse_effects(e.find("on_expire"), ctx + ".on_expire");
    def.news_template_open = e.string_or("news_template_open", "");
    def.news_template_resolved = e.string_or("news_template_resolved", "");
    def.news_template_expire = e.string_or("news_template_expire", "");
    if (const json::Value* cs = e.find("choices")) {
      for (std::size_t i = 0; i < cs->as_array().size(); ++i) {
        const json::Value& c = cs->as_array()[i];
        const std::string cctx = ctx + ".choices[" + std::to_string(i) + "]";
        c.reject_unknown_keys({"id", "display_key", "cost", "required_reserve_workers", "required_assigned_workers",
                               "effects", "news_template", "resolution", "cancels_scheduled_from"},
                              cctx);
        EventChoiceDef ch;
        ch.id = c.require_string("id", cctx);
        require_content_id(ch.id, cctx + ".id");
        ch.display_key = c.string_or("display_key", "choice." + def.id + "." + ch.id);
        if (const json::Value* cost = c.find("cost")) ch.cost = parse_resource_map(*cost, cctx + ".cost");
        ch.required_reserve_workers = static_cast<int>(c.int_or("required_reserve_workers", 0));
        ch.required_assigned_workers = static_cast<int>(c.int_or("required_assigned_workers", 0));
        ch.effects = parse_effects(c.find("effects"), cctx + ".effects");
        ch.news_template = c.string_or("news_template", "");
        auto res = parse_choice_resolution(c.string_or("resolution", "resolve"));
        if (!res) throw SimError("catalog: " + cctx + ": resolution must be 'resolve' or 'keep_open'");
        ch.resolution = *res;
        if (const json::Value* cancels = c.find("cancels_scheduled_from")) {
          for (const auto& v : cancels->as_array()) ch.cancels_scheduled_from.push_back(v.as_string());
        }
        for (const auto& other : def.choices) {
          if (other.id == ch.id) throw SimError("catalog: " + cctx + ": duplicate choice id '" + ch.id + "'");
        }
        def.choices.push_back(ch);
      }
    }
    if (!def.choices.empty() && def.decision_deadline_days <= 0) {
      throw SimError("catalog: " + ctx + ": an event with choices needs a positive decision_deadline_days");
    }
    if (event_by_id_.count(def.id) != 0) throw SimError("catalog: duplicate event id '" + def.id + "'");
    event_by_id_[def.id] = static_cast<int>(events_.size());
    events_.push_back(def);
  }
}

void Catalog::load_rules(const json::Value& root) {
  if (const json::Value* v = root.find("simulation_version")) simulation_version_ = v->as_string();
  if (const json::Value* n = root.find("civilian_needs")) {
    n->reject_unknown_keys({"food_per_resident", "water_per_resident", "power_per_resident"}, "rules.civilian_needs");
    needs_.food_per_resident = n->int_or("food_per_resident", needs_.food_per_resident);
    needs_.water_per_resident = n->int_or("water_per_resident", needs_.water_per_resident);
    needs_.power_per_resident = n->int_or("power_per_resident", needs_.power_per_resident);
  }
  if (const json::Value* w = root.find("wellbeing")) {
    wellbeing_.health_start = w->int_or("health_start", wellbeing_.health_start);
    wellbeing_.health_down_max = w->int_or("health_down_max", wellbeing_.health_down_max);
    wellbeing_.health_up_max = w->int_or("health_up_max", wellbeing_.health_up_max);
    wellbeing_.stability_start = w->int_or("stability_start", wellbeing_.stability_start);
    wellbeing_.stability_move_max = w->int_or("stability_move_max", wellbeing_.stability_move_max);
    wellbeing_.stability_base_bp = w->int_or("stability_base_bp", wellbeing_.stability_base_bp);
    wellbeing_.stability_food_weight_bp = w->int_or("stability_food_weight_bp", wellbeing_.stability_food_weight_bp);
    wellbeing_.stability_water_weight_bp = w->int_or("stability_water_weight_bp", wellbeing_.stability_water_weight_bp);
    wellbeing_.stability_housing_weight_bp =
        w->int_or("stability_housing_weight_bp", wellbeing_.stability_housing_weight_bp);
    wellbeing_.stability_health_weight_bp =
        w->int_or("stability_health_weight_bp", wellbeing_.stability_health_weight_bp);
    wellbeing_.clinic_floor_bp = w->int_or("clinic_floor_bp", wellbeing_.clinic_floor_bp);
    wellbeing_.clinic_span_bp = w->int_or("clinic_span_bp", wellbeing_.clinic_span_bp);
    wellbeing_.health_factor_base_bp = w->int_or("health_factor_base_bp", wellbeing_.health_factor_base_bp);
    wellbeing_.health_factor_span_bp = w->int_or("health_factor_span_bp", wellbeing_.health_factor_span_bp);
    wellbeing_.fatigue_penalty_span_bp = w->int_or("fatigue_penalty_span_bp", wellbeing_.fatigue_penalty_span_bp);
  }
  if (const json::Value* c = root.find("condition")) {
    condition_.min_bp = c->int_or("min_bp", condition_.min_bp);
    condition_.max_bp = c->int_or("max_bp", condition_.max_bp);
    condition_.maintained_gain_per_day = c->int_or("maintained_gain_per_day", condition_.maintained_gain_per_day);
    condition_.unmaintained_loss_per_day = c->int_or("unmaintained_loss_per_day", condition_.unmaintained_loss_per_day);
    condition_.service_machinery_cost = c->int_or("service_machinery_cost", condition_.service_machinery_cost);
    condition_.service_downtime_days = c->int_or("service_downtime_days", condition_.service_downtime_days);
    condition_.service_gain_bp = c->int_or("service_gain_bp", condition_.service_gain_bp);
    condition_.service_max_condition_bp = c->int_or("service_max_condition_bp", condition_.service_max_condition_bp);
    condition_.service_cooldown_days = c->int_or("service_cooldown_days", condition_.service_cooldown_days);
  }
  if (const json::Value* c = root.find("construction")) {
    construction_.power_per_worker = c->int_or("power_per_worker", construction_.power_per_worker);
    construction_.min_days = c->int_or("min_days", construction_.min_days);
  }
  if (const json::Value* w = root.find("workforce")) {
    workforce_.reserve_to_job_days = w->int_or("reserve_to_job_days", workforce_.reserve_to_job_days);
    workforce_.job_to_job_days = w->int_or("job_to_job_days", workforce_.job_to_job_days);
  }
  if (const json::Value* n = root.find("news")) {
    news_.shortage_open_days = static_cast<int>(n->int_or("shortage_open_days", news_.shortage_open_days));
    news_.shortage_close_days = static_cast<int>(n->int_or("shortage_close_days", news_.shortage_close_days));
    news_.milestone_min_increase_bp = n->int_or("milestone_min_increase_bp", news_.milestone_min_increase_bp);
    news_.milestone_cooldown_days = n->int_or("milestone_cooldown_days", news_.milestone_cooldown_days);
    news_.max_entries = static_cast<int>(n->int_or("max_entries", news_.max_entries));
    news_.max_metric_samples = static_cast<int>(n->int_or("max_metric_samples", news_.max_metric_samples));
  }
}

namespace {

ResourceMap map_or_empty(const Catalog& cat, const json::Value& parent, const std::string& key,
                         const std::string& context) {
  ResourceMap out;
  const json::Value* v = parent.find(key);
  if (v == nullptr) return out;
  if (!v->is_object()) throw SimError("catalog: " + context + "." + key + ": expected an object");
  for (const auto& [rid, val] : v->as_object()) {
    int idx = cat.find_resource(rid);
    if (idx < 0) throw SimError("catalog: " + context + "." + key + ": unknown resource '" + rid + "'");
    if (!val.is_int() || val.as_int() < 0) {
      throw SimError("catalog: " + context + "." + key + ": '" + rid + "' must be a nonnegative integer");
    }
    if (val.as_int() != 0) out[idx] = val.as_int();
  }
  return out;
}

}  // namespace

void Catalog::load_scenarios(const json::Value& root) {
  for (const auto& s : root.require_array("scenarios", "scenarios")) {
    s.reject_unknown_keys({"id", "display_key", "start_day", "planets", "expedition", "freight", "mandate", "relief",
                           "completion", "survival", "events"},
                          "scenarios.scenario");
    ScenarioDef def;
    def.id = s.require_string("id", "scenarios.scenario");
    require_content_id(def.id, "scenarios.scenario.id");
    const std::string ctx = "scenario '" + def.id + "'";
    def.display_key = s.string_or("display_key", "scenario." + def.id);
    def.start_day = s.int_or("start_day", 0);

    for (const auto& p : s.require_array("planets", ctx)) {
      p.reject_unknown_keys({"planet_id", "colonised", "population", "workers", "stocks", "facilities", "health_bp",
                             "fatigue_bp", "stability_bp"},
                            ctx + ".planets");
      ScenarioPlanet sp;
      sp.planet_id = p.require_string("planet_id", ctx + ".planets");
      if (find_planet(sp.planet_id) == nullptr) {
        throw SimError("catalog: " + ctx + ": unknown planet '" + sp.planet_id + "'");
      }
      sp.colonised = p.bool_or("colonised", false);
      sp.population = p.int_or("population", 0);
      sp.workers = p.int_or("workers", 0);
      if (sp.workers > sp.population) throw SimError("catalog: " + ctx + ": workers exceed population");
      sp.stocks = map_or_empty(*this, p, "stocks", ctx + ".planets");
      sp.health_bp = p.int_or("health_bp", wellbeing_.health_start);
      sp.fatigue_bp = p.int_or("fatigue_bp", 0);
      sp.stability_bp = p.int_or("stability_bp", wellbeing_.stability_start);
      if (const json::Value* fs = p.find("facilities")) {
        for (const auto& f : fs->as_array()) {
          f.reject_unknown_keys({"facility_id", "recipe_id", "assigned_workers", "condition_bp"}, ctx + ".facilities");
          StartingFacility sf;
          sf.facility_id = f.require_string("facility_id", ctx + ".facilities");
          const FacilityDef* fd = find_facility(sf.facility_id);
          if (fd == nullptr) throw SimError("catalog: " + ctx + ": unknown facility '" + sf.facility_id + "'");
          sf.recipe_id = f.string_or("recipe_id", fd->recipes.front().id);
          if (fd->recipe_index(sf.recipe_id) < 0) {
            throw SimError("catalog: " + ctx + ": facility '" + sf.facility_id + "' has no recipe '" + sf.recipe_id + "'");
          }
          sf.assigned_workers = static_cast<int>(f.int_or("assigned_workers", -1));
          sf.condition_bp = f.int_or("condition_bp", kBpOne);
          if (sf.condition_bp < condition_.min_bp || sf.condition_bp > condition_.max_bp) {
            throw SimError("catalog: " + ctx + ": starting condition out of range for '" + sf.facility_id + "'");
          }
          sp.facilities.push_back(sf);
        }
      }
      def.planets.push_back(sp);
    }

    const json::Value& ex = s.require("expedition", ctx);
    ex.reject_unknown_keys({"target_planet", "residents", "workers", "launch_cost", "cargo", "transit_per_day",
                            "transit_days", "hub_facility_id", "launch_deadline_day"},
                           ctx + ".expedition");
    def.expedition.target_planet = ex.require_string("target_planet", ctx + ".expedition");
    if (find_planet(def.expedition.target_planet) == nullptr) {
      throw SimError("catalog: " + ctx + ".expedition: unknown target planet");
    }
    def.expedition.residents = ex.int_or("residents", 100);
    def.expedition.workers = ex.int_or("workers", 50);
    def.expedition.launch_cost = map_or_empty(*this, ex, "launch_cost", ctx + ".expedition");
    def.expedition.cargo = map_or_empty(*this, ex, "cargo", ctx + ".expedition");
    def.expedition.transit_per_day = map_or_empty(*this, ex, "transit_per_day", ctx + ".expedition");
    def.expedition.transit_days = ex.int_or("transit_days", 2);
    def.expedition.hub_facility_id = ex.string_or("hub_facility_id", "colony_hub");
    if (find_facility(def.expedition.hub_facility_id) == nullptr) {
      throw SimError("catalog: " + ctx + ".expedition: unknown hub facility");
    }
    def.expedition.launch_deadline_day = ex.int_or("launch_deadline_day", 60);
    if (def.expedition.workers > def.expedition.residents) {
      throw SimError("catalog: " + ctx + ".expedition: workers exceed residents");
    }
    // Cargo must be covered by the launch cost: an expedition cannot create goods.
    for (const auto& [idx, qty] : def.expedition.cargo) {
      auto it = def.expedition.launch_cost.find(idx);
      Milli paid = it == def.expedition.launch_cost.end() ? 0 : it->second;
      if (qty > paid) {
        throw SimError("catalog: " + ctx + ".expedition: cargo of '" + resource(idx).id +
                       "' exceeds the debited launch cost");
      }
    }

    const json::Value& fr = s.require("freight", ctx);
    fr.reject_unknown_keys({"origin_planet", "destination_planet", "cargo_capacity", "fuel_tank_capacity", "crew",
                            "leg_days", "min_dwell_days", "round_trip_fuel", "outbound_fuel_burn", "return_fuel_burn",
                            "round_trip_machinery", "default_food_reserve_days", "default_coal_reserve_days"},
                           ctx + ".freight");
    def.freight.origin_planet = fr.require_string("origin_planet", ctx + ".freight");
    def.freight.destination_planet = fr.require_string("destination_planet", ctx + ".freight");
    if (find_planet(def.freight.origin_planet) == nullptr || find_planet(def.freight.destination_planet) == nullptr) {
      throw SimError("catalog: " + ctx + ".freight: unknown route endpoint");
    }
    def.freight.cargo_capacity = fr.int_or("cargo_capacity", def.freight.cargo_capacity);
    def.freight.fuel_tank_capacity = fr.int_or("fuel_tank_capacity", def.freight.fuel_tank_capacity);
    def.freight.crew = static_cast<int>(fr.int_or("crew", def.freight.crew));
    def.freight.leg_days = fr.int_or("leg_days", def.freight.leg_days);
    def.freight.min_dwell_days = fr.int_or("min_dwell_days", def.freight.min_dwell_days);
    def.freight.round_trip_fuel = fr.int_or("round_trip_fuel", def.freight.round_trip_fuel);
    def.freight.outbound_fuel_burn = fr.int_or("outbound_fuel_burn", def.freight.outbound_fuel_burn);
    def.freight.return_fuel_burn = fr.int_or("return_fuel_burn", def.freight.return_fuel_burn);
    def.freight.round_trip_machinery = fr.int_or("round_trip_machinery", def.freight.round_trip_machinery);
    def.freight.default_food_reserve_days = fr.int_or("default_food_reserve_days", 3);
    def.freight.default_coal_reserve_days = fr.int_or("default_coal_reserve_days", 3);
    if (def.freight.outbound_fuel_burn + def.freight.return_fuel_burn > def.freight.round_trip_fuel) {
      throw SimError("catalog: " + ctx + ".freight: burn exceeds the reserved round-trip fuel");
    }
    if (def.freight.round_trip_fuel > def.freight.fuel_tank_capacity) {
      throw SimError("catalog: " + ctx + ".freight: round-trip fuel exceeds tank capacity");
    }

    const json::Value& md = s.require("mandate", ctx);
    md.reject_unknown_keys({"id", "issue_day", "deadline_day", "requirement", "negotiated_requirement",
                            "negotiate_before_day", "negotiate_adherence", "honour_adherence", "fail_adherence",
                            "mission_outbound_days", "mission_service_days", "mission_return_days", "mission_fuel",
                            "mission_machinery", "last_departure_day"},
                           ctx + ".mandate");
    def.mandate.id = md.string_or("id", def.mandate.id);
    def.mandate.issue_day = md.int_or("issue_day", def.mandate.issue_day);
    def.mandate.deadline_day = md.int_or("deadline_day", def.mandate.deadline_day);
    def.mandate.requirement = map_or_empty(*this, md, "requirement", ctx + ".mandate");
    def.mandate.negotiated_requirement = map_or_empty(*this, md, "negotiated_requirement", ctx + ".mandate");
    def.mandate.negotiate_before_day = md.int_or("negotiate_before_day", def.mandate.negotiate_before_day);
    if (const json::Value* a = md.find("negotiate_adherence")) {
      for (const auto& [key, val] : a->as_object()) def.mandate.negotiate_adherence[key] = static_cast<int>(val.as_int());
    }
    def.mandate.honour_adherence = static_cast<int>(md.int_or("honour_adherence", def.mandate.honour_adherence));
    def.mandate.fail_adherence = static_cast<int>(md.int_or("fail_adherence", def.mandate.fail_adherence));
    def.mandate.mission_outbound_days = md.int_or("mission_outbound_days", def.mandate.mission_outbound_days);
    def.mandate.mission_service_days = md.int_or("mission_service_days", def.mandate.mission_service_days);
    def.mandate.mission_return_days = md.int_or("mission_return_days", def.mandate.mission_return_days);
    def.mandate.mission_fuel = md.int_or("mission_fuel", def.mandate.mission_fuel);
    def.mandate.mission_machinery = md.int_or("mission_machinery", def.mandate.mission_machinery);
    def.mandate.last_departure_day = md.int_or("last_departure_day", def.mandate.last_departure_day);
    if (def.mandate.requirement.empty()) throw SimError("catalog: " + ctx + ".mandate: requirement is empty");
    if (def.mandate.deadline_day <= def.mandate.issue_day) {
      throw SimError("catalog: " + ctx + ".mandate: deadline must follow the issue day");
    }

    const json::Value& rl = s.require("relief", ctx);
    rl.reject_unknown_keys({"grant", "delivery_delay_days", "adherence_cost", "flag"}, ctx + ".relief");
    def.relief.grant = map_or_empty(*this, rl, "grant", ctx + ".relief");
    def.relief.delivery_delay_days = rl.int_or("delivery_delay_days", 2);
    def.relief.adherence_cost = static_cast<int>(rl.int_or("adherence_cost", -10));
    def.relief.flag = rl.string_or("flag", "relief_used");
    if (def.relief.grant.empty()) throw SimError("catalog: " + ctx + ".relief: grant is empty");

    const json::Value& cp = s.require("completion", ctx);
    cp.reject_unknown_keys({"evaluation_day", "colony_launch_by_day", "colony_founded_min_days_before_end",
                            "min_health_bp", "min_stability_bp", "final_clean_days"},
                           ctx + ".completion");
    def.completion.evaluation_day = cp.int_or("evaluation_day", 120);
    def.completion.colony_launch_by_day = cp.int_or("colony_launch_by_day", 60);
    def.completion.colony_founded_min_days_before_end = cp.int_or("colony_founded_min_days_before_end", 20);
    def.completion.min_health_bp = cp.int_or("min_health_bp", 6000);
    def.completion.min_stability_bp = cp.int_or("min_stability_bp", 4000);
    def.completion.final_clean_days = static_cast<int>(cp.int_or("final_clean_days", 10));

    if (const json::Value* sv = s.find("survival")) {
      sv->reject_unknown_keys({"emergency_fulfilment_bp", "emergency_days", "failure_days"}, ctx + ".survival");
      def.survival.emergency_fulfilment_bp = sv->int_or("emergency_fulfilment_bp", 2500);
      def.survival.emergency_days = static_cast<int>(sv->int_or("emergency_days", 5));
      def.survival.failure_days = static_cast<int>(sv->int_or("failure_days", 10));
      if (def.survival.failure_days <= def.survival.emergency_days) {
        throw SimError("catalog: " + ctx + ".survival: failure_days must exceed emergency_days");
      }
    }

    if (const json::Value* evs = s.find("events")) {
      for (const auto& e : evs->as_array()) def.event_ids.push_back(e.as_string());
    }

    if (scenario_by_id_.count(def.id) != 0) throw SimError("catalog: duplicate scenario id '" + def.id + "'");
    scenario_by_id_[def.id] = static_cast<int>(scenarios_.size());
    scenarios_.push_back(def);
  }
}

// ---------------------------------------------------------------------------
// Whole-catalog validation. All referenced IDs must exist before a scenario can
// start (TDD 4.3).
// ---------------------------------------------------------------------------

void Catalog::validate() {
  static const char* kRequiredResources[] = {"food", "water", "iron_ore", "coal", "steel", "machinery", "fuel"};
  for (const char* r : kRequiredResources) {
    if (find_resource(r) < 0) throw SimError(std::string("catalog: required P1 resource '") + r + "' is missing");
  }
  food_ = resource_index("food");
  water_ = resource_index("water");
  iron_ore_ = resource_index("iron_ore");
  coal_ = resource_index("coal");
  steel_ = resource_index("steel");
  machinery_ = resource_index("machinery");
  fuel_ = resource_index("fuel");

  if (playable_factions().empty()) throw SimError("catalog: at least one playable faction is required");
  if (policies_.empty()) throw SimError("catalog: at least one policy is required");
  if (default_policy_id_.empty()) throw SimError("catalog: no policy is marked is_default");
  for (const auto& p : policies_) {
    for (const auto& [fid, unused] : p.adherence_on_entry) {
      (void)unused;
      if (faction_by_id_.count(fid) == 0) {
        throw SimError("catalog: policy '" + p.id + "': unknown faction '" + fid + "' in adherence_on_entry");
      }
    }
  }
  std::set<std::string> recipe_ids;
  for (const auto& f : facilities_) {
    for (const auto& r : f.recipes) {
      if (!recipe_ids.insert(r.id).second) throw SimError("catalog: duplicate recipe id '" + r.id + "'");
      if (r.staff == 0 && !r.outputs.empty() && r.power_output_per_day == 0 && r.inputs.empty()) {
        // A zero-staff commodity producer would be free output; reject it.
        throw SimError("catalog: recipe '" + r.id + "': a zero-staff recipe cannot produce commodities");
      }
    }
    if (f.one_per_world && f.buildable) {
      throw SimError("catalog: facility '" + f.id + "': a one-per-world founding asset must not be buildable in P1");
    }
  }
  // Every metric a condition reads must be readable in the scope it names, so a
  // typo fails to load rather than throwing when the condition is first evaluated.
  std::function<void(const ConditionNode&, const std::string&)> check_condition =
      [&](const ConditionNode& node, const std::string& where) {
        if (node.kind == ConditionKind::CompareMetric && !is_known_metric(*this, node.metric, node.scope)) {
          throw SimError("catalog: " + where + ": metric '" + node.metric + "' is not readable in scope '" +
                         node.scope + "'");
        }
        for (const auto& child : node.children) check_condition(child, where);
      };
  for (const auto& e : events_) {
    if (e.has_condition) check_condition(e.condition, "event '" + e.id + "'.condition");
    if (e.has_condition_resolved) check_condition(e.condition_resolved, "event '" + e.id + "'.condition_resolved");
  }
  for (const auto& e : events_) {
    if (!e.applies_to_facility.empty() && facility_by_id_.count(e.applies_to_facility) == 0) {
      throw SimError("catalog: event '" + e.id + "': applies_to_facility names an unknown facility");
    }
    auto check_effects = [&](const std::vector<EffectDef>& list, const std::string& where) {
      for (const auto& eff : list) {
        if (!eff.faction.empty() && faction_by_id_.count(eff.faction) == 0) {
          throw SimError("catalog: event '" + e.id + "': " + where + " references unknown faction '" + eff.faction + "'");
        }
        for (const auto& nested : eff.nested) {
          if (!nested.faction.empty() && faction_by_id_.count(nested.faction) == 0) {
            throw SimError("catalog: event '" + e.id + "': " + where + " nested effect references unknown faction");
          }
        }
      }
    };
    check_effects(e.on_open, "on_open");
    check_effects(e.on_resolve, "on_resolve");
    check_effects(e.on_expire, "on_expire");
    for (const auto& ch : e.choices) {
      check_effects(ch.effects, "choice '" + ch.id + "'");
      for (const auto& other : ch.cancels_scheduled_from) {
        bool found = false;
        for (const auto& candidate : e.choices) {
          if (candidate.id == other) found = true;
        }
        if (!found) {
          throw SimError("catalog: event '" + e.id + "': choice '" + ch.id + "' cancels unknown choice '" + other + "'");
        }
      }
    }
    for (const auto& eff : e.on_expire) {
      if (eff.kind == EffectKind::OpenEvent && event_by_id_.count(eff.key) == 0) {
        throw SimError("catalog: event '" + e.id + "': on_expire opens unknown event '" + eff.key + "'");
      }
      for (const auto& nested : eff.nested) {
        if (nested.kind == EffectKind::OpenEvent && event_by_id_.count(nested.key) == 0) {
          throw SimError("catalog: event '" + e.id + "': on_expire schedules an unknown event");
        }
      }
    }
    for (const auto& eff : e.on_open) {
      if (eff.kind == EffectKind::OpenEvent && event_by_id_.count(eff.key) == 0) {
        throw SimError("catalog: event '" + e.id + "': on_open opens unknown event '" + eff.key + "'");
      }
    }
    for (const auto& ch : e.choices) {
      for (const auto& eff : ch.effects) {
        if (eff.kind == EffectKind::OpenEvent && event_by_id_.count(eff.key) == 0) {
          throw SimError("catalog: event '" + e.id + "': choice '" + ch.id + "' opens unknown event '" + eff.key + "'");
        }
        for (const auto& nested : eff.nested) {
          if (nested.kind == EffectKind::OpenEvent && event_by_id_.count(nested.key) == 0) {
            throw SimError("catalog: event '" + e.id + "': scheduled effect opens unknown event '" + nested.key + "'");
          }
        }
      }
    }
  }
  if (scenarios_.empty()) throw SimError("catalog: at least one scenario is required");
  for (const auto& s : scenarios_) {
    for (const auto& eid : s.event_ids) {
      if (event_by_id_.count(eid) == 0) {
        throw SimError("catalog: scenario '" + s.id + "': unknown event '" + eid + "'");
      }
    }
    std::set<std::string> seen_planets;
    for (const auto& p : s.planets) {
      if (!seen_planets.insert(p.planet_id).second) {
        throw SimError("catalog: scenario '" + s.id + "': planet '" + p.planet_id + "' appears twice");
      }
      const PlanetDef& pd = planet(p.planet_id);
      if (static_cast<int>(p.facilities.size()) > pd.slot_count) {
        throw SimError("catalog: scenario '" + s.id + "': planet '" + p.planet_id + "' starts with more facilities than slots");
      }
      for (const auto& [idx, qty] : p.stocks) {
        if (qty > pd.resource_capacity) {
          throw SimError("catalog: scenario '" + s.id + "': starting stock of '" + resource(idx).id +
                         "' exceeds capacity on '" + p.planet_id + "'");
        }
      }
    }
    if (seen_planets.count(s.freight.origin_planet) == 0 || seen_planets.count(s.freight.destination_planet) == 0) {
      throw SimError("catalog: scenario '" + s.id + "': the freight route references a planet absent from the scenario");
    }
    if (seen_planets.count(s.expedition.target_planet) == 0) {
      throw SimError("catalog: scenario '" + s.id + "': the expedition target is absent from the scenario");
    }
  }
}

Catalog Catalog::load(const ContentSource& source) {
  // Collect every definition file, sorted bytewise by its path, so the catalog
  // hash does not depend on how a host enumerated them.
  std::vector<ContentFile> files;
  for (auto& f : source.read_all()) {
    // Schema documentation is not a definition.
    if (f.path.rfind("schemas/", 0) == 0) continue;
    if (f.path.size() < 5 || f.path.compare(f.path.size() - 5, 5, ".json") != 0) continue;
    files.push_back(std::move(f));
  }
  std::sort(files.begin(), files.end(), [](const ContentFile& a, const ContentFile& b) {
    return json::BytewiseLess{}(a.path, b.path);
  });
  if (files.empty()) throw SimError("catalog: no definition files found in " + source.describe());
  for (std::size_t i = 1; i < files.size(); ++i) {
    if (files[i].path == files[i - 1].path) {
      throw SimError("catalog: '" + files[i].path + "' was supplied twice by " + source.describe());
    }
  }

  Catalog cat;
  struct Loaded {
    std::string rel;
    std::string schema;
    json::Value root;
    std::string canonical;
  };
  std::vector<Loaded> loaded;
  loaded.reserve(files.size());
  for (const auto& f : files) {
    json::Value root = json::parse(f.bytes);
    if (!root.is_object()) throw SimError("catalog: " + f.path + ": top level must be an object");
    std::string schema = root.require_string("schema", f.path);
    loaded.push_back({f.path, schema, root, json::serialize_canonical(root)});
  }

  std::string hash_input;
  for (const auto& l : loaded) {
    hash_input += l.rel;
    hash_input.push_back('\n');
    hash_input += l.canonical;
    hash_input.push_back('\n');
  }
  cat.hash_ = sha256_hex(hash_input);

  static const char* kPassOrder[] = {"resources", "rules", "planets", "facilities", "factions", "events", "scenarios"};
  std::set<std::string> known(std::begin(kPassOrder), std::end(kPassOrder));
  for (const auto& l : loaded) {
    if (known.count(l.schema) == 0) {
      throw SimError("catalog: " + l.rel + ": unknown schema '" + l.schema + "'");
    }
  }
  for (const char* pass : kPassOrder) {
    for (const auto& l : loaded) {
      if (l.schema != pass) continue;
      if (l.schema == "resources") {
        cat.load_resources(l.root);
      } else if (l.schema == "rules") {
        cat.load_rules(l.root);
      } else if (l.schema == "planets") {
        cat.load_planets(l.root);
      } else if (l.schema == "facilities") {
        cat.load_facilities(l.root);
      } else if (l.schema == "factions") {
        cat.load_factions(l.root);
      } else if (l.schema == "events") {
        cat.load_events(l.root);
      } else if (l.schema == "scenarios") {
        cat.load_scenarios(l.root);
      }
    }
  }
  cat.validate();
  return cat;
}

}  // namespace expansion
