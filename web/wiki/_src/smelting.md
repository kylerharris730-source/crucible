# Ore into bars

Smelting here is not a recipe. It is the heat simulation doing its ordinary job:
you get the ore hot enough and it melts, and when the melt cools it sets as
metal. There is no furnace UI and no progress bar — there is a temperature, and
either you reach it or you do not.

That makes one table the whole of this page.

## You will need

- Ore, mined
- A heat source hot enough for *that* ore — see the table
- Somewhere the melt can pool without running off

## Steps

1. **Build a pit or a basin** out of something that does not melt.
   [Stone](../materials/stone.html) is fine and conducts heat well (85 of 255);
   [Iron](../materials/iron.html) is better still at 255.
2. **Put the ore in it.**
3. **Heat it from outside** with a fuel hot enough for that ore.
4. **Wait for it to run.** The ore becomes a molten metal, which is a liquid and
   flows.
5. **Let it cool.** Below its setting point the melt turns solid, and you mine
   it as metal.

## When it works

**What each ore needs, and what it becomes:**

| Ore | Melts at | Becomes | Sets below |
|---|---|---|---|
| [Tin](../materials/tinore.html) | 120 °C | [Tin Melt](../materials/tinmelt.html) | 105 °C |
| [Gold](../materials/goldore.html) | 160 °C | [Gold Melt](../materials/goldmelt.html) | 120 °C |
| [Copper](../materials/cuore.html) | 165 °C | [Molten Copper](../materials/moltcopp.html) | 140 °C |
| [Iron](../materials/feore.html) | 190 °C | [Molten Iron](../materials/moltiron.html) | 160 °C |
| [Titanium](../materials/tiore.html) | 208 °C | [Ti Melt](../materials/timelt.html) | 175 °C |
| [Tungsten](../materials/wore.html) | 211 °C | [W Melt](../materials/wmelt.html) | 195 °C |

**What each fuel reaches:**

| Burn | Sits at | Which is enough for |
|---|---|---|
| [Wood Ember](../materials/wood-ember.html) | 130 °C | tin |
| [Fumarole](../materials/fumarole.html) | 150 °C | tin |
| [Ember](../materials/ember.html) (coal) | 185 °C | tin, gold, copper |
| [Fire](../materials/fire.html) (leaves, cotton, rope) | 205 °C | + iron |
| [Fuel Fire](../materials/fuelfire.html) | 202 °C | + iron |
| [Coke Ember](../materials/coke-ember.html) | 215 °C | **everything** |
| [Lava](../materials/lava.html), [Brimfire](../materials/brimfire.html) | 215 °C | everything |

Read the two tables against each other and the progression falls out of them.
**Coal takes you to copper and stops.** Its embers sit at 185 °C and iron needs
190 — five degrees short, and no amount of patience closes it. **Fuel fire at
202 °C gets you iron and steel and stops**, because titanium needs 208. **Only
coke ember at 215 °C reaches titanium and tungsten**, and coke is not something
you find. You make it, in a sealed vessel, and nothing in the game tells you
how — see [Coke, and the sealed retort](coke-retort.html).

The cheap surprise: burning *leaves, cotton or rope* gives open fire at 205 °C,
hotter than coal. It is the cheapest route to iron and it burns out fast.

## When it does not

**The ore sits there and nothing happens.** Your fire is not hot enough. Check
both tables. This is almost always the problem, and waiting longer never fixes
it — a cell settles at the temperature its neighbours can deliver.

**The melt ran away down a tunnel.** Molten metal is a liquid with real flow.
Build the basin before you light the fire.

**The metal set somewhere you cannot reach.** Same cause. Note the setting
points are *lower* than the melting points — molten iron stays liquid down to
160 °C — so it keeps running for a while after you stop heating.

**Your basin melted too.** Check what you built it from. Stone melts to lava at
185 °C, which is below several of the temperatures on this page.

**You used refractory to line it and it never got hot.**
[Refract](../materials/refract.html) conducts heat at 4 of 255. It is furnace
*lining* — designed to keep heat away from what is behind it. That is right for
a furnace shell and exactly wrong for a wall you are trying to heat something
through.

## Next

[Coke, and the sealed retort](coke-retort.html) — the process that takes you
past iron, and the one the game never mentions.
