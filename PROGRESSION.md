# Crucible — progression plan

How every material and item comes to be made, what gets added to fill the gaps,
and the enemies that put pressure on it.

This is a **build plan**, not a spec. It extends [DESIGN.md](DESIGN.md) — read
that first, particularly §2 (weapons fire materials) and §3 (progression gates
are already in the material table). Both are decisions already taken, and most
of what follows is finishing them rather than inventing anything.

---

## 0. Read this before writing code

Five things in this codebase will break **silently** if you get them wrong.

1. **`MatId` is append-only.** New materials go immediately before `MAT_COUNT`,
   never inserted among related ones. Save files store raw numeric material ids;
   inserting shifts every id after it and quietly corrupts every existing world.
   `MAT_ALUMINUM_NITRIDE` is already parked at the end for exactly this reason
   and says so. Grouping by theme is what the *comments* are for.

2. **Worldgen never touches the global rng.** Everything derives from
   `hash1()`/`fbm()` on position or index. A single `rngNext()` in generation
   makes the world depend on how many random numbers were drawn before it — so
   it changes shape according to what the player did last session.

3. **Material properties live in standalone parallel tables**, not in `MATS`
   rows — `g_matStrength`, `g_matIsPlant`, `g_matConducts`, and ~20 more. That
   is deliberate (it avoids `-Wmissing-field-initializers` on every row). A new
   material means visiting the tables it belongs in; forgetting one gives a
   material that is, say, minable but not conductive, with no warning.

4. **`ItemDef` is memset to zero, and 0 is a real value** for `equipSlot` and
   `deviceType`. There is a documented past bug where every stack of stone
   became wearable. `kind` is the only field that decides what an item is.

5. **One cell of world is one unit of item.** Recipe quantities are in cells.
   Keep it 1:1 — the comment in `craft.cpp` explains why the arithmetic staying
   honest matters more than the tuning convenience.

**The house principle:** prefer a table row to a rule. Almost every mechanic
here — smelting, phase change, drops, slaking — is a column, not a branch in
`world.cpp`. Keep it that way.

---

## 1. Where we actually are

The gap is not "few recipes". The gap is that **the entire metallurgical half of
the game has simulation and no crafting.**

You can find copper ore, build a furnace out of ceramic, smelt it, watch the
slag float and the metal settle, break the crust, and collect copper — and then
there is *nothing to make with it*. All eight current recipes are wood and
crops.

| system | simulated? | craftable? |
|---|---|---|
| wood, rope, platform, door, torch | — | ✅ |
| crops, seeds, replanting | ✅ | ✅ |
| ore → slag + metal separation | ✅ | ❌ nothing consumes metal |
| clay → ceramic firing | ✅ | ❌ |
| coal → fuel slaking | ✅ | ❌ |
| ~30 tools, devices, flight items | — | ❌ all creative-only |
| graphene | ✅ (as a material) | ❌ **unobtainable at all** |

That last row is worth calling out: graphene is the endgame conductor, the only
material with no melting point, and there is currently no way to get any.

---

## 2. The organising principle

Adding "crafting stations" sounds like it contradicts DESIGN.md's thesis that
machines are *simulated, not recognised*. It does not, provided the line is
drawn in one specific place:

> **Simulation transforms matter. Crafting fabricates objects.**

- Turning ore into metal, clay into ceramic, sand into glass, two molten metals
  into an alloy, or anything into slag — that is **heat and chemistry**, it
  happens in a vessel you built out of pixels, and it stays simulated. There is
  no "smelter" you place.
- Turning a bar of metal into a *drill* — that is **fabrication**, it is not
  interesting to simulate, and it belongs in a menu at a bench.

This gives a clean answer to "how do I make every item": the **materials** come
out of the simulation, and the **objects** come off the bench. It also means the
station tier is a fabrication tier, never a heat tier — the furnace is always
yours, always pixels, and always the interesting part.

---

## 3. Three engine additions, before any content

These are prerequisites. Doing content first means redoing it.

### 3.1 Crafting stations

`craft.h` already predicts this exactly:

> *"When a furnace-only recipe arrives it wants a `station` field here and one
> more test in `craftCan()`, and that is a smaller change than pre-building the
> machinery for it now."*

Do precisely that and no more:

```c
enum CraftStation {
    STATION_HAND = 0,   /* anywhere, no station */
    STATION_BENCH,      /* wood + stone: shaping, joinery, simple parts */
    STATION_ANVIL,      /* forming metal stock into parts */
    STATION_CHEM,       /* acid work, anything that must be contained */
    STATION_ASSEMBLY,   /* devices, circuits, precision */
};
```

- One `u8 station` on `Recipe`; `STATION_HAND` is 0 so every existing recipe is
  unchanged by the memset rules.
- `craftCan()` gains one test: is a station of this type within a few cells?
- Stations are placed **devices** (`ITEMK_DEVICE`), so they reuse everything
  `device.h` already does — footprint, save, right-click.
- The menu greys out rows whose station is absent and says which is missing.
  This doubles as the tech tree's signposting, exactly as the recipe list
  already doubles as the tutorial.

### 3.2 Contact reactions — the keystone

The single most valuable engine addition, because **one mechanism buys acid,
alloys, and everything chemical afterwards.**

`g_matWetInto`/`g_matWetBy` is already a contact reaction — it is just hardcoded
to one pair (coal + steam → fuel). Generalise it:

```c
struct Reaction {
    u8 a, b;          /* the two materials that must touch */
    u8 intoA, intoB;  /* what each becomes; MAT_EMPTY consumes it */
    u8 chance;        /* out of 255, per contact per frame */
    u8 minTemp;       /* 0 for none */
};
```

Store as a `MAT_COUNT × MAT_COUNT` byte matrix indexing into a short reaction
list — at ~60 materials that is under 4 KB and O(1) per neighbour test, which
matters because `world.cpp` is already walking neighbours here.

Migrate coal+steam→fuel onto it as the first entry, so the new path is proven
against known-good behaviour before anything depends on it.

This yields:
- **Acid** dissolving materials by strength tier.
- **Alloys** — molten copper + molten tin → molten bronze, *in a vessel you
  built*. This is the mechanic that best expresses the whole thesis: mixing two
  molten metals is something the simulation can do and no crafting menu can.
- Rust, quenching variants, and anything chemical later, for free.

### 3.3 A recipe browser that scales

Eight recipes fit in a list. Seventy do not. Before adding sixty recipes:
categories, search, and scroll. There is precedent for both halves already —
the creative menu has a scroll window and the circuit signal picker has search.
Reuse them rather than inventing a third pattern.

---

## 4. The material ladder

Each tier's **gate** is a temperature or a hazard you cannot yet survive, and
its **unlock** is what lets you reach the next one. That is DESIGN.md §3 made
concrete.

Numbers below are intent, not tuning — measure and adjust, and note that
smelting points should sit *below* the pure metal's melting point, as copper and
iron already do.

### Tier 0 — surface *(have, plus glass)*

wood · stone · sand · dirt · clay · wheat/flax/cotton

**NEW — Glass.** Sand melts to molten glass, freezes to glass. Three new
materials (`MAT_GLASS`, `MAT_GLASS_MELT`, and see below).

Glass earns its place on one property: **`g_matOpacity` 0 while `KIND_STATIC`.**
It is the first solid you can see through, and this game has a real light field,
so that is a mechanic and not a decoration:

- Windows — light into a base without a hole in it.
- **A vessel you can watch the reaction inside.** A still with a glass wall is
  the single best advertisement for a simulated-machines game.
- Lenses (`ITEM_LENS` currently has no recipe and obviously wants glass).
- **Acid containment** — see tier 2. Glass must precede acid.

Cheap, early, and it makes the existing simulation *visible*. Do this first of
all the content.

### Tier 1 — shallow *(copper, coal, ceramic — plus tin and bronze)*

**NEW — Tin.** A shallow, scarce ore that exists only to alloy. Low melt.

**NEW — Bronze.** Molten copper + molten tin → molten bronze, via §3.2.
Stronger and less heat-fragile than copper, still melts low enough for an
early furnace. This is the **tutorial for alloying** and should be reachable
before iron so the mechanic is taught while stakes are low.

### Tier 2 — mid *(iron, fuel — plus steel, acid)*

**NEW — Steel.** Molten iron + carbon (coal) → molten steel. Higher melting
point and a strength between `STR_METAL` (150) and `STR_HARD` (210) — it wants
its own tier constant. The structural material for the mid game.

**NEW — Acid.** A `KIND_LIQUID` found in deep pockets, the same way lava
hotspots are placed (`naturalTemp` has the precedent for "a place, not a
puddle").

Acid dissolves by strength tier through the reaction table: it eats soil, stone
and soft materials, is slowed by metal, and is **held by glass, gold and
ceramic**. That containment constraint is the entire mechanic — you must have
glass before you can carry acid, and the puzzle of moving it is the content.

It gives the player a **chemical route past materials the thermal route cannot
touch**, which is a genuine second axis rather than a bigger number.

### Tier 3 — deep *(gold, titanium)*

**NEW — Gold.** Best electrical conductor in the game, **immune to acid**, and
deliberately *bad* at being structural: soft (`STR_SOFT`), low melting point.
It is not "better copper" — it is the wire that survives a corrosive
environment, and the contact material for anything that has to keep working
when a corroder has been through. Rare, in small pockets rather than veins.

**NEW — Titanium.** Light, `STR_HARD`, corrosion-proof, high melting point.
Armour, and the hull of the thing you leave on. Its smelting point is the gate
that requires a fuel-fired, ceramic-lined furnace — the first time the player
must build a *good* furnace rather than a working one.

### Tier 4 — deepest *(tungsten, graphene, refractory)*

**NEW — Tungsten.** The highest melting point in the game. You line the crucible
with it in order to melt everything else. The game is called Crucible; this is
the material that finally lets you build a real one, and it should read as the
payoff it is.

**Graphene — give it a source.** From coal under sustained extreme heat and
pressure. It already exists, is already the only material with no melting point,
and is currently unobtainable outside creative mode. This is a bug in the
progression, not a new feature.

**NEW — Refractory / aerogel.** The high-temperature insulator DESIGN.md
deliberately withheld. Note the gap is now only *partly* open: rubber is
`heatCond` 12 and fails at 100 °C, and **ceramic is already `heatCond` 18 and
never melts**, so the "no high-temp insulator" claim in DESIGN.md is softer than
it reads. The real remaining gap is something *better than rubber* that also
survives heat — target `heatCond` ~4, no melting point. Verify ceramic's numbers
before designing against them.

### What this adds up to

Roughly **25 new `MatId` entries** once molten/ore phases are counted. That
affects `MAT_COUNT`, every parallel table's size, and the save format. Plan it
as one migration rather than five.

---

## 5. Crafting coverage for what already exists

The user-facing goal: **every item obtainable without creative mode.** Grouped
by the tier that should gate it.

| tier / station | items |
|---|---|
| **Hand** | torch, platform, rope, door, seeds *(all exist today)* |
| **Bench** (wood+stone) | multitool Mk I, drill, sickle, chest, ladder/rope variants, workbench itself |
| **Anvil** (bronze/iron) | auger, lance, item pipe, crossover, spout, drain, anvil itself |
| **Anvil** (steel) | multitool Mk II, thermocouple, clock, placer, miner, pulse button, block watcher |
| **Assembly** (gold + glass) | lens, relay, lamp, the three combinators, disruptor |
| **Assembly** (titanium) | rocket boots, hermes boots, jetpacks I–III, **armour (new)** |
| **Chem** (acid) | etching-based recipes, graphene, refined reagents |

### Armour is the missing equipment category

`ItemDef` already carries `heatResist` and `coldResist`, and the comment says
outright that they exist *"because a hook with nothing on the other end of it is
how you find out at armour time that the wiring never worked."* The hook is
built and unused. Armour is the natural head/body equipment tier, it makes
titanium worth mining, and it is what makes deep tiers survivable — which is the
loop.

### Deliberately **not** craftable

State this in the code, or someone will "finish the job" later:

- `MAT_WALL`, `MAT_CLONE`, `MAT_VOID` — creative/debug tools, not fiction.
- **`MAT_HEATER` / `MAT_COOLER`** — the important one. They hold themselves at
  max/min temperature *forever*, for free. A craftable infinite heat source
  deletes fuel, deletes the furnace, and deletes thermal engineering, which is
  the entire game. They must stay creative-only.
- Some items should be **found, not made** — in wreckage and ruins. The fiction
  is a crash; the pack you start without is somewhere out there. If everything
  is craftable, nothing is worth exploring for, and the depth→materials loop
  loses half its pull.

### Deliberately not adding

- **Tool durability.** Tedium with no thesis value. The progression is already
  gated by what you can *reach* and *survive*, which is more interesting than
  what has worn out.
- **Multi-step ore processing** (crush → wash → smelt). Depth for its own sake.
  Alloys add the same depth and are *simulated*, which these would not be.

---

## 6. Weapons

DESIGN.md §2 already decided this: a launcher fires **whatever you load it
with**, and modules calibrate *delivery* rather than adding damage.

Current state: `ITEM_MOD_SHOT` / `ITEM_MOD_BLAST` carry fixed `power`, `pierce`,
`blast`, `shotColour`, and `projectile.cpp` breaks cells by comparing `power`
against `g_matStrength`. The delivery half exists. The payload half does not.

**The change:** a projectile gains `u8 payload` (a `MatId`), drawn from the
pack, deposited or reacted on impact.

That one field collapses combat, mining and machine-building into one system:

| payload | effect | already simulated? |
|---|---|---|
| LN2 | freezes, shatters, quenches | ✅ |
| lava / fuel | ignites | ✅ |
| acid | dissolves what heat cannot | after §3.2 |
| water | extinguishes, slakes | ✅ |
| stone/sand | plugs a hole at range | ✅ |

Nothing in that table is a new weapon system — it is the existing material
simulation, delivered. Modules stay as spread, velocity, payload size, fuse
delay, arc. Weapon *tiers* are launcher capacity and velocity, never damage
numbers.

**Watch:** the launcher now consumes from the inventory, which is a real item
model and UI change (what is loaded, how much is left, how you switch). That is
the actual work here, not the projectile field.

---

## 7. Enemies

The design question DESIGN.md left open. The answer that fits *this* game:

> **In a game about machines, the threat should threaten machines.**

An enemy that only drains hp turns combat into a parallel game with its own
rules. An enemy that interacts with the simulation makes every thermal decision
a defensive decision too, and costs almost nothing in new systems because the
simulation is already there.

### Archetypes

1. **Heat-seeker** — navigates toward the hottest cell it can sense. *Your
   furnace is a beacon.* Counters: insulate it, run it cold between uses, or
   build a decoy. This is the best one: it makes the core verb of the game
   generate its own risk.
2. **Corroder** — secretes acid; eats walls, pipes and wiring. Counters:
   gold contacts, ceramic-lined walls. Gives gold a defensive purpose beyond
   conductivity.
3. **Burrower** — tunnels through stone and breaches sealed rooms, so a wall is
   a delay rather than a solution. Counters: harder material, or depth.
4. **Chill-swarm** — quenches fires and freezes water lines. The direct
   inverse of the heat-seeker; attacks uptime, not health.
5. **Ambient hazards** (not entities) — gas pockets, spore clouds. Pressure
   with no AI and no cost.

All four use the armature rig already built, at different proportions and
shades — that was the point of building it that way.

Scale by **depth, not by elapsed time**, matching the loop.

### What does not exist yet

This is the largest new subsystem in this document. There is currently **no
entity system at all** — the player is a single global with bespoke physics.
Needed:

- An entity list with per-entity position, velocity, hp, and state.
- Reuse of `Player`'s grid collision (`playerSolid`, `boxBlocked`) — generalise
  those rather than writing a second collider that can disagree with the first.
- Simple state-machine AI plus grid-aware movement. No pathfinding at first;
  the archetypes above are all gradient-followers (heat, player, material), and
  a gradient is far cheaper and far more in keeping with the simulation.
- Damage in both directions, and a death drop.

Do this **last**. It is the biggest risk and the least reversible, and
everything above ships without it.

---

## 8. Suggested order

Each step is independently shippable and leaves the game playable.

| # | step | why here |
|---|---|---|
| 1 | Stations + recipe browser | Prerequisite. No new content; makes all of it possible. |
| 2 | **Glass** | Smallest new material, exercises the melt path, needed before acid, and immediately makes the simulation visible. |
| 3 | **Crafting coverage for existing items** | The core ask. Everything becomes obtainable. Biggest player-facing win. |
| 4 | Contact reactions + **bronze/steel** | Keystone engine work, proven against coal+steam first. |
| 5 | **Acid + gold** | Needs glass (containment) and reactions. Opens the chemical axis. |
| 6 | Deep tier — titanium, tungsten, graphene source, refractory | Fills the top of the ladder and DESIGN.md's deliberate gap. |
| 7 | Payload weapons + armour | Needs the materials above to be worth anything. |
| 8 | Entity system + enemies | Largest, riskiest, least reversible. |

Steps 1–3 alone deliver "you can craft everything", which is what was asked for.

---

## 9. Risks

- **This plan bets on `simulated`, not `recognised`** (DESIGN.md §1, still
  open). Alloying-by-contact-reaction is a simulated-machines bet. If that
  decision flips, alloys become a station recipe instead — cheap to change, but
  know that it is a bet and not a settled thing.
- **Performance.** DESIGN.md already names the late game as the wall: cost
  scales with activity, and settled chunks are what make this engine fast. Acid
  keeps chunks awake by definition, and enemies keep them awake wherever they
  are. Both make that wall closer. Measure before and after §5 and §7.
- **~25 new materials is one migration**, touching `MAT_COUNT`, every parallel
  table, and the save format. Do it deliberately and in one pass, append-only.
- **Recipe count is a UI problem before it is a content problem.** Do not add
  sixty recipes to a list that cannot show them.
- **Scope.** Sections 1–5 are a coherent, complete game improvement on their
  own. Sections 6–8 are a second project wearing the same coat. It is entirely
  reasonable to stop after §5 and reassess.
