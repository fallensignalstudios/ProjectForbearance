#include "expansion/text.hpp"

#include <sstream>

namespace expansion::text {
namespace {

const std::map<std::string, std::string>& strings() {
  static const std::map<std::string, std::string> kStrings = {
      {"news.first_delivery", "First {0} delivery accepted at {planet}: {quantity} units."},
      {"news.first_return_delivery_departed", "The freighter left {planet} carrying a return shipment."},
      {"news.colony_launched", "The colony expedition departed for {0}; it arrives on day {arrival_day}."},
      {"news.colony_founded", "{planet} is founded. The expedition delivered its surviving cargo."},
      {"news.construction_completed", "Construction of {0} finished at {planet}; it is usable tomorrow."},
      {"news.shortage_opened", "{0} has been short at {planet} for {missed_days} days."},
      {"news.recovery", "{0} supply at {planet} has recovered."},
      {"news.first_surplus", "{planet} recorded its first {0} surplus: {quantity} units in one day."},
      {"news.production_high", "{planet} set a new {0} production high: {quantity} units in one day."},
      {"news.survival_emergency", "A survival emergency is declared at {planet}."},
      {"news.survival_emergency_lifted", "The survival emergency at {planet} has been lifted."},
      {"news.relief_requested", "A relief contract was accepted for {planet}; it arrives on day {arrives_day}."},
      {"news.relief_delivered", "Relief supplies arrived at {planet}."},
      {"news.safety_warning", "Safety inspectors issued a warning at {planet}."},
      {"news.safety_warning_lapsed", "The safety warning at {planet} lapsed without a restored condition."},
      {"news.accident_prevented", "Restored condition prevented an accident at {planet}."},
      {"news.mining_accident", "A mining accident occurred at {planet}."},
      {"news.accident_response", "A response to the accident at {planet} was chosen: {1}."},
      {"news.accident_deferred", "The administration deferred action on the accident at {planet}."},
      {"news.worker_demands", "Workers at {planet} presented safety demands."},
      {"news.worker_demands_settled", "The safety settlement at {planet} was accepted."},
      {"news.worker_demands_refused", "The safety settlement at {planet} was refused; work has stopped."},
      {"news.faction_review", "The faction opened a review of the administration."},
      {"news.faction_review_resolved", "The faction review was resolved: {1}."},
      {"news.policy_change", "{planet} adopted the {0} policy."},
      {"news.mandate_issued", "An external front issued a supply mandate."},
      {"news.mandate_negotiated", "The mandate was renegotiated to a smaller manifest."},
      {"news.mandate_declined", "The administration declined the mandate."},
      {"news.mandate_delivery", "A supply mission was accepted at the front."},
      {"news.mandate_fulfilled", "The supply mandate was fulfilled."},
      {"news.mandate_failed", "The supply mandate lapsed unfulfilled."},
      {"news.mission_departed", "A supply mission departed for the front."},
      {"news.missed_colonial_manifest", "A scheduled colonial shipment was missed; the freighter was unavailable."},
      {"news.scenario_complete", "The evaluation closed: Complete."},
      {"news.scenario_compromised", "The evaluation closed: Compromised."},
      {"news.scenario_failed", "The scenario ended in civilian collapse after {days} days."},
      {"news.surrendered", "The administration surrendered on day {day}."},
  };
  return kStrings;
}

const std::map<std::string, std::string>& reasons() {
  static const std::map<std::string, std::string> kReasons = {
      {"full_throughput", "running at full throughput"},
      {"idle_by_choice", "idle by choice"},
      {"servicing_downtime", "stopped for servicing"},
      {"under_construction", "still under construction"},
      {"commissioning", "commissioned; usable tomorrow"},
      {"no_staff", "no workers assigned"},
      {"understaffed", "short of workers"},
      {"poor_health", "reduced by poor health"},
      {"fatigued", "reduced by fatigue"},
      {"poor_condition", "reduced by machinery condition"},
      {"missing_input", "short of an input material"},
      {"power_shortfall", "short of power"},
      {"output_store_full", "the output store is full"},
      {"active_modifier", "reduced by an active effect"},
      {"insufficient_stock", "not enough unreserved stock"},
      {"insufficient_handling", "not enough port handling capacity today"},
      {"ship_unavailable", "the freighter is unavailable"},
      {"ship_not_docked", "the freighter is not docked here"},
      {"dwell_not_elapsed", "the freighter's dock day has not elapsed"},
      {"insufficient_fuel", "not enough fuel"},
      {"no_spaceport", "the destination has no working spaceport"},
      {"zero_cargo_not_authorised", "an empty outward trip needs explicit authorisation"},
      {"not_enough_workers", "not enough workers"},
      {"overstaffed", "that job is already at full staff"},
      {"no_free_slot", "no free planetary slot"},
      {"revision_mismatch", "the preview was built on an older revision"},
  };
  return kReasons;
}

std::string render_number(const std::string& name, std::int64_t value) {
  auto ends_with = [&](const char* suffix) {
    const std::string s(suffix);
    return name.size() >= s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0;
  };
  if (ends_with("_bp")) return format_bp_percent(value);
  if (ends_with("_day") || ends_with("_days") || ends_with("_count") || name == "day" || name == "days" ||
      name == "workers" || name == "residents" || name == "adherence") {
    return std::to_string(value);
  }
  return format_milli(value);
}

}  // namespace

std::string display_name(const std::string& content_id) {
  std::string out;
  out.reserve(content_id.size());
  bool start_of_word = true;
  for (char c : content_id) {
    if (c == '_') {
      out.push_back(' ');
      start_of_word = true;
      continue;
    }
    if (start_of_word && c >= 'a' && c <= 'z') {
      out.push_back(static_cast<char>(c - 'a' + 'A'));
    } else {
      out.push_back(c);
    }
    start_of_word = false;
  }
  return out;
}

const std::string& lookup(const std::string& key) {
  auto it = strings().find(key);
  if (it != strings().end()) return it->second;
  return key;
}

const std::string& reason_text(const std::string& reason_id) {
  auto it = reasons().find(reason_id);
  if (it != reasons().end()) return it->second;
  return reason_id;
}

std::string format(const std::string& key, const std::vector<NamedValue>& args,
                   const std::vector<std::string>& text_args) {
  const std::string& tmpl = lookup(key);
  std::string out;
  out.reserve(tmpl.size() + 32);
  for (std::size_t i = 0; i < tmpl.size(); ++i) {
    if (tmpl[i] != '{') {
      out.push_back(tmpl[i]);
      continue;
    }
    std::size_t close = tmpl.find('}', i);
    if (close == std::string::npos) {
      out.push_back(tmpl[i]);
      continue;
    }
    const std::string name = tmpl.substr(i + 1, close - i - 1);
    i = close;
    bool replaced = false;
    if (!name.empty() && name[0] >= '0' && name[0] <= '9') {
      const std::size_t index = static_cast<std::size_t>(std::stoul(name));
      if (index < text_args.size()) {
        out += display_name(text_args[index]);
        replaced = true;
      }
    }
    if (!replaced) {
      for (const auto& a : args) {
        if (a.key == name) {
          out += render_number(name, a.value);
          replaced = true;
          break;
        }
      }
    }
    if (!replaced) out += "{" + name + "}";
  }
  return out;
}

std::string news_line(const NewsRecord& n) {
  std::string body = format(n.template_key, n.args, n.text_args);
  // Substitute the planet name separately so it is never treated as a number.
  const std::string token = "{planet}";
  const std::string name = n.planet_id.empty() ? std::string("The sector") : display_name(n.planet_id);
  std::size_t pos = body.find(token);
  while (pos != std::string::npos) {
    body.replace(pos, token.size(), name);
    pos = body.find(token);
  }
  std::ostringstream ss;
  ss << "day " << n.day << "  " << body;
  return ss.str();
}

std::string fact_line(const FactRecord& f) {
  std::ostringstream ss;
  ss << "day " << f.day << "  " << f.kind;
  if (!f.planet_id.empty()) ss << " @" << f.planet_id;
  if (f.entity_id != 0) ss << " #" << f.entity_id;
  for (const auto& t : f.text_args) ss << " " << t;
  for (const auto& a : f.args) ss << " " << a.key << "=" << render_number(a.key, a.value);
  if (!f.reason_id.empty()) ss << " (" << f.reason_id << ")";
  if (!f.causal_parents.empty()) {
    ss << " <-";
    for (InstanceId p : f.causal_parents) ss << " #" << p;
  }
  return ss.str();
}

}  // namespace expansion::text
