// Player commands (TDD 5.1).
//
// A command executes atomically between simulation days, including while paused.
// Successful commands increment the revision and command sequence but do not
// advance the calendar. Duplicate command ids return the recorded result without
// applying again. A rejected command alters neither state nor schedule.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "expansion/units.hpp"

namespace expansion {

enum class CommandKind {
  AssignWorkers,            // move `count` workers between Reserve (0) and facilities
  SetProductionPriority,    // reorder within the configurable non-residential bands
  SetFacilityIdle,
  StartConstruction,
  CancelConstruction,
  ServiceFacility,
  SelectPolicy,
  UpdateRoute,
  AuthoriseDeparture,       // book and depart the freighter on its colony route
  LaunchColonization,
  ResolveEvent,
  RespondToDemand,          // honour | negotiate | decline
  LaunchStrategicMission,
  RequestRelief,
  Surrender,
};

const char* command_kind_id(CommandKind k);
std::optional<CommandKind> parse_command_kind(const std::string& s);

struct Command {
  std::string id;               // stable id; a repeat returns the recorded result
  CommandKind kind = CommandKind::Surrender;
  std::string planet_id;
  std::string content_id;       // facility / policy / choice / response / route field id
  std::string recipe_id;        // fixed recipe variant, for a multi-variant facility
  InstanceId facility_id = 0;   // 0 == Reserve for AssignWorkers
  InstanceId to_facility_id = 0;
  InstanceId event_instance_id = 0;
  int count = 0;
  int priority_band = 0;
  bool flag = false;                          // idle / enabled / override
  std::map<int, Milli> resources;             // manifest or route targets
  std::map<int, Milli> floors;                // explicit reserve floors
  std::vector<int> floor_overrides;           // resources whose default floor is waived
};

struct CommandResult {
  bool accepted = false;
  Revision revision = 0;
  std::string reason;      // typed reason id
  std::string detail;      // human-readable specifics, e.g. an exact deficit
  std::vector<InstanceId> changed_ids;
  bool replayed = false;   // duplicate command id returned its recorded result
};

}  // namespace expansion
