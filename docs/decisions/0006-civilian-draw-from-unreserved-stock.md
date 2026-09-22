# 0006 — Civilian needs draw from unreserved stock

Status: accepted.

## Context

Section 6.1 requires that `available = on_hand - sum(valid stock reservations)`,
that total reservations never exceed on-hand, and that a reservation has one owner
and is released or consumed exactly once. Section 6.3 says a reserve floor protects
automatic export, not civilian use.

A reserve floor and a stock reservation are different things. A floor is a policy on
what the route may load. A reservation is a committed claim by a named owner:
construction escrow before it moves, a ship booking, an expedition, a mission.

The design does not say which of on-hand or available civilian consumption draws
from.

## Decision

Civilian Food and Water consumption draws from `available`, not from `on_hand`. So a
reservation is honoured against civilian demand.

Reserve floors are unaffected and continue to protect only export, as specified: the
floor is applied when computing a loadable manifest and never when serving
civilians.

## Consequences

The reservation invariant stays exact and one-owner, which is what makes T02 and the
commit-time checks meaningful. Nothing can be consumed twice, and a shortage is
always traceable to a named owner rather than to an unexplained shortfall.

The visible cost is that a player who reserves a large shipment can starve a world
while food sits reserved for the dock. That is a real consequence of a player
decision, it is reported as an ordinary shortage against the owning booking, and the
route plan is editable at any time. The alternative, letting civilians eat reserved
stock, would silently invalidate a committed booking and make the ledger unable to
explain where the cargo went.

P1 has no automatic booking: a shipment is reserved only when the player's route or
mission commits it, and the reservation is short-lived.
