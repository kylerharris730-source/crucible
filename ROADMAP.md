# The road to done

What Cinderlift needs before it counts as finished, and the larger ideas that
are not scheduled yet. Near-term chores live in [CHECKLIST.md](CHECKLIST.md);
this file is the long arc.

**Done means:** FOUR bosses, all three layers populated and fleshed out, and a
rocket you can build to win. Two of those three are now done: the bosses are
finished and the layers are populated. The rocket is what is left.

Settled 2026-09-06, and the number is now four rather than "three or four": a
layer 3 boss and then a final one. What follows is the ordering decision that
goes with it, and it matters more than the list does.

**Content first, balance after.** More accessories, more items, and a pass over
the numbers all come AFTER the main game is roughly finished, not alongside it.
Balancing a game that is still missing a third of its content means balancing
against a shape that has not settled, and every number set that way has to be
set again.

And "roughly finished" is deliberately a low bar, in the sense the word has for
this kind of game: Terraria at release was less than half of what it is now.
The target is a complete arc that a player can start and win -- three populated
layers, four bosses, a rocket -- not a complete GAME. Anything that reads as
"and it should also have..." belongs after 1.0 and should be argued about
then.

---

## 1. Bosses — 4 of 4

Today there is exactly one: the **Brood Mother** (900 hp, summoned with
`ITEM_BROOD_CALL`, drops `ITEM_FORGE_CORE`). She is not spawned, only called,
and that pattern is worth keeping — a boss that wanders into you is an ambush,
a boss you summon is a decision.

- [x] **A layer-2 boss.** The Widow: an eight-legged spider, 40x32, that
      scuttles like a Thresher and spits volleys of silk. Killing it opens the
      layer 2 stratum, the way the Brood Mother opens layer 1.
- [x] **A layer-3 boss.** The Censer: 56x44, four hanging limbs, and the first
      MULTI-PART fight in the game -- the body takes a fifth of what you deal it
      while any limb lives, so the fight has an order rather than a health bar.
      Drops the Pyre Core. It opens no seal, because there is nothing below it.
- [x] **A final boss.** The Effigy: 72x88 -- taller than anything else in the
      game and over half again the Censer's area -- a standing figure with a
      cage for a torso and something burning inside it. Three parts, and each
      closes one of the three answers to a slow enemy: the two ARMS reach for
      you if you stand close, the CROWN drops fire on you if you stand away,
      and the ground erupts where you are if you keep moving. The body takes a
      sixth of what you deal it while any part lives. Drops the Ascent Core.

      Gated behind the **Pyre Core** rather than behind the rocket, because the
      rocket does not exist yet. That is the one thing still owed here: when
      there is a rocket, the Effigy Call should come off the Assembly Table and
      onto it, so the last fight is something the ending asks for rather than
      something you can craft the moment you have beaten the Censer.

Each wants a summon item, a drop that unlocks the next tier, and — going by
the Brood Mother — an arena big enough for a dash attack (`BOSS_DASH_SPEED`,
`BOSS_STUCK_FRAMES`).

## 2. Populate the layers

Measured, not guessed:

| layer | enemies | who |
|---|---|---|
| 1 | 6 | Rock Mite, Cinder Moth, Drip Slime, Husk, Bat, Spitter |
| 2 | 5 | Shambler, Thresher, Culverin, Wisp, Stooper |
| 3 | 4 | Ashhound, Emberwing, Slagmaw, Cinderling |

- [x] **Layer 3 has inhabitants.** Four: the Ashhound (fast ground pursuer that
      routes and never stops), the Emberwing (fast flier that PATHFINDS -- 712
      frames of 900 within contact range against the Bat's 306), the Slagmaw
      (artillery whose globs leave fire where they land) and the Cinderling
      (fast, fragile, and it sets the floor alight behind it).
- [x] **Layer 3 has its own materials and hazards.** Brimstone that is warm to
      stand near and BURNS at a temperature lava reaches and a torch does not;
      brimfire that spreads through a seam and burns out into ash; fumaroles
      that spit fire out of cave floors. All of it through the thermal model:
      layers 1 and 2 are about what is hunting you, the deep is about the rock.

## 3. The rocket, and winning

- [ ] **There is no win condition of any kind yet.** Nothing FLIES yet --
      but as of 2026-09-10 the rocket itself exists: see ENDGAME.md, which is
      the design, and stages one and two of it are built: the rocket, and the
      countdown that reaches ignition. What is missing is what happens after
      ignition, and what winning DOES.
- [x] Design the rocket as a **build**, not a purchase: a multi-part structure
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
