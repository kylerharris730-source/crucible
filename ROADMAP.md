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

## Bees and wax

Not started; recorded here so it is not lost. The shape, as described:

- **Bees pathfind between flowers and their hive** and generate **honey** and
  **beeswax** over time. Wax is solid at room temperature and melts low, which
  the existing thermal system already models properly — it would slot into the
  same phase-change machinery as everything else rather than needing special
  cases.
- **Upgradable bees.** Do something to a hive to advance it: sprinkle it with
  valuable items, or heat it. Higher tiers produce a **special honey and wax**,
  and combining those yields **a new metal** — so bees become a materials
  route that is not mining, which is the interesting part. It gives the game a
  second way to obtain things, on a timer and a supply chain rather than a
  pickaxe.
- Feeding it valuables makes the upgrade a real economic choice; heating it
  ties into the heat ladder the game is already built around.

Worth knowing before this starts:

- **Bees would be the first friendly creature in the game.** Every entity today
  is hostile except the Crash Dummy, so this needs neutral-entity behaviour:
  no aggro, no despawn while its hive stands, and it must not count against
  `ENT_MAX_ALIVE` the way a threat does.
- **There are no flowers yet.** The crop system has wheat, flax and cotton to
  build on, but a blossom the bees can visit is new.
- Pathing exists and is tested (`enemy_path`, `nav_cost`), so routing hive →
  flower → hive is reusing machinery rather than inventing it.

---

## Not required for done

Kept separate so the finish line stays legible: the **shop** and the
**wiki** (both in [CHECKLIST.md](CHECKLIST.md)) make the game easier to
approach, not more complete. Neither should block calling it done.
