# Bringing this simulation up in Unreal Engine 5

This is the handoff document. It says what exists, what has been verified, what
has not, and the order to do things in when you have an engine in front of you.

**Read this first: none of the engine-facing code in `hosts/unreal/` has ever been
compiled.** This repository builds with CMake and Ninja in a container with no
engine installed. Everything under `hosts/unreal/` is written from the engine's
documented constraints and is unverified by any compiler. Everything under
`core/`, `persistence/` and `presentation/` is verified, including the exact
arrangement the engine module uses to compile it — see "What is already proven".

---

## What is already proven, here, without an engine

Run `cmake --build build/dev && ctest --test-dir build/dev`. Two suites run:

| Target | What it proves |
| --- | --- |
| `expansion_tests` | The simulation, compiled one file at a time: 114 tests |
| `expansion_tests_amalgamated` | The same 114 tests against the **single translation unit the engine module compiles** |

Both pass, and the golden replay tests inside them compare canonical day hashes.
So the engine's compilation arrangement produces the same simulation, not merely
one that links. That is the single most useful thing verified here, because it is
the thing that would otherwise fail first and most confusingly.

Also verified:

* `tools/check_amalgamation.py` — the source tree, `CMakeLists.txt` and
  `ExpansionAmalgamated.cpp` list the same 22 sources.
* GCC and Clang produce identical canonical hashes for all four recorded runs.
* `EXPANSION_FORCE_PORTABLE_MATH` produces identical hashes too. MSVC has neither
  `unsigned __int128` nor `__builtin_add_overflow`, and `wide_math.hpp` has a
  portable path for exactly that; it is not a fallback nobody has run.
* The core links no filesystem code. `hostfs/` is a separate library, excluded
  from the engine module by design.

## What is not verified

Everything in `hosts/unreal/`:

* `ExpansionUE.Build.cs` — module rules, include paths, `bEnableExceptions`.
* `ExpansionUE.uplugin` — plugin descriptor.
* `ExpansionTypes.h` — the `USTRUCT` and `UENUM` mirrors.
* `ExpansionSessionSubsystem.h/.cpp` — the subsystem, the tick, the delegates,
  the content source over the engine's file layer, the save paths.

Expect to fix names: engine API surface moves between versions, and these were
written against UE 5.3–5.5 as documented rather than as compiled.

---

## Bring-up order

Do these in order. Each step's failure is diagnosable on its own; skipping ahead
makes two failures look like one.

### 1. Make a project and drop the plugin in

```
MyProject/
  MyProject.uproject
  Plugins/
    ExpansionUE -> <this repo>/hosts/unreal      (symlink, or copy)
```

The plugin's `Build.cs` computes the repository root as four directories up from
the module, so a symlink keeps the include paths correct. A copy does not — fix
`Repo` in `ExpansionUE.Build.cs` if you copy.

### 2. Put the content where the module looks for it

```
hosts/unreal/Content/Definitions/    <- the contents of <repo>/content/
```

Symlink or copy `content/` there. `LoadCatalog()` reads
`<plugin>/Content/Definitions` recursively for `*.json`.

Two things matter about this directory:

* **The catalog hash is over the bytes of every definition file.** A save loads
  only against an exactly matching catalog in P1. If the packaging step rewrites,
  reformats or re-encodes a `.json`, the hash changes and every save becomes
  `IncompatibleSave`. Ship these as raw files, not as cooked assets.
* **`content/schemas/` is skipped by the loader** (it is documentation), so it
  does not affect the hash. Copy it or not.

Verify the hash matches before going further:

```
./build/dev/expansion validate --content=content          # prints the catalog hash
```

then compare against what `LoadCatalog()` produces in the engine. If they differ,
the file set or the bytes differ — not the loader.

### 3. Compile

The first compile is where the unverified code fails. Expect, in rough order of
likelihood:

* Engine header paths that have moved (`Misc/App.h`, `HAL/FileManager.h`,
  `Interfaces/IPluginManager.h`).
* `FTickableGameObject` requirements — `GetStatId`, `IsTickable`, and whether your
  engine version wants `GetTickableTickType`.
* `int64` in a `UPROPERTY`. Blueprints support it, but an old plugin setting or an
  older engine may not; if so, the fix is to keep the `int64` in C++ and expose a
  formatted `FText` plus an `int32` where the range genuinely fits (a basis-point
  value always does; a `Milli` quantity does not).
* Unity build merging the module's sources with each other. That is fine — the
  simulation already builds as one unit. If it merges with *engine* translation
  units and something collides, set `bUseUnity = false` for this module.

If the simulation sources themselves fail to compile, that is information worth
keeping: they build clean here under stricter warnings than Unreal's default, on
two compilers, including as one translation unit. A failure means a platform or
toolchain difference, so write down which.

### 4. Check the boundary holds

Before building any interface:

1. `LoadCatalog()` returns `bOk` and the hash matches step 2.
2. `NewSession("first_dependency", "dominion", 0)` returns `bOk`.
3. Call `StepDay()` 120 times and compare the canonical hash against the
   command-line host running the same scenario:
   ```
   ./build/dev/expansion run --content=content --scenario=first_dependency \
       --faction=dominion --days=120 --save=/tmp/s.scexp
   ./build/dev/expansion hash --content=content --save=/tmp/s.scexp
   ```
   These must be identical. If they are not, stop: something about the engine's
   build has changed the arithmetic, and no interface work is worth doing until
   that is understood. `expansion verify` and the golden journals in
   `tests/replays/` are the tools for narrowing it.
4. Save to a slot, load it back, compare hashes again.

Only then start on widgets.

---

## The three rules a widget must follow

These are not style preferences. Each one prevents a specific failure the design
is built to avoid.

### Never bind a number through a per-frame getter

Bind `UExpansionSessionSubsystem::OnChanged`. It fires once per committed change
and carries `FExpansionChangeSet`, naming which panels moved: the sector header,
freight, decisions, history, and the specific planets and facilities. Rebind those
and nothing else.

A per-frame attribute binding on thirteen facility cards, each assembling a view,
is how this kind of interface becomes slow — and it is slow in a way that looks
like the simulation's fault when it is not. The simulation resolves a day in
0.35 ms.

`GetProgressToNextDay()` is the exception: it is the one thing safe to read per
frame, because it is arithmetic on an integer accumulator and touches no state.

### Never do arithmetic on a displayed quantity

Every quantity arrives as an exact `int64` and the text the simulation rendered
for it. Print the text.

Do not divide the integer by `1000.0f` to make your own string. The entire economy
is integer arithmetic precisely so that no two readers disagree in the last digit,
and a float conversion in a widget reintroduces exactly what that was for. If you
need a number you were not given, ask: `FormatQuantity(int64)` and
`FormatPercent(int32 BasisPoints)` are `BlueprintPure` and call the simulation's
own formatter.

Basis points divide by 10000 for a progress bar. That is the one division allowed,
and it is exact.

### Never explain a shortfall in your own words

Every facility card carries `ReasonId` (typed) and `ReasonText` (plain language,
from the simulation's own table). Show `ReasonText`. Branch styling on `ReasonId`.

The design is explicit that a labour shortfall must not be summarised as
"efficiency" and must not be reported as missing material. A widget that writes its
own explanation will eventually write the wrong one. The same applies to
`FExpansionConcern::Cause` and to a rejected command's `Reason` and `Detail`: a
rejection carries the exact deficit, and showing it beats greying out a button.

---

## Time

`host::TickScheduler` converts frame deltas into whole day steps. Ten seconds is
one day at 1x. It accumulates in **integer microseconds**, not seconds, so the day
boundaries of a frame sequence are the same at 30, 60, 72 and 144 fps and on every
platform — a float accumulator drifts as deltas are summed and subtracted, and
"ten seconds is one day" becomes only approximately true, differently per machine.

Four behaviours matter to a host:

* At most four day steps run per frame; the remainder is retained but capped at one
  further day. A host that stalls resumes where it was, not sixty days later.
* A single delta longer than half a second is treated as a stall, not elapsed play.
* `suspend()` on focus loss discards the partial day. Time spent suspended is not
  play, and replaying it as catch-up is the bug this prevents.
* `hold_for_decision()` holds the calendar before the next day when a critical
  decision opens. The subsystem calls it automatically and releases it when the
  player answers or picks a speed.

No economic quantity is ever derived from a frame delta. The only thing wall time
decides is *when* `StepDay` is called.

---

## Saves

The save envelope is `SCEXP`, written by `persistence/`, and it is portable
between the command-line host and the engine — same bytes, same checksum, same
required exact catalog match. The subsystem writes to
`<ProjectSavedDir>/Expansion/slot<N>.scexp` through the engine's file layer rather
than through `hostfs/`, because a console has no writable path of the shape
`hostfs/` assumes.

Two failures are distinct on purpose and a player needs to be told which:

* `IncompatibleSave` — the save was written against a different catalog or
  simulation version. Nothing is wrong with the file.
* `CorruptSave` — the file is truncated, mistyped, or fails its checksum.

Do not collapse these into "could not load".

---

## Known gaps

* **Open finding 0009** — coal exhaustion on Homeworld is unrecoverable. Raised
  for approval A04, no balance value changed. See
  `docs/decisions/0009-coal-exhaustion-is-unrecoverable.md`. It will be reachable
  in play; decide it before shipping.
* **Open finding 0011** — an event instance records no facts about what opened it,
  so a decision card has no provenance to show and names its subject instead. See
  `docs/decisions/0011-condition-trigger-provenance.md`.
* **No interface content.** There are no widgets, no Blueprints, no art. The
  subsystem is the whole host. Which panels exist and how they look is the next
  piece of work and this document says nothing about it.
* **No audio, no localisation beyond the string table.** `presentation/src/text.cpp`
  holds every display string in one place, which is where a localisation pass would
  start; nothing reads a `.po` or an Unreal string table yet.

---

## Where to look in the repository

| You want | Read |
| --- | --- |
| The host boundary, non-throwing | `core/include/expansion/api.hpp` |
| The view structs a widget binds | `presentation/include/expansion/view_models.hpp` |
| Time and change notification | `core/include/expansion/host_services.hpp` |
| Why the module is built this way | `docs/decisions/0010-unreal-host.md` |
| What a day does, phase by phase | `docs/Expansion_TDD.md` §5.2 |
| Which requirement is verified where | `docs/acceptance_matrix.md` |
| Every typed reason a widget may show | `core/include/expansion/reasons.hpp` |
