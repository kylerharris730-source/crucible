# The road to done

What Cinderlift needs before it counts as finished, and the larger ideas that
are not scheduled yet. Near-term chores live in [CHECKLIST.md](CHECKLIST.md);
this file is the long arc.

**Done means:** three or four bosses, all three layers populated, and a rocket
you can build to win.

---

## 1. Bosses — 1 of 3–4

Today there is exactly one: the **Brood Mother** (900 hp, summoned with
`ITEM_BROOD_CALL`, drops `ITEM_FORGE_CORE`). She is not spawned, only called,
and that pattern is worth keeping — a boss that wanders into you is an ambush,
a boss you summon is a decision.

- [ ] **A layer-2 boss.** Layer 2 has five ordinary enemies and no capstone.
- [ ] **A layer-3 boss** — see §2; there is nothing down there at all yet.
- [ ] **Optionally a fourth, at the rocket.** A final fight gated behind the
      win condition rather than behind a depth, so the ending is something you
      beat rather than something you assemble.

Each wants a summon item, a drop that unlocks the next tier, and — going by
the Brood Mother — an arena big enough for a dash attack (`BOSS_DASH_SPEED`,
`BOSS_STUCK_FRAMES`).

## 2. Populate the layers

Measured, not guessed:

| layer | enemies | who |
|---|---|---|
| 1 | 6 | Rock Mite, Cinder Moth, Drip Slime, Husk, Bat, Spitter |
| 2 | 5 | Shambler, Thresher, Culverin, Wisp, Stooper |
| **3** | **0** | — |

- [ ] **Layer 3 has no inhabitants.** Worldgen labels the zone
      (`ZONE_LAYER3` in `worldgen.cpp`) and `caveLayerOf` returns 2 for it, but
      not one entity sets bit 4 in its `layerMask`, so the deep is generated
      and then empty. This is the single biggest content hole.
- [ ] Give layer 3 its own materials and hazards, not just tougher enemies.
      The difficulty step between 1 and 2 is currently carried by the roster
      alone; the deep should feel different to stand in.

## 3. The rocket, and winning

- [ ] **There is no win condition of any kind yet.** Nothing in the code builds
      toward an ending.
- [ ] Design the rocket as a **build**, not a purchase: a multi-part structure
      assembled from the deepest materials, so finishing the game is the last
      and largest engineering problem rather than a crafting recipe. The device
      and station systems already support multi-cell placed machines.
- [ ] Decide what winning does — credits, a new-game-plus, or simply a marked
      save. Worth deciding early, because it determines whether the rocket is
      consumed.

---

## Bees and wax — built

Shipped. What exists now:

- **Hives** are devices. They spawn bees up to a setpoint (1–5, default 5),
  slowly, so a new hive fills rather than arrives finished. Each delivery a
  bee brings back is extruded as **wax from the top** and **honey from the
  sides** — different faces so the two products separate themselves with no
  sorting machinery.
- **Bees** fly hive → nearest flower → hive, on a bounded, occasional search.
  They are the first creatures in the game that are not hostile: no contact
  damage, and excluded from the spawn cap so an apiary does not accidentally
  become a monster repellent.
- **Flowers** grow on their own while turf spreads, and can be sown from
  seed. A hive with no flowers in range produces nothing, which is tested.
- **Beeswax** is solid at room temperature and melts at 46 °C (with 6° of
  hysteresis so it cannot flicker). **Honey** is denser than water and
  genuinely thick — dispersion zero, so a spilled cell stays put and only a
  deep pool spreads under its own weight.
- **Coal bees.** Sustained contact with coal converts a bee; a brush past a
  seam does not, and the soot wears off. Their **coal wax and coal honey
  boil back down into coal** at 78 °C and 72 °C — a hive over a heat source
  is a coal supply that runs without you.
- **Honey Draught** heals 55 against bread's 30.

### Still open on this

- [ ] **The metal.** Combining a special honey and a special wax into a new
      alloy is the part that is not built. The materials and the boil-down
      path exist; what is missing is the recipe and the metal itself.
- [ ] **More bee kinds.** Only coal converts today. Ore dusts, steam and
      gentle heat were all on the list; the conversion is written as a
      lookup (`beeDustAt`) so another one is a line there and a species
      beside `ENT_COAL_BEE`.
- [ ] **A wild hive to find**, so the first hive does not need wax you can
      only get from a hive. Today the entry point is planting flowers and
      the bench recipe makes every hive after the first.
- [ ] **Nobody has played it.** The loop is covered by `tests/hive_bees.cpp`
      (13 checks) and the sprites have been looked at, but no hive has been
      placed by hand in a running game.

## Not required for done

Kept separate so the finish line stays legible: the **shop** and the
**wiki** (both in [CHECKLIST.md](CHECKLIST.md)) make the game easier to
approach, not more complete. Neither should block calling it done.
