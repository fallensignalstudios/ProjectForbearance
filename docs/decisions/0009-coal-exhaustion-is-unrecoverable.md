# 0009 — Coal exhaustion on Homeworld is unrecoverable: a finding, not a decision

Status: **open finding**, raised for approval A04. No balance value has been
changed. This record exists so the contradiction stays visible.

## Context

Section 21.2 names the step after the neutral calibration precisely: "Follow it
immediately with a deliberately broken Coal-supply fixture and a readable
explanation." `tests/integration/test_coal_shortage.cpp` is that fixture. It idles
Homeworld's Coal Extraction Site and changes nothing else.

Section 8.3 states an intent: "Solar is intentionally sufficient to permit partial
water/agriculture/mining recovery after a fuel shortage. Do not add a hidden fuel
requirement to water or an unbreakable power/fuel dependency."

Section 21.1 names the matching risk, "Unrecoverable bootstrap loops", with the
control "Solar fallback, financed first port, visible relief/loss path", and the
evidence that would change the plan: "A recoverable shortage requires goods only
obtainable through the broken system."

## What the fixture shows

Homeworld consumes 22 Coal a day: 8 for the Thermal Generator, 8 for the Steel
Foundry, 6 for the Fuel Refinery. From an opening 240, the stockpile lasts until
day 11.

| Day | Coal | Generated | To facilities | Generator's reason |
| --- | --- | --- | --- | --- |
| 10 | 20 | 110 | 68 | full throughput |
| 11 | 0 | 110 | 65.3 | full throughput |
| 12 | 0 | 20 | 0 | short of an input material |

From day 12 the only supply is the Colony Hub's 20 passive solar. A thousand
residents need exactly 20 of protected residential power. Nothing is left, so no
facility runs, including the Coal Extraction Site whose output is the only thing
that would restore generation.

Restarting the mine at that point changes nothing, and the tests record it:
`coal_exhaustion_is_a_dead_end_on_homeworld`. The loop is at least legible. The
mine reports a power shortfall, the generator reports a missing input, and neither
is described as "efficiency".

Acting one day earlier recovers completely, with no lasting damage:
`coal_shortage_recovers_while_stock_remains`. So the economy has breathing room, but
the window closes the day the stockpile reaches zero.

## Why no value was changed

The two constraints cannot both hold with the seed as written.

Section 9.3 pins fourteen exact one-day figures, among them 110 power generated as
20 solar plus 90 thermal, and 20 of residential demand. Raising the Hub's solar
output, or lowering residential demand per resident, satisfies Section 8.3's intent
but breaks the calibration that Section 9.3 states exactly and that test T01 checks
to the unit.

Section 9.3's figures are the one hard arithmetic contract in the design. Section
8.3's sentence is an intent. Changing either is a design decision, and A04 is
explicitly the approval that revises the numerical seed after these tests. Choosing
silently between them is what the conflict-and-change rule in Section 1.3 forbids.

## Options for A04

1. **Raise the Hub's passive solar** to about 40, enough to leave headroom for the
   Waterworks and one Extraction Site after residential demand. Section 9.3's
   generated and spare power figures move, so the calibration must be restated.
2. **Add a distinct emergency supply** that is not part of the neutral fixture, for
   example a small always-on reserve at the Hub that only serves facilities when
   thermal generation is zero. Section 9.3 is untouched; a new concept enters P1.
3. **Accept the dead end** and rely on the relief contract and the loss path to make
   it visible rather than mysterious. Section 8.3's sentence would then need
   rewording, because the shipped seed does not honour it at Homeworld's population.
4. **Rely on the second world.** Frontier's 100 colonists need 2 of residential
   power against the same 20 of solar, so the intent already holds there. Homeworld
   could import Coal. This makes the sector genuinely interdependent, which suits
   the prototype's question, but it does not help before the colony exists.

A note in favour of option 1 or 2: the dead end is reachable in the first eleven
days, before a player has any colony, any freight, or any relief contract, and
before the shortage news even opens. The hysteresis reports a shortage on its second
missed day, which is the day after recovery became impossible.
