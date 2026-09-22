# Recorded runs

Each `.script` file is a worked solution for a shipped scenario, expressed only
in player commands. Each `.journal.json` is the accepted command journal that
script produced, together with the canonical day hash of every committed day.

The journals are golden fixtures. A change to the simulation, the balance seed or
the catalog that alters any committed day will change these hashes and fail
`golden_replay`. That is the intended signal: it forces a balance or rule change
to be acknowledged rather than absorbed silently (technical design, Section 19.4).

To refresh them deliberately after an approved change:

    build/dev/expansion run --content=content --faction=dominion \
        --script=tests/replays/first_dependency_dominion.script --quiet \
        --journal=tests/replays/first_dependency_dominion.journal.json

`first_dependency_collapse.script` is the deliberately broken counterpart: it
idles the water supply, drives the colony into a survival emergency, spends the
one relief contract and then loses. It exists so the explicit loss path is
reproducible, not because a real player would follow it.
