# How heat behaves

Not a task — the behaviour every task on this site rests on. If you understand
this page, the smelting and coking pages stop being recipes to follow and start
being obvious.

Every cell in the world carries a temperature, including air. Heat moves between
neighbours, and what each material does at what temperature is on its own page.

## Conduction is a property of the destination

Each material has a number for **how readily heat crosses into it**. It runs
from 0 to 255 and it is the single most important number for anything you build
around a fire.

| Material | Conducts |
|---|---|
| [Iron](../materials/iron.html) | 255 |
| [Water](../materials/water.html) | 180 |
| [Stone](../materials/stone.html) | 85 |
| [Ceramic](../materials/ceramic.html) | 18 |
| [Refract](../materials/refract.html) | 4 |

Air is deliberately low, or a fire would flash-heat a whole room.

That table explains the single commonest failure in this game: **what you build
a wall out of decides whether the thing behind it ever gets hot.** An iron wall
cooks what is behind it. A refractory wall is designed not to — it is furnace
lining, and its whole job is keeping heat away from what is on the other side.
Line a retort with it and the retort sits cold while the fire roars.

Refractory is not a better wall. It is an *opposite* wall, and it is right when
you want to contain heat and wrong when you want to transmit it.

## Thermal mass is a separate axis

A second number says how much heat a material *holds*, as a doubling: 2×, 4×,
8×. It is not the same thing as conduction, and separating them is what lets
lava heat everything it touches at full rate while staying molten for a long
time.

Practically: something with high thermal mass stays hot long after you stop
heating it. [Coke](../materials/coke.html) holds 4×, which is part of why a coke
bed is worth building.

## Temperature decides what a material *is*

Materials change into other materials at thresholds. There are four kinds, and
every material page lists whichever it has:

- **Ignites at** — it catches fire and becomes something burning
- **Melts or boils at** — it becomes a liquid or a gas
- **Freezes or sets below** — the reverse
- **Decays** — it falls apart over time, regardless of heat

The melting and setting points are deliberately **not the same number**. Molten
iron appears at 190 °C and does not set until it drops below 160 °C, so it keeps
flowing for a while after you stop heating it. Every chain like this is walkable
by clicking, from any material page.

## A fire is a material, not an effect

This is the thing that makes the simulation make sense. Fire, embers and lava
are *materials* with temperatures, sitting in cells. That is why:

- burning different things gives different temperatures (130 °C to 215 °C)
- a fire spreads by heating its neighbours past their ignition points
- water quenches fire by touching it
- you can build a heat source the same way you build a wall

There is no "furnace" object doing the work. There is a hot material next to a
cold one.

## Where to go from here

- [Making fire](making-fire.html) — starting one at all
- [Ore into bars](smelting.html) — the two tables that matter
- [Coke, and the sealed retort](coke-retort.html) — the process that needs all
  of this at once
