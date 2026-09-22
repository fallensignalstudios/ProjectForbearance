# 0003 — A neutral verification faction profile

Status: accepted.

## Context

Section 9.3 gives an exact one-day calibration for Homeworld and states that the
fixture "deliberately has no faction, policies, events, transfers, or construction",
and that playable scenarios apply Dominion or Reformation and will produce different
values.

Section 12.2 gives Dominion a 10% Industry throughput modifier under which inputs,
power and outputs rise together. Creating a session requires a faction. Under
Dominion, six of the fourteen calibration figures move: Steel, Machinery and Fuel
output rise, the ore and coal the Industry recipes consume rise with them, and
facility power demand rises by three units.

So the calibration cannot be checked through the real resolver while a playable
faction is selected, and neither faction is neutral.

## Decision

The catalog carries a third profile, `neutral_calibration`, marked `fixture_only`.
It has no modifiers, and the loader rejects a `fixture_only` profile that carries
any. `Catalog::playable_factions()` excludes it, and `expansion validate` selects it
for the calibration.

## Consequences

`expansion validate` reproduces all fourteen Section 9.3 figures exactly through the
same resolver that plays the game, rather than through a separate arithmetic script.
The first run of this check is what revealed that the figures were being compared
under Dominion.

A host must not offer `fixture_only` profiles as playable choices. `playable_factions()`
exists so that is a one-line filter rather than a convention. Section 2.2's "two
faction profiles" is unchanged: this is a verification fixture, not a third ideology.
