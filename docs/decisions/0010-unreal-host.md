# 0010 — Unreal Engine 5 as the presentation host, with the simulation compiled from source

Status: **accepted** for approval A01. Supersedes nothing; A01 was left open in
the technical design as "choose the presentation host".

## The decision

Unreal Engine 5 with UMG hosts the presentation layer. The simulation is
**compiled from source into the engine module**, through one translation unit, and
is not linked as a prebuilt library.

## Why the simulation is compiled rather than linked

A static library has to agree with whatever consumes it on the C++ ABI, the
standard library implementation and version, the runtime (`/MT` against `/MD` on
Windows), the exception model, and the optimisation and sanitiser flags. Unreal
sets all of those itself, per platform and per configuration, and changes them
between engine versions. A library built by this repository's CMake would have to
match, for every platform shipped, and the failure mode when it does not is a
link error at best and a corrupted `std::string` at worst.

Compiling the same sources with the engine's own toolchain removes that entire
class of problem. It also means there is one build of the simulation, not two that
can drift: a change to the resolver is in the engine build the moment it is in the
repository.

The cost is that the engine compiles 22 sources it does not own. That is a few
seconds, once, and cached thereafter.

## Why one translation unit

UnrealBuildTool compiles the `.cpp` files it discovers under a module directory.
The simulation deliberately lives outside any engine module — it must build with
no engine present at all, which is what keeps the core testable and the
determinism verifiable. There is no supported way to point UnrealBuildTool at
sources elsewhere.

So `hosts/unreal/Source/ExpansionUE/Private/ExpansionAmalgamated.cpp` includes
each simulation source in turn. It is the only `.cpp` UnrealBuildTool needs to
find, and the include paths in `ExpansionUE.Build.cs` make the sources' own
relative includes resolve.

Sharing one translation unit is not free, and the costs are concrete rather than
theoretical:

* Two file-static helpers with the same name become an ambiguity. This is not a
  hypothetical: building this arrangement immediately found one, between
  `expansion::sim::recipe_of` and a new static `recipe_of` in `view_models.cpp`.
  The static was renamed.
* A macro one source defines reaches every source after it.
* A `using` directive leaks likewise.

None of that is acceptable to discover inside an engine, months later. So the
arrangement is built and tested **here**:

* `expansion_amalgamated` compiles the single translation unit under the same
  `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror`.
* `expansion_tests_amalgamated` runs the entire verification suite against it,
  including the golden replay journals. All 114 tests pass and every canonical
  hash matches the per-file build, so the arrangement produces the same
  simulation and not merely one that compiles.
* `tools/check_amalgamation.py` fails when the source tree, `CMakeLists.txt` and
  the translation unit disagree about which sources exist.

All three run in CI. A source added to two of the three places fails immediately
rather than in an engine nobody can run here.

## What the engine module does and does not contain

| Concern | Where it lives |
| --- | --- |
| The simulation, saves, views | `core/`, `persistence/`, `presentation/` — compiled into the module |
| Content bytes | Read through `ContentSource`; the module implements one over the engine's file layer, so a pak works identically to a directory |
| Save files | Written through the engine's `ProjectSavedDir`, not through `hostfs/` |
| Time | `host::TickScheduler` — integer microseconds, fixed step, no economic quantity from a frame delta |
| Change notification | `host::ChangeTracker` — one delegate carrying which panels moved |
| Everything Blueprint-facing | `FExpansion*` structs, copied per notification |

`hostfs/` is deliberately excluded from the module. It is the only code in the
repository that opens a file, and a packaged build on a console has no writable
path of the shape it assumes. Excluding it is what keeps the claim "the core never
touches a filesystem" true in the build that ships rather than only in the tests.

## Exceptions

`ExpansionUE.Build.cs` sets `bEnableExceptions = true`. This is not a relaxation
of the boundary; it is what implements it.

The core raises `SimError` internally — that is how it keeps overflow and
invariant violations from becoming silent wrong answers. `api.hpp` and the view
builders catch every one and return a typed `Outcome`. Those catch handlers are
compiled *into this module*, because the simulation sources are. Without
exceptions enabled they do not compile, and the choice would be between a core
that cannot check itself and a throw reaching engine code, which in a packaged
build is a crash rather than an error.

Nothing throws outward. The host module's own code contains no `try` and no
`catch`: every simulation call it makes returns an `Outcome` or a
`CommandResult`. That was checked by counting them.

## What is verified and what is not

Verified in this repository, on every build:

* the single-translation-unit arrangement compiles under strict warnings;
* it passes all 114 tests, with canonical hashes identical to the per-file build;
* the three source lists agree;
* the simulation is free of any filesystem dependency, because `hostfs/` is a
  separate library and the core does not link it;
* the portable-math path (`EXPANSION_FORCE_PORTABLE_MATH`) produces identical
  hashes, which is what MSVC needs, having neither `__int128` nor
  `__builtin_*_overflow`.

**Not verified: any of the engine-facing code.** `ExpansionUE.Build.cs`, the
`USTRUCT` definitions, and `UExpansionSessionSubsystem` have never been compiled.
There is no engine in this container. Every file states that at its head, and
`docs/unreal_integration.md` lists what to check first when an engine is
available.

## Alternatives considered

1. **A prebuilt static library per platform**, in a ThirdParty module. Standard,
   supported, and the toolchain-matching problem above is exactly why it was not
   chosen. It would also give two builds of the simulation that can drift.
2. **Copying the sources into the module directory** at generate time, so
   UnrealBuildTool discovers them normally. No single-translation-unit hazards,
   but it puts generated copies of the simulation in the tree, and a stale copy
   is a silent wrong answer rather than a build failure.
3. **A C ABI shim and a shared library.** Immune to ABI mismatch, at the cost of
   flattening every view struct into a C-compatible shape and marshalling across
   it. The view layer would have to be written twice.
4. **Godot, or a custom renderer.** Not evaluated on merit; the project's target
   platforms and the team's existing tooling are Unreal.

Option 2 remains the fallback if a platform's toolchain cannot compile the single
translation unit. The check script and the amalgamated test target exist partly so
that switching to it later is a change of mechanism and not of behaviour.
