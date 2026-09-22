cmake_minimum_required(VERSION 3.20)
project(SovereignCallExpansion LANGUAGES CXX VERSION 0.1.0)

# E0: engine-independent C++20 core with a headless command-line host. The core
# builds with no engine installed and no third-party dependency (TDD 3.2, 19.4).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(EXPANSION_WERROR "Treat warnings as errors" ON)
option(EXPANSION_BUILD_TESTS "Build the verification suite" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:preprocessor)
  if(EXPANSION_WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
  if(EXPANSION_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

add_library(expansion_core STATIC
  core/src/units.cpp
  core/src/json.cpp
  core/src/sha256.cpp
  core/src/catalog.cpp
  core/src/state.cpp
  core/src/state_codec.cpp
  core/src/metrics.cpp
  core/src/sim_support.cpp
  core/src/sim_effects.cpp
  core/src/sim_day.cpp
  core/src/sim_production.cpp
  core/src/sim_freight.cpp
  core/src/sim_consequences.cpp
  core/src/sim_commands.cpp
  core/src/session.cpp
)
target_include_directories(expansion_core PUBLIC core/include)

add_library(expansion_persistence STATIC
  persistence/src/save_file.cpp
  persistence/src/replay.cpp
)
target_include_directories(expansion_persistence PUBLIC persistence/include)
target_link_libraries(expansion_persistence PUBLIC expansion_core)

add_library(expansion_presentation STATIC
  presentation/src/read_models.cpp
  presentation/src/text.cpp
)
target_include_directories(expansion_presentation PUBLIC presentation/include)
target_link_libraries(expansion_presentation PUBLIC expansion_core)

add_executable(expansion_cli
  cli/main.cpp
  cli/commands.cpp
)
target_link_libraries(expansion_cli PRIVATE expansion_core expansion_persistence expansion_presentation)
set_target_properties(expansion_cli PROPERTIES OUTPUT_NAME expansion)

if(EXPANSION_BUILD_TESTS)
  enable_testing()
  add_executable(expansion_tests
    tests/test_main.cpp
    tests/unit/test_units.cpp
    tests/unit/test_json.cpp
    tests/unit/test_catalog.cpp
    tests/unit/test_inventory.cpp
    tests/integration/test_economy.cpp
    tests/integration/test_freight.cpp
    tests/integration/test_events.cpp
    tests/integration/test_persistence.cpp
    tests/integration/test_scenario.cpp
    tests/integration/test_derived.cpp
    tests/integration/test_golden.cpp
  )
  target_include_directories(expansion_tests PRIVATE tests)
  target_link_libraries(expansion_tests PRIVATE expansion_core expansion_persistence expansion_presentation)
  target_compile_definitions(expansion_tests PRIVATE
    EXPANSION_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/content"
    EXPANSION_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    EXPANSION_REPLAY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/replays")
  add_test(NAME expansion_tests COMMAND expansion_tests)
endif()
