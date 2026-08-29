# Cinderlift — progression plan

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

**NEW — Tungsten.** The highest melting point in the game. You line the cinderlift
with it in order to melt everything else. The game is called Cinderlift; this is
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

### Shots fall, and speed is the only lever

Projectiles obey gravity — **the same 0.18 the player falls at**, not a private
number, because a world with two gravities is a world nobody can predict by
watching it. Which makes muzzle speed the interesting stat rather than a number
nobody could feel: drop over a distance is quadratic in flight time, so one
gravity produces a nearly flat line for something fast and a pronounced lob for
something slow, with no per-weapon fudge. Measured:

| | speed | drop @15 | @30 | @56 (full reach) |
|---|---|---|---|---|
| Bolt Caster | 6.0 | 1.1 | 2.7 | 9.9 |
| Blast Module, default | 3.5 | 2.7 | 8.1 | 24.5 |
| Spitter glob | 4.7 | 1.8 | 5.0 | 14.0 |
| **Shot Module** | 3.5 | **0** | **0** | **0** |

**The Shot Module is the one exemption** (`ItemDef::shotBeam`), and it is
structural rather than cosmetic. Its own note always said it "has to read as a
beam, not a pebble" — but it is also the *mining* weapon, and with pierce 10 its
job is to bore a straight tunnel. An arcing beam curves into the floor a few
cells out and digs a ramp. The opt-out is explicit and the default is ON, so a
new weapon that never thinks about gravity still obeys the world.

**Aim is deliberately not compensated.** The reticle marks where you pointed,
not where the shot lands. Auto-correcting would mean adding gravity and then
hiding every consequence of it, and the gap is the skill the arc buys.

#### A lob cannot outrange `v²/g`, and that broke a creature

The one real casualty, worth recording because it is invisible from outside. A
projectile's maximum reach is `v²/g` **whatever angle it leaves at** — no
cleverness in the aiming beats it. The spitter threw its glob at 1.7, giving it
a reach of **sixteen cells**, and it stands off at **ninety**. It was not
inaccurate; it was physically incapable, and the symptom was a creature that
turned to face you, ran its attack timer, and never fired.

Fixed by raising `shotSpeed` to 4.7 (reach 123, comfortable margin over the
stand-off) and teaching `spitTick` to aim high. That aiming is the **exact**
launch solution, not an approximation: substituting the speed constraint into
the two motion equations collapses to one quadratic in `t²`, whose smaller root
is the direct shot and whose larger is a mortar arc. A negative discriminant
means genuinely out of range, and the answer there is to hold fire.

The obvious iterative dodge — *"it takes this long, so it drops that far, so aim
there"* — was tried first and **measured at zero hits from six ranges**. It
estimates flight time as straight-line distance over speed, but horizontal speed
is only `v·cosθ`, so the steeper it aims the longer it really takes and the
correction chases its own tail.

Two things this leaves for play-testing: the glob now arrives in 0.35 s at the
stand-off against the old 0.88 s (though it is a visible *arc* now, which is
easier to read the landing point of, not harder), and the mortar solution peaks
**51 cells** up — confirmed useless underground, since a glob breaks nothing and
would splatter on any ceiling.

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

### Status: built, for layer 1

This section was written when there was no entity system at all. There is one
now, and three of the archetypes above exist: the **rock mite** (burrower), the
**cinder moth** (heat-seeker) and the **drip slime** (corroder). See
`src/entity.h` for the design and §10 below for the layer scheme they belong to.

What was built and what it cost:

- `Entity` + `ENT_DEFS[]` in a fixed pool of 96, ticked after the player so
  contact damage tests where the character actually ended up.
- `solidBox()` in `player.cpp`, sharing `playerSolid()` with the character —
  one collider, not two that can disagree about platforms or falling powder.
- `ItemDef::damage` split from `ItemDef::power`, and `ItemDef::armour` added.
  **This split is the load-bearing one:** `power` is a threshold against
  `g_matStrength` and is therefore capped by the material ladder, which has four
  rungs left. Combat needs about seven steps to reach the end of hardmode, so
  damage had to become an unbounded number of its own.
- Entities are **not saved**. They respawn from the dark, so the save format did
  not have to grow; only `g_worldTime` was added (four bytes, `TIME` section).

Still missing: bosses, and anything for layers 2 and 3.

### What did not exist when this was written

There was **no entity system at all** — the player was a single global with
bespoke physics. What it needed, all now done:

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

---

## 10. The three cave layers

Added after §1–§9 were implemented. This is the structure the rest of the
progression hangs off, and it replaces "depth" as the game's pacing axis with
something the player can see and be stopped by.

### The world got taller

`SIM_H` 3072 → **6144**, `SURFACE_Y` → `SIM_H/2`, so both the sky and the
underground doubled. The old world was crossable in well under a minute in
either direction. Costs 145 MB of `World` against a 32-bit process's 2 GB;
settled chunks are still free, which is what makes that a memory question
rather than a frame-time one.

**Measure, do not assume.** The mean stone line sits at **y=2962**, not at
`SURFACE_Y` — soil depth and the lake basin both push it down — so the
underground is about **3180 cells**, not the 3900 that halving the world height
suggests. The first cut of the layer depths was picked from the optimistic
figure and gave layer 3 a fifth of the world. `layers.cpp` re-measures this in
one line.

### The layers, and what seals them

| | depth below stone line | ore | hazard | backdrop |
|---|---|---|---|---|
| **Layer 1** | 0 – 1050 | copper, tin, coal, iron | — | warm brown |
| *stratum* | 1050 | — | — | — |
| **Layer 2** | 1074 – 2050 | gold, titanium | acid pockets (1150+) | cold green |
| *stratum* | 2050 | — | — | — |
| **Layer 3** | 2074 – ~3180 | tungsten | lava hotspots (2200+) | hot red |

`MAT_STRATUM` at a new strength rung **`STR_SEALED = 235`** — above every tool
and shot that exists, below `STR_ABSOLUTE`. So it reads as unbreakable for the
whole of the game as it stands and stops reading that way the moment something
with power 235 is built, which is a reward hardmode can hand out. That gap is
the last of the ladder's headroom and is deliberately spent on this one thing.

Generated **last**, after caves, ore, hotspots and acid, so nothing can cut
through it — the seal is a property of the ordering, not of every other
generator agreeing to be careful. Verified unbroken across all 4,094 columns.

`digInto()` gained a `power` parameter to make this bite. Every existing tool is
set to `STR_HARD`, so **nothing that could be dug before is harder now** —
introducing a gate is not an excuse to re-tier the mining ladder underneath it.

### Iron was not a layer 1 ore

Worth recording because the table said otherwise. Measured on a generated
world, iron ran 420–1900 with a **p10 of 640 and a mean of 1145** — against
gold's 1166. Iron and gold were the same tier by every measure except intent,
and 92% of the world's iron sat below what is now the layer 1 boundary. Every
band is now bounded by a *layer*, and `layers.cpp` asserts the invariant that
actually matters: **no ore ever appears above its own layer.** (Leaking a little
*deeper* is fine — a vein of copper poking under a barrier costs nothing.)

### Zones do two jobs

`ZoneId` grew from `{SKY, UNDER}` to `{SKY, LAYER1, LAYER2, LAYER3}`, appended
so old saves keep their meaning (`ZONE_LAYER1` *is* the old `ZONE_UNDER`, which
is right — an old world's underground is shallow-tier). One label now picks both
the backdrop and which creatures may spawn, so crossing a boundary is visible
and "what lives here" is a property of the place.

### Day and night

One counter (`g_worldTime`), twelve minutes, applied at the **ray seed** in
`light.cpp` rather than to the finished light value — so a torch at night is
correctly brighter than its surroundings instead of being dimmed along with
them. Night floors at 34/255 rather than 0: a pure-black surface is
indistinguishable from the void outside the world and cannot be walked on.

### The reward is a station, not an ore

**Decided.** The layer 1 boss drops a **Forge Core**, whose only use is building
the **Blast Furnace** (`STATION_FORGE`, `MAT_STATION_FORGE`) at the anvil it
supersedes. The steel tier — Thermal Lance, both pieces of steel armour, Jetpack
Mk II — moved behind it.

The reasoning, which generalises to every later boss: an exclusive **ore** makes
the boss something you *farm*, and a boss you farm is a chore with a health bar.
A **station** is won once, changes what you can build forever, and cannot be
ground for. Later layers should follow the same shape.

Note carefully what did *not* move. Smelting steel is still an iron+coal contact
reaction happening in a vessel you built out of pixels. The forge gates turning
a steel bar into an **object**. That is §2's line — simulation transforms
matter, crafting fabricates objects — and `forge.cpp` asserts it by failing if a
recipe ever appears whose output is steel.

**One placeholder to delete.** There is no boss yet, so shipping the gate
without the key would make the entire steel tier unreachable — a straight
downgrade from today, and something `reachable.cpp` would correctly report as a
failure. Until the boss exists, a Forge Core can be crafted from a deliberately
painful pile of late layer 1 materials. When the boss lands: give it
`ITEM_FORGE_CORE` as its drop, delete that one recipe, and re-run
`reachable.cpp` — which will then be asserting the boss is the only source.

### The roster, and how it splits

Six creatures, deliberately in two halves. The first three interact with the
SIMULATION -- a burrower that eats walls, a heat-seeker that finds your furnace,
a corroder that leaves acid. The second three are pure combat, and what
distinguishes them is MOVEMENT rather than a gimmick, because a layer made only
of gimmicks is a layer where every fight is a puzzle.

| | size | hp | touch | speed | what it is |
|---|---|---|---|---|---|
| Rock mite | 12x9 | 18 | 6 | 0.34 | chews rock; a wall is a delay |
| Cinder moth | 9x7 | 10 | 4 | 0.52 | flies to the hottest cell |
| Drip slime | 11x8 | 24 | 5 | 0.20 | leaves acid that outlives it |
| Husk | 11x22 | 46 | 11 | 0.42 | walks at you and does not stop |
| Bat | 9x7 | 12 | 7 | 1.35 | fast, poor steering, overshoots |
| Spitter | 10x12 | 22 | 5 | 0.26 | holds distance and shoots |

The bat is the one worth explaining, because the obvious implementation is
wrong. The overshoot is NOT speed -- a fast creature that re-aims every frame
tracks you perfectly and is simply unavoidable. It commits to a heading for
34-60 frames and cannot turn fast enough to correct it, so it arrives where you
WERE. The counter-play is to let it commit and then not be there.

#### A drop is not just a supply question

The moth used to drop **glass** — wings fused by the heat it chases, and
justified on supply: glass gates the Chemistry Bench and the Assembly Table, and
the only other source is a two-cell-deep beach on one lake.

It was still the wrong drop, for a reason the supply argument never reaches.
**Glass is a building block, and the commonest flier in the layer handing you
stacks of one turns a fight into inventory management.** What you want off a
fast nuisance is something you *spend*. It drops **coal** now, which is what a
heat-chaser should be carrying anyway.

**This costs something and the cost is real:** glass is back to being gated on
that one beach. The honest fix is for sand to generate somewhere underground —
it is currently the only common surface material with no deep source at all —
and that is worth doing before the Chemistry Bench matters.

### How fast a cave fills, which the comment got backwards

`entSpawnTick` carried a claim that twenty probes against a cap of ten "fills a
dark cavern over a few seconds — somewhere gradually becoming occupied, not an
ambush materialising." That was written beside the constant rather than measured
from it. Measured in a dark layer-1 cavern: **first creature on frame 1, cap
full on frame 10.** A sixth of a second. Precisely the ambush it promised not to
be.

Two separate levers now, because "too many" and "too fast" are different
complaints:

- `ENT_MAX_ALIVE` **10 → 7** — the density a cave settles at.
- `SPAWN_COOL` **= 45 frames** — a global clock, reset only on a successful
  spawn. Probes are *not* the pacing lever; lowering them would make spawn rate
  depend on how cluttered the cave is rather than on time.

Measured after: **cap reached at 4.62 s**, one creature at a time.

### Galleries, and a tuning knob that was not monotonic

Two changes to the caves, both measured against the previous world rather than
eyeballed.

**Galleries.** Every third worm is now cut with a fraction of `CAVE_SWING` and a
dead-level base heading. `CAVE_SWING` is 1.15 rad — about 66° — so a "mostly
horizontal" worm still spent most of its length climbing and diving, and the
underground had no long level runs anywhere. First attempt used swing 0.11 and
produced *ruled lines*: the longest walkable level run went 487 → **1178** cells
and the overview showed dead-flat tunnels crossing most of the map. 0.28 lands
at **684** — clearly horizontal, still visibly rising and falling.

**More and bigger chambers**, 26 → 34, with every fourth at 1.55× radius. Note
that multiplier *compounds* with the existing depth `grow` (~1.37 at the
bottom); at the 1.85 first tried, the deepest rooms were 6× the base area and
layer 3 measured 70% more hollow.

The finding worth keeping is about the **knob, not the number**. Chamber depth
used to be drawn uniformly from the whole underground, so changing
`CHAMBER_COUNT` changed every chamber's index and relocated the entire set.
Layer 3's air went **8.5% → 14.5% → 7.9%** across three successive tunings —
*not monotonic*, and a knob that is not monotonic cannot be tuned, only guessed
at. Chambers are now **dealt round the layers** by index and placed within one,
which makes the count mean what it looks like it means and guarantees every
layer gets rooms.

| air by layer | before | after |
|---|---|---|
| Layer 1 | 11.36% | 14.61% |
| Layer 2 | 11.80% | 13.73% |
| Layer 3 | 8.54% | 12.52% |

### The boss

**Brood Mother**, 34x24, 900 hp. A rock mite grown enormous, which is the right
shape for a first boss: after a layer of killing her young she needs no
introduction and her threat is legible before she does anything.

Two phases and nothing more. She walks, charges (committed for CHARGE_FRAMES so
the lunge is readable rather than merely fast), and chews rock across her whole
face so no wall is an answer. Below half health she charges more often and calls
her brood.

Summoned by a crafted **Brood Call** -- 12 chitin, 6 iron, 4 bronze at the anvil
-- and never spawned. A boss you can blunder into is one that kills you while
you are carrying a full pack of ore; a summon means you arrive having chosen the
ground and the moment.

She drops the **Forge Core**, so the steel tier is genuinely behind her and the
placeholder recipe that stood in for her is deleted. That closes the loop the
Blast Furnace opened.

### Four dead ends in the opening — all of them ours

Designing layer 1 turned up four things that were not *hard*, they were
**impossible**, and every one had been invisible because the tests all started
from a stocked inventory. They are worth listing together because they share a
shape: a loop with no way in, which no amount of playing better can open.

| Dead end | Why it was sealed | What opens it |
|---|---|---|
| **No fire** | The torch is cold, lava is behind a sealed stratum, and sparks need a Clock, which needs iron, which needs fire. The survival opening ran on the debug Heat brush. | **Flint Striker** — 4 stone, by hand |
| **No weapon** | Damage lives on *modules*; a module needs a Multitool; both need copper. The first descent had no answer to anything alive. | **Bolt Caster** — you spawn holding one |
| **Wheat did nothing** | It grew from seed and dropped seed. Nothing in the middle. | **Bread** — 4 wheat at a bench, heals 30 |
| **No crop seeds anywhere** | Wheat, flax and cotton grow from seed and drop seed, and worldgen plants only oak. The entire agricultural branch was unreachable. | 2 grass → 1 seed, by hand *(stopgap)* |

Two notes on these.

The **grass-to-seed recipes are a stopgap and are labelled as one in
`craft.cpp`.** The real answer is that wheat and flax should *generate* — wild
meadows, the way oak generates — and when they do, those three rows should be
deleted. Grass is the placeholder source only because it is the one plant that
spreads on its own, so it is renewable without being free.

`reachable.cpp` **did not catch the crop loop and still would not**, which is
the more useful finding. Its closure seeds itself with `GROWN[]` — everything a
plant can drop — so it believed wheat was available because wheat drops wheat.
A closure that seeds itself with its own outputs cannot detect a cycle with no
entry point. It *did* catch the Brood Call (`needs Chitin — which nothing
produces`) because creature drops were genuinely missing from its seed set, and
that is now fixed. The crop case is a different bug in the same tool and it is
still open.

The **Bolt Caster is the floor of the ladder, not a rung on it.** Four damage
against the Shot Module's six, and `power = 0`, which loses to every material in
the game — so a bolt stops at the first wall instead of digging through it. That
is the line between a weapon and a tool, and a starter weapon belongs on this
side of it. It has no module slots at all, which is what makes it a floor: there
is nothing to socket into it and never will be, so the only way to shoot harder
is to build the Multitool it is pointedly worse than.

That slotlessness needed one change in `toolResolve()`. Its first job is to bail
out of a tool with no `ToolInst` — *"no state: a stick"* — and a slotless weapon
has no state by construction, so the starter weapon has to be answered **above**
that check rather than below it. Put below it, as it first was, the thing you
spawn holding resolves to a stick.

### Stations became objects, and one field made it cheap

All five crafting stations were **single cells** — a workbench you could lose
behind one grain of dirt. They are now 14×14 devices, each with its own sprite.

The cost was a table row apiece, and the reason is `DeviceInfo::cellMat`. That
field was added so the torch could be made of `MAT_TORCH` rather than of
machinery; it means each station's footprint is written in its own
`MAT_STATION_*`, so **`craftScanStations` still finds a bench by looking for
bench material and never learns that devices exist.** Everything else — overlap
rejection, `devIntact` noticing you mined a corner, `devRemove` clearing the
block — already read `cellMat` rather than assuming `MAT_DEVICE`.

The item and the material stay **separate ids** on purpose: `ITEM_WORKBENCH` is
what you carry, `MAT_STATION_BENCH` is what the footprint is made of. That also
means an old save holding the material can still place the single cell it always
could, and it still works as a station.

**It exposed a latent bug.** `devPlace` decided lattice-snapping with
`type >= DEV_PIPE` — an ordinal test that was correct only by accident of enum
order, and had already drifted: the block watcher, pulse button and all three
combinators sort after `DEV_PIPE` and were being snapped to a 14-cell grid,
though none of them connects by edge contact. Appending the workbench made it
*visible* (furniture jumping to a grid). It now asks `isLogistics()`, the named
predicate that already existed.

### The dig filter

A whitelist of materials the tool may break, for one situation with no other
answer: a vessel part-way through a smelt holds ceramic with copper and slag
through it, and the ordinary brush is indiscriminate. Draining the metal out
while it is still liquid is the elegant route and needs planning; this is the
one that works after the fact without dismantling the setup.

A `Filter: ON (n)` button, a picker that **toggles and stays open** (unlike every
other picker here, which chooses one thing and closes — you are building a set),
and an amber **funnel** cursor. The cursor is a distinct *shape*, not a
recoloured crosshair, because this is a mode that silently makes digging refuse
most of the world, and being unable to dig for reasons you cannot see is the
worst way for it to fail.

Two decisions worth keeping:

- **A skipped cell does not spend the bite.** This is what makes it a tool
  rather than a curiosity: measured, a filtered bite takes 10 cells and an
  unfiltered one takes 10. Without it you would hold the button while a
  thousand-cell heap released one copper a second. It sits with `plantsOnly` and
  `power`, which skip the same way for the same reason.
- **A parameter, not a global**, even though exactly one caller passes it.
  `digInto` is also how the *miner device* removes cells, and a sidebar toggle
  silently changing what a machine digs would be a bug invisible from the
  machine.

Scope, deliberately: it gates **digging only** — not projectiles, not background
scraping — and the whitelist is not saved.

### One `kind`, two different things

`ITEMK_TOOL` now means a modular platform *or* a fixed weapon, and that
ambiguity broke the module bench in a way nothing caught.

`firstToolSlot()` returned the first `ITEMK_TOOL` in the pack, which was
unambiguous while the multitools were the only tools. The Bolt Caster is also an
`ITEMK_TOOL`, has `toolSlots == 0` by design, and is handed to you **in slot 0
at spawn** — so it beat every multitool, the panel computed a loadout of zero
slots, `benchH` went to 0, and the module bench rendered at zero height.
**Nothing errored. A section of the panel simply was not there**, which is why
the suite was green and a player found it in a minute.

Fixed by requiring `toolSlots > 0` — a tool with no slots has no loadout, so it
is never the right answer to "which bench do I show". `toolResolve` already
handled the distinction correctly, which is why *shooting* worked while the
bench did not.

The general lesson is worth more than the fix: **a `kind` that acquires a second
meaning silently invalidates every "first of this kind" query written under the
first meaning.** Those queries do not fail loudly; they answer confidently with
the wrong thing.

### What layer 1 still needs

- **Play testing.** Nothing below has met a human. The numbers most likely to be
  wrong: the husk hitting for 11 against 100 hp with no armour; whether the
  bat is fun or merely annoying; 900 boss hp, which assumes you fight her after
  the Blast Module (22 damage, 41 hits) rather than the Shot Module (6, and 150);
  a spawn cap of ten filling a cavern in seconds; and 12 chitin at 1-3 a kill
  being roughly seven creatures, which may be too cheap for a summon. Add to
  that list: whether 30 health a loaf makes bread worth the pack space, and
  whether the Bolt Caster is weak enough to be a reason to upgrade without being
  so weak that the first mite is a war of attrition. And now that shots fall:
  whether leading a bolt across a cavern is satisfying or fiddly (the lever is
  `ITEMS[ITEM_BOLTER].shotSpeed`, not the gravity), whether the Blast Module's
  24-cell drop at full reach makes it feel like a grenade launcher or like a
  broken gun, and whether a spitter glob arriving in 0.35 s is fair.
- Layers 2 and 3 have **no creatures** — deliberate, and `spawn.cpp` asserts it,
  so the day something is given a layer 2 mask that test notices.
- **Hardmode has no materials at all.** Seven ores do not stretch to three
  layers plus four more bosses; that is the next real content gap.
- **Wild wheat and flax should generate**, so the grass-seed stopgap above can
  be deleted rather than kept.
- **The sprite palette is full.** Every letter and digit is spoken for; the
  Bolt Caster's stock had to take `#`. The next additions either start on
  punctuation or begin reusing colours that already mean something else, and
  the second of those is how a shared palette quietly stops being shared.
