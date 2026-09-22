# Sovereign Call: Expansion

An engine-independent simulation kernel for the two-world dependency prototype
described in *Sovereign Call: Expansion — Technical Design Document, Prototype
Baseline v0.1*. The core is C++20 with no third-party dependency, a headless
command-line host, and JSON-authored content.

The prototype's question is not whether every meter exists. It is whether a
player can diagnose one shortage, remedy it, found a colony, support it by
freight, receive a useful return shipment, and understand an obligation that
competes for the same transport.

## Build and test

Nothing outside a C++20 compiler and CMake 3.20 is required. The core builds with
no game engine installed.

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Or without presets:

```
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/dev
./build/dev/expansion_tests
```

`ctest` runs the suite twice: once against the simulation compiled a file at a
time, and once against the single translation unit the Unreal module compiles.
Both runs are 114 tests, including the acceptance matrix of Section 18.2, the
deliberately broken Coal-supply fixture, and four recorded runs replayed as golden
fixtures. A single test can be selected with `--filter=t05`.

GCC and Clang builds produce identical canonical hashes for every recorded run, as
do the single-translation-unit build and the portable-math build. Continuous
integration builds all of them with warnings as errors.

## Run the simulation

```
# Validate the catalog and reproduce the neutral one-day calibration.
./build/dev/expansion validate --content=content

# Play a recorded solution and watch the news as it happens.
./build/dev/expansion run --content=content --faction=dominion \
    --script=tests/replays/first_dependency_dominion.script

# Save, then inspect any view.
./build/dev/expansion run --content=content --faction=reformation --days=40 --save=/tmp/slot.json
./build/dev/expansion show --content=content --save=/tmp/slot.json --view=planet --planet=homeworld
./build/dev/expansion show --content=content --save=/tmp/slot.json --view=facility --facility=1
./build/dev/expansion show --content=content --save=/tmp/slot.json --view=freight
./build/dev/expansion show --content=content --save=/tmp/slot.json --view=history
./build/dev/expansion show --content=content --save=/tmp/slot.json --view=ledger --day=40

# Write a local diagnostic export of the whole run: daily metrics, dispatches,
# freight movements and the final report.
./build/dev/expansion export --content=content --save=/tmp/slot.json --out=/tmp/run.json

# Arrival-aware forecast on a clone. The live state is untouched.
./build/dev/expansion forecast --content=content --save=/tmp/slot.json --days=30

# Replay a journal and compare canonical day hashes.
./build/dev/expansion replay --content=content --journal=tests/replays/first_dependency_dominion.journal.json
```

`expansion help` lists every verb.

## What the repository contains

| Path | Owns |
| --- | --- |
| `core/` | Units, JSON, the validated catalog, authoritative state, the daily resolver, commands, the canonical state codec |
| `persistence/` | The save envelope, slot safety, the replay journal and difference reporting |
| `presentation/` | Read models, the structured views a widget binds, and every display string |
| `hostfs/` | The only code that opens a file. A packaged engine build leaves it out |
| `hosts/unreal/` | The Unreal Engine 5 presentation host. **Compiled by nothing yet** — see `docs/unreal_integration.md` |
| `cli/` | The headless host: validation, scenario runs, inspection, forecasting, replay |
| `content/` | The authored catalog and the numerical seed, plus schema documentation |
| `tests/` | Unit and integration tests, seeded fixtures, recorded runs |
| `docs/` | The implementation contract, the acceptance matrix and the decision record |

## Implementation state

The technical design distinguishes five evidence levels: Defined, Implemented,
Unit-verified, Integrated and Playtested. This repository reaches **Unit-verified**
for the simulation kernel and its acceptance matrix. It is not Integrated in a
game engine and not Playtested with players.

Delivered, with tests: the ten-phase daily resolver; workforce, power, recipes,
needs, condition and construction; the single freighter, ports, manifests,
reservations and round-trip fuel; the colony expedition and founding; policies,
faction adherence, the accident chain, facts-based news; the strategic mandate,
the relief contract and the loss path; the completion predicates; canonical save,
load, replay and forecast; and one recorded Complete run per faction.

Also delivered, for the engine host: a fixed-step clock that accumulates in
integer microseconds, so ten seconds is one day at 1x on every platform and a
focus loss creates no catch-up debt; change tracking that reports which panels a
committed change invalidated, so a host rebinds those and ticks nothing; and
structured views with a label, an exact integer and the rendered text for every
number a widget shows.

Not delivered: anything that requires an engine to compile. `hosts/unreal/` holds
the module rules, the Blueprint-facing types and the session subsystem, and none
of it has been compiled by anything — there is no engine in the build environment.
What *is* verified is the arrangement it uses to compile the simulation: the single
translation unit builds under the same strict warnings and passes all 114 tests
with identical hashes. `docs/decisions/0010-unreal-host.md` explains why that
arrangement was chosen and `docs/unreal_integration.md` is the bring-up order.

Also not delivered: any packaged Windows build; a graphical interface, so
accessibility and the comprehension gate remain untested; a performance benchmark,
because no reference machine has been named; and every system Section 20 defers,
including population growth, which is exactly zero.

`docs/acceptance_matrix.md` records the evidence level of each test individually,
and two open findings.

**Coal exhaustion on Homeworld is unrecoverable**, because the Hub's solar output
exactly equals protected residential demand. That needs a balance decision, not a
code change, and the options are in
`docs/decisions/0009-coal-exhaustion-is-unrecoverable.md`.

**An event instance records nothing about what opened it.** `trigger_facts` is
declared, saved and hashed, and nothing populates it, so a decision card has no
provenance to show. Fixing it changes the canonical hash and would require
re-recording every golden journal, so it is written up rather than done:
`docs/decisions/0011-condition-trigger-provenance.md`.

## Authority

Numbers, thresholds and formulas in `content/` are the technical design's proposed
seed, not approved canon. They are a starting hypothesis to be revised after
testing. `docs/decisions/` records every place this implementation adds to or
narrows the design, so a difference is visible rather than absorbed.

The two prototype worlds are called Homeworld and Frontier. They are test labels,
not canonical locations.
