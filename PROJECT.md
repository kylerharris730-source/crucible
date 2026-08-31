# Cinderlift — the whole project, on one page

**What this document is.** Every other `.md` here is deep and narrow: one owns
the simulation, one owns progression, one owns the wire protocol. None of them
says *where the project is*. This one does, and it is the file to read first
when you have been away.

It is a map, not a spec. Where it disagrees with a subsystem doc, the subsystem
doc is right about its own subsystem and this file is stale — fix it.

Written 2026-08-31 against `main` @ `8042b51`.

---

## 1. The thing itself

> Crashed on an alien planet. Build machines out of real pixel physics, and
> escape.

```
depth  →  materials  →  machines  →  capability  →  more depth
```

That loop is the entire game. Everything else is either the loop, or scaffolding
holding the loop up. When a feature cannot be attached to an arrow in that
diagram, it is content — and content is the cheap part.

The differentiator is **thermal engineering**: a continuous temperature field
with conductivity, thermal mass, phase change and insulation, which no
comparable game simulates. Nearest prior art is Oxygen Not Included, not Noita.
That is DESIGN.md's claim and nothing since has challenged it.

---

## 2. The stack, bottom to top

Seven layers. Each is complete enough that the one above it can be built. This
is the ordering to think in, because a change low in the stack is expensive and
a change high in it is not.

| # | layer | owns | primary source | doc |
|---|---|---|---|---|
| 1 | **Simulation** | 99 materials, temperature field, phase change, fluids, powders, gas pressure, wetness | `materials.cpp`, `world.cpp` | INTERNALS.md, THERMAL_PRESSURE.md |
| 2 | **World** | chunked streaming, active margins, worldgen, caves, ore bands, layers, day/night, lighting, save | `worldgen.cpp`, `world.cpp`, `save.cpp`, `light.cpp` | PROGRESSION.md §10 |
| 3 | **Actors** | player, collision, health, inventory/equipment, 13 creature types, rigs and gaits, drones, projectiles | `player.cpp`, `entity.cpp`, `rig.cpp`, `armature.cpp`, `drone.cpp`, `projectile.cpp` | PROGRESSION.md §7, SPRITES.md |
| 4 | **Machines** | 22 device types, item logistics, copper pulses, circuit wires, combinators | `device.cpp`, `navigate.cpp`, `room.cpp` | LOGISTICS.md, CIRCUITS.md |
| 5 | **Progression** | ~116 items, ~100 recipes, 6 crafting stations, three cave layers, one boss | `item.cpp`, `craft.cpp` | PROGRESSION.md |
| 6 | **Shell** | window, panels, hotbar, creative inventory, UI scale, input, undo | `main.cpp` (8,051 lines), `render.cpp`, `sprite.cpp` | README.md |
| 7 | **Platform** | Win32 native; Win32-shim → SDL/WASM for web; TCP multiplayer; WebRTC multiplayer; launcher and releases | `src/web/`, `network.cpp`, `multiplayer.cpp`, `launcher/`, `signal/` | MULTIPLAYER.md, LAUNCHER.md, signal/README.md |

**Layer 6 is the one to watch.** `main.cpp` holds the frame loop, the window
proc and every panel. It is also, deliberately, the file the web port refused to
fork — the entire web diff to it is one `#include`. That constraint is
load-bearing and worth more than the tidiness would be.

---

## 3. Where the project actually is

"Shipped" means it is in `main` and a player can reach it.

### Done and stable

- **The simulation.** Inherited from `powder`, essentially complete, measured.
- **A world bigger than the screen.** DESIGN.md called this "the point of no
  return". It was crossed. `SIM_H` is 9216, streaming works, saves work.
- **A player that collides with pixels.** Also from DESIGN.md's risk list. Done.
- **Machines and automation.** Logistics, pulses, circuit wires and combinators,
  all shipped and documented.
- **The three cave layers.** Depths measured rather than assumed, ore bands
  bounded by layer, `MAT_STRATUM` sealing each boundary, generated *last* so the
  seal is a property of ordering rather than of every generator being careful.
- **Crafting stations and a material ladder** through steel, with the Blast
  Furnace behind the layer 1 boss.
- **Layer 1 roster + Brood Mother boss.** Six creatures plus a summoned boss.
- **Layer 2 roster.** Shambler, Thresher, Culverin, Wisp, Stooper — plus ichor
  as the layer drop and class armour. (HANDOFF.md still lists most of this as
  uncommitted "next steps". It shipped.)
- **The browser build.** `cinderlift.com`, deployed by CI on push, saves persist
  to IndexedDB, UI scale adapts to the window.
- **Native four-player multiplayer.** Host + 3, host-authoritative, client
  prediction, chunk-hash repair, per-peer watermarks, field-wise codec, and a
  four-real-process test.
- **Launcher and releases.** Self-updating installer, version resources,
  manifest verification, offline launch.

### In flight, uncommitted right now

**Four-player multiplayer in the browser.** WebRTC data channels behind the same
`network.cpp` protocol via `src/web/netshim.h`, plus a Cloudflare Worker + D1
broker that turns the two-paste handshake into a five-character room code. The
tree has `web/netfour.html`, `web/netpeer.html`, `tests/signal_four.mjs` and a
substantial rewrite of `web/webrtc.js`. This is the current front.

The framing is right and worth keeping: the broker is a **convenience over a
path that cannot break**. Manual paste always works, the broker only removes
typing, and an unreachable broker is treated exactly like an absent one.

### Not started

- **Layer 3 creatures.** Layer 3 has ore and lava hotspots and nothing alive.
- **Bosses beyond the Brood Mother.**
- **Tiers 3–4 of the ladder** — titanium, tungsten, graphene's source, the
  refractory insulator. Gold and acid exist; the top of the ladder does not.
- **Hardmode has no materials at all.** Named in PROGRESSION.md as the next real
  content gap.
- **The escape machine.** The actual ending. DESIGN.md §4 committed to it being
  a machine rather than a boss, and nothing has been built toward it.
- **Blueprints.** The moment someone builds a still they like, they will want to
  re-place it.
- **Threading.** See §6.

---

## 4. Decisions that are settled — do not relitigate

These have been paid for. Reopening one costs more than it looks.

1. **Simulation transforms matter; crafting fabricates objects.** Ore→metal,
   clay→ceramic, sand→glass, alloys — heat and chemistry, in a vessel you built
   out of pixels, always simulated. Metal bar→drill — fabrication, in a menu at
   a bench. This is the working answer to DESIGN.md's "simulated or recognised"
   question, and it has held across every feature since.
2. **Weapons fire materials.** One system for collection, refining and combat.
   Modules calibrate delivery; they are not a spell language.
3. **Progression gates live in the material table.** Melting points and
   insulation ceilings *are* the tech tree. Depth is the axis.
4. **A boss drops a station, never an ore.** An ore makes a boss farmable; a
   station is won once and changes what you can build forever.
5. **The finale is a machine, not a boss fight.**
6. **`main.cpp` never branches on platform.** The web build consumes the same
   `FillRect`. One `#include` is the entire concession.
7. **Prefer a table row to a rule.** Smelting, phase change, drops and slaking
   are columns, not branches in `world.cpp`.
8. **Repo URLs, `%LOCALAPPDATA%\Crucible`, and `crucible` where it means the
   vessel, all stay un-renamed.** Deliberate; do not "finish the job".

## 5. Decisions still open

| question | where it bites | leaning |
|---|---|---|
| **Graphene route** — brute-force heat and pressure from coal, wax catalysts, or both? | The endgame conductor is still unobtainable outside creative | **Complement.** Keeps bees optional-but-rewarding; does not invalidate a documented decision. |
| **Armour scope** — one set per layer, or prove the pattern with a single layer-2 set of three archetypes? Does drone armour buff the drone or the player? | `item.h` only, or `drone.cpp` too | Prove the pattern first |
| **`ITEM_LENS` has no recipe.** Obviously wants glass. | Small, unblocked | Just do it |
| **Web saves vs. native saves.** The browser persists to IndexedDB; the two worlds never meet. | Player expectation | Undecided |
| **Cross-compiler play.** Packets are portable; the *join snapshot is an ordinary save file*, written as raw `fwrite(&x, sizeof(x))`. And `PLYR` loads only `if (len == sizeof(Player))`, so adding one player field silently discards every existing character. | Blocks non-identical builds today | Convert saves to the `codec.h` visitors |
| **Code signing** (~$200–400/yr OV). | The launcher was flagged as malware on one machine | Outstanding. Version resources reduced the problem; only signing solves it. |

---

## 6. The standing risks

**Cost scales with activity, and this is a game about causing activity.** The
engine is fast because settled chunks cost zero. A base with five running stills,
a furnace and a lava tap is a base where nothing sleeps. Acid keeps chunks awake
by definition; so do enemies. The late game is the performance problem, and it
arrives exactly when players are most invested. Mitigations, in order of appeal:
threading; letting recognised steady-state machines stop simulating their
interior; sleep heuristics.

**Threading is harder here than in Noita.** Noita four-colours the chunk grid,
which is safe because a cell moves one cell per update. Here, liquid lateral
scans reach ~15 cells and graphene's `heatSpread` is **28** — a cell reaches
clean past its neighbour into the chunk beyond. One chunk of separation is not
enough. Options: a wider guard band, larger chunks, or special-casing the
long-range paths. Worth solving *before* content depends on the timings.

**Constants drift, silently.** Two bugs in one session came from a number that
was correct when written and never rechecked when its inputs moved: the light
margin (set to 128, twice its own stated invariant — 91% of every solve
discarded, 4.9 ms of a 6.4 ms frame) and the pulse plane (justified in its own
comment at 24 MiB, quietly grown to 72 MB). Both were found by reading a comment
against the code beside it. **Suspect others.** Any constant with a justifying
comment that cites a number is a candidate.

**A `kind` that acquires a second meaning silently invalidates every "first of
this kind" query written under the first meaning.** They do not fail loudly;
they answer confidently with the wrong thing. That is how the module bench came
to render at zero height with a green test suite.

**~25 new materials is one migration**, touching `MAT_COUNT`, every parallel
table, and the save format. Plan it as one pass, append-only.

---

## 7. Things that break silently — the invariant list

Consolidated from PROGRESSION.md §0 and MULTIPLAYER.md. Every one of these fails
without an error.

1. **`MatId` is append-only.** Saves store raw numeric ids; inserting a material
   among related ones corrupts every existing world. The same applies to
   `ItemId`, `EntityType` (its ordinals back a layer bitmask) and `ZoneId`.
2. **Worldgen never touches the global rng.** Everything derives from
   `hash1()`/`fbm()` on position. One `rngNext()` makes the world depend on how
   many numbers were drawn before it — so it changes shape according to what the
   player did last session.
3. **Material properties live in standalone parallel tables**, not in `MATS`
   rows — `g_matStrength`, `g_matConducts`, ~20 more. A new material means
   visiting each table it belongs in. Miss one and you get a material that is
   minable but not conductive, with no warning.
4. **`ItemDef` is memset to zero, and 0 is a real value** for `equipSlot` and
   `deviceType`. `kind` is the only field that decides what an item is. There is
   a documented past bug where every stack of stone became wearable.
5. **One cell of world is one unit of item.** Recipe quantities are in cells.
   Keep it 1:1.
6. **Never add network calls inside material behaviours.** The host diffs
   changed interest chunks and replicates generically, so new materials, heat
   rules and fluid reactions need *no* multiplayer code at all.
7. **The sprite palette is full.** Every letter and digit is spoken for; the Bolt
   Caster's stock had to take `#`. The next addition either starts on punctuation
   or quietly stops the palette being shared.
8. **Never widen `build.bat`'s cleanup past `.exe`.** Saves live in `build\`.

---

## 8. The arc from here

Three horizons. Each step is independently shippable and leaves the game
playable — that property is worth protecting.

### Horizon A — finish what is open

1. **Land web multiplayer.** It is the in-flight work and it is close. Commit
   the WebRTC rewrite, the broker changes and `signal_four.mjs`.
2. **Rewrite HANDOFF.md, or delete it.** It describes the world as of `52b783e`,
   thirty-plus commits ago, and lists as "next steps" work that has since
   shipped. A stale handoff is worse than none: it is confidently wrong.
3. **A test runner.** There are 38 C++ harnesses, one PowerShell script and one
   Node module, each built by hand. There is no `make test`. This is the single
   highest-leverage missing piece of infrastructure, and it gets more valuable
   with every invariant in §7.
4. **Clear the documentation drift in §9.**

### Horizon B — the ladder and the layers

5. **Sand underground.** Currently the only common surface material with no deep
   source, and glass gates both the Chemistry Bench and the Assembly Table.
6. **Wild wheat and flax generate**, so the grass→seed stopgap can be deleted
   rather than kept.
7. **Tier 3–4 materials** — titanium, tungsten, graphene's source, and the
   refractory insulator DESIGN.md deliberately withheld. Verify ceramic's
   `heatCond` first: at 18 and no melting point, the "no high-temperature
   insulator" claim is softer than it reads.
8. **Layer 3 creatures and its boss**, following the station-not-ore rule.
9. **Play testing.** Nothing in layer 1 or 2 has met a human. The numbers most
   likely to be wrong are listed at the end of PROGRESSION.md §10: husk damage
   against 100 hp with no armour, whether the bat is fun or merely annoying, 900
   boss hp, the 12-chitin summon cost, and whether leading a falling bolt across
   a cavern is satisfying or fiddly.

### Horizon C — the game has an ending

10. **Threading, or the machine-sleep alternative.** Before the late game exists,
    not after.
11. **Blueprints.**
12. **The escape machine**, and with it a reason for every layer below it.
13. **Hardmode materials**, if hardmode is kept.

**The gate on all of Horizon C is Horizon B step 9.** Nobody has played this.
Building the top of the ladder before knowing whether the middle is fun is the
expensive order.

---

## 9. Known documentation drift

These are why it feels hard to find the thread.

- **HANDOFF.md is stale.** See Horizon A step 2.
- **PROGRESSION.md cites verification harnesses that are not in the tree** —
  `reachable.cpp`, `layers.cpp`, `spawn.cpp`, `forge.cpp`. Each is described as
  an *asserting* invariant check ("`spawn.cpp` asserts layers 2 and 3 have no
  creatures", "re-run `reachable.cpp`"). If they were one-off scripts, say so.
  If they should exist, write them — they are precisely the checks §7 needs.
  Layer 2 now has creatures, so at least one of those assertions is void either
  way.
- **DESIGN.md's "Other known work" is largely done** — world bigger than the
  window, player/pixel collision, saving. Its "First milestone" was overtaken
  long ago. Mark the resolved risks resolved, so the two that remain (threading,
  late-game cost) stand out instead of hiding in a list of finished items.
- **MULTIPLAYER.md predates the browser transport.** It presents TCP and
  direct-IP as the only path. `netshim.h` and the broker need a section.
- **README.md stops at the sandbox systems** — no multiplayer, no cave layers,
  no boss, no browser build.

---

## 10. Working on it

```bash
build.bat                      # native game -> build\cinderlift.exe
```

```bash
build_launcher.bat             # installer   -> build\cinderlift-launcher.exe
```

```bash
./build_web.sh                 # wasm        -> web\ (must be served, not file://)
```

- `make` is the g++ equivalent of `build.bat`; `make clean` removes compiler
  output only, never saves.
- Tests build like the tools: every `src/*.cpp` except `main.cpp`, plus the one
  harness file. No runner yet — see Horizon A step 3.
- `multiplayer_test.bat [n]` launches a local host plus n clients over loopback.
- Tagging `v*` cuts a Windows release; pushing to `src/**` or `web/**` publishes
  the site. The two are deliberately uncoupled.
- **`build.bat` needs an absolute path** when run from Git Bash.
- **Windows Defender quarantines emsdk's small launcher `.exe`s**; local web
  builds route around it. CI is unaffected.
- `.claude/` is local user configuration. Do not stage, edit or commit it.

### Diagnostic tools

| tool | answers |
|---|---|
| `tools/profile.cpp` | where a frame goes, per phase, across scenes |
| `tools/lightmargin.cpp` | cost and correctness of the light margin |
| `tools/pulsecheck.cpp` | block-allocated pulse planes vs a reference map |
| `tools/wirecheck.cpp` | circuit behaviour signature, diffable across builds |
| `tools/cover.cpp` | the share image, through the real renderer |
| `tools/rigshot.cpp` | creature rigs, rendered |

---

## 11. Reading order

New here, or back after a month away:

1. **This file** — where things are.
2. **DESIGN.md** — why the game is this game. Short.
3. **PROGRESSION.md §0** — the five things that break silently. Non-negotiable.
4. Then whichever of INTERNALS / CIRCUITS / LOGISTICS / MULTIPLAYER / SPRITES /
   THERMAL_PRESSURE / LAUNCHER covers what you are touching.

INTERNALS.md is a reference, not a read-through. Its material tables at the end
are the part you will come back to.
