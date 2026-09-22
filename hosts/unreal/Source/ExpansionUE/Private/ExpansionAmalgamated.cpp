// The simulation, as one translation unit.
//
// UnrealBuildTool compiles the .cpp files it discovers under a module directory.
// The simulation deliberately lives outside any engine module -- it has to build
// with no engine present -- so this file is the seam: it includes each source in
// turn, and it is the only .cpp UnrealBuildTool needs to find.
//
// Two consequences follow from sharing one translation unit, and both are real
// rather than theoretical: two file-static helpers with the same name become an
// ambiguity, and a macro one source defines reaches the next. Neither is a
// hypothetical risk to be watched for -- the first one was caught by building
// this arrangement, which is why the CMake build carries the same single-unit
// target and runs the whole test suite against it. If you add a source, add it
// here and to CMakeLists.txt; tools/check_amalgamation.py fails when the two
// disagree, and the expansion_tests_amalgamated target fails when the result is
// not the same simulation.
//
// hostfs/src is absent on purpose: a packaged build reads content through the
// engine's platform layer, not through a directory.

#include "core/src/api.cpp"
#include "core/src/host_services.cpp"
#include "core/src/units.cpp"
#include "core/src/json.cpp"
#include "core/src/sha256.cpp"
#include "core/src/catalog.cpp"
#include "core/src/state.cpp"
#include "core/src/state_codec.cpp"
#include "core/src/metrics.cpp"
#include "core/src/sim_support.cpp"
#include "core/src/sim_effects.cpp"
#include "core/src/sim_day.cpp"
#include "core/src/sim_production.cpp"
#include "core/src/sim_freight.cpp"
#include "core/src/sim_consequences.cpp"
#include "core/src/sim_commands.cpp"
#include "core/src/session.cpp"
#include "persistence/src/save_file.cpp"
#include "persistence/src/replay.cpp"
#include "presentation/src/read_models.cpp"
#include "presentation/src/view_models.cpp"
#include "presentation/src/text.cpp"
