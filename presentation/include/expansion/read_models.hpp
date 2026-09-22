// Read-only views assembled from a committed snapshot (TDD 15.2).
//
// The presentation layer never mutates resources or workers; everything here is
// derived from the authoritative state and can be recomputed.
#pragma once

#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/session.hpp"

namespace expansion::read {

struct Concern {
  std::string planet_id;
  std::string headline;
  std::string cause;        // typed reason id
  Day days_remaining = -1;  // -1 when not a countdown
  int severity = 0;         // lower is more urgent
};

// "What is at risk, when, and why" comes first; detail is the third layer.
std::string civilization_overview(const Session& session);
std::string network_map(const Session& session);
std::string planet_inspector(const Session& session, const std::string& planet_id);
std::string facility_inspector(const Session& session, InstanceId facility_id);
std::string freight_inspector(const Session& session);
std::string decision_drawer(const Session& session);
std::string history_drawer(const Session& session, int max_entries, const std::string& search);
std::string ledger_view(const Session& session, Day day);
std::string outcome_report(const Session& session);

// The three most actionable concerns, cause first.
std::vector<Concern> top_concerns(const Session& session, int limit);

}  // namespace expansion::read
