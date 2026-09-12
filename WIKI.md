# The wiki — style, intent, and every page in it

The plan for the reference site. Written before any of it, because the decisions
that are cheap now are expensive once two hundred pages exist: where facts come
from, what each kind of page is *for*, and what voice it speaks in.

---

## 0. The one rule about facts

**Anything the game already knows, the game writes. Anything only a person
knows, a person writes. Nothing is in both halves.**

A hand-written page that quotes a number is wrong the first time that number is
tuned, and nothing tells you — it just sits there being confidently wrong. This
month alone spear reach, sky colour, plant cover, trinket slot count and the
whole fuel-to-coke chain changed. A hand-maintained table of 141 recipes would
already be stale.

So the reference half is **generated from the game's own tables**, by a program
that links the game's own code. If a material's ignition point changes, the page
changes the next time the wiki is built, because the page was never told the
number — it asked.

### Regenerated on request, not on push

`scripts/run_wiki.sh` builds the generator, runs it, converts the icon sheet,
and writes `web/wiki/`. You run it when you want the wiki updated. Nothing in CI
does this.

Which means the output **is committed**, unlike the wasm build — the Pages
workflow already uploads all of `web/`, so committed pages publish with the next
deploy and no workflow changes at all. The trade is deliberate: a generated
directory in the diff, in exchange for you deciding when the wiki moves. The
generated files carry a header line naming the commit they were built from, so
"is the wiki stale?" is one `git log` away.

*(Measured, and worth knowing in case this ever does go into CI: the generator's
link set — all of `src/` except `main.cpp` and `network.cpp`, the test-harness
set exactly — builds and links with **zero Windows libraries**. Nothing about
this design is Windows-locked.)*

---

## 1. What is derivable

Live counts from the tables as of v0.6.1 — measured, not estimated:

| Source | Count | Gives us |
|---|---|---|
| `MATS[]` + ~20 `g_mat*` tables | **116 materials** | kind, density, heat conductivity, thermal mass, ignition/boil/cool points and what each becomes, strength, decay, drops-as, passability, plant/seed flags, glow |
| `ITEMS[]` | **293 items**, 290 stackable | name, kind, stack size, equip slot, reach/speed/armour/resist, tool tier and delay, mine radius and yield, damage, what it places or summons, and **180 authored descriptions already written** |
| `RECIPES[]` | **141 recipes** across 6 stations | inputs, outputs, counts, station — and the reverse index, *what is this material used for*, which is the most-wanted page on any wiki and is pure derivation |
| `ENT_DEFS[]` | **27 creatures** | health, damage, speed, depth band, drops |
| `DEVS[]` | **25 devices** | footprint, behaviour, limits |
| `g_sprite[]` + `renderMaterialIcon` | **208 sprites** | **every icon on the site, drawn by the game's own renderer** |

The stations are `Hand, Bench, Anvil, Chemistry Bench, Assembly Table, Blast
Furnace`, and the 16 Hand recipes are the whole of what a new player can make
before building anything — which makes them the spine of the first tutorial.

---

## 2. House style

Applies to every page, generated and written.

**Voice.** Second person, present tense, plain. "Seal the fuel in an iron box."
Not "the fuel must be sealed" and not "we recommend sealing". The game's own item
descriptions already speak this way; the wiki matches them rather than inventing
a second register.

**No marketing.** Nothing is "powerful", "amazing" or "essential". A disruptor
clears 64 cells; the reader can decide whether that is exciting.

**Numbers carry units, always.** °C for temperature, cells for distance and
radius, frames for time (and seconds in brackets the first time on a page, since
60 frames is a second and nobody should have to know that). Raw table values
never appear unconverted — `degC(c) = c + 40` is an implementation detail and
`175` is not a temperature.

**Say what fails.** Every page that describes a process names the way it goes
wrong, because that is the half a player cannot work out alone. "Exposed fuel at
135 °C burns instead of coking" is more useful than the happy path.

**Link, never restate.** A tutorial that needs a number links to the generated
page holding it. This is the one rule from §0 applied to prose, and it is what
stops the heat tutorial from slowly becoming a second, wrong copy of the material
table.

**Spoilers are marked, not avoided.** Boss pages and the endgame exist; they sit
behind a clearly labelled heading rather than being coy or being sprung on
someone reading about copper.

**Look like the game.** Reuse `web/index.html`'s CSS variables verbatim
(`--bg`, `--panel`, `--edge`, `--ink`, `--dim`, `--accent`, `--ember`,
`--alarm`). Dark, flat, no rounded cards, cell-sharp icons with
`image-rendering: pixelated` — a game whose entire look is that the cells are
visible must not have a wiki that smooths them.

**Static HTML, no framework.** Tables ship complete in the HTML; the filter and
sort are a few dozen lines of vanilla JS over markup that already works with JS
off. No JSON fetch, so there is nothing to get out of sync.

---

## 3. Every page type

For each: who it is for, what is on it, and the rule that keeps it honest.

### 3.1 The hub — `/wiki/`

*Intent:* answer "where do I go" in one screen, for two different readers who
arrive for opposite reasons.

Two columns, not one list. **Learn** (the tutorials, in play order) and **Look
up** (the six reference indexes, with their live counts — "116 materials", not
"Materials", because the count tells a reader the table is complete). Below them,
a short "recently changed" line naming the build the wiki was generated from.

*Rule:* no content of its own. The moment the hub starts explaining something it
has become a page that can contradict another page.

### 3.2 Reference index — `/wiki/materials/`, `/items/`, `/creatures/`, `/devices/`

*Intent:* find one row fast, or scan for a pattern ("what burns hottest?").

One wide sortable table, every row a link, icon in the first column. A filter box
and kind/tier chips above it. **Everything is on one page** — 116 rows is nothing
for a browser and paginating it would destroy the one thing a table is good for,
which is sorting the whole set by a column.

*Rule:* the index shows only columns a reader might sort or compare by. Detail
belongs on the detail page. An index that shows everything is a detail page with
bad typography.

### 3.3 Material page — `/wiki/materials/<name>`

*Intent:* the reader is holding this, or standing in it, and wants to know what it
does.

Fixed section order, every material the same, so the page is skimmable by
position:

1. **Icon, name, kind** and the one-line authored description if it has one.
2. **Behaviour** — in prose assembled from the table, not as a stat dump.
   "Sinks through water. Flows freely. Absorbs nothing."
3. **Heat** — conductivity and thermal mass in words plus numbers, then the
   transitions as a small chain: *ignites at 135 °C → Fuel Fire*, *cools below
   X → Y*. This is the most valuable block on the page and the most impossible
   to maintain by hand.
4. **Mining** — strength, what tool tier clears it, what it drops.
5. **Used in** — every recipe consuming it, and every recipe producing it. Both
   directions, derived.
6. **Found** — depth band and generation notes where derivable.

*Rule:* sections with nothing in them are **omitted, not shown empty**. "Heat:
none" on ninety pages teaches the reader to stop reading the heat section.

### 3.4 Item page — `/wiki/items/<name>`

*Intent:* should I make this, and what does it replace?

Same skeleton as a material page, with the stat block specialised by kind — a
mining tool shows radius and yield, worn gear shows slot and bonuses, a weapon
shows damage and pierce. Then **how to get it** (its recipes, with station), and
**what it supersedes or competes with**, derived from the ladder: boots and
jetpacks contest the feet slot, and a page that does not say so is a page that
lets a reader craft the wrong thing.

*Rule:* the authored `description` is quoted verbatim at the top and never
paraphrased. It is the text the player sees in game; two wordings of one item is
how a wiki starts feeling untrustworthy.

### 3.5 Recipe / station page — `/wiki/recipes/<station>`

*Intent:* I am standing at this station — what can it make?

One page per station, in the game's own panel order so the page and the in-game
list agree. Each row: output icon and count, inputs with icons and counts, and a
link to the output's page.

*Rule:* ingredient quantities come from the table and are never rounded or
tidied. A recipe asking for 7 of something looks like a typo and is not one.

### 3.6 Creature page — `/wiki/creatures/<name>`

*Intent:* this just hit me. What is it and what do I do?

Sprite, depth band, health, damage, speed, drops — then **how it behaves**, which
is the hand-written sentence per creature that no table holds ("a bat
double-jumps; leading your shot is wrong"). Then what it drops and what that is
for, linked.

*Rule:* the behaviour sentence is the only hand-written text on an otherwise
generated page, and it lives in a single authored table in the generator so all
27 sit together and a missing one is visible.

### 3.7 Device page — `/wiki/devices/<name>`

*Intent:* wiring this up, and it is not doing what I expected.

Footprint in cells, what it places, its inputs and outputs, and — the reason this
page type exists separately — **its limits, stated numerically**. The heat lamp
caps at 100 °C. That single fact cost a play session to discover, and it is
sitting in `DEVS[]` where a generated page can simply print it.

*Rule:* every limit gets a number or it is not a limit, it is a vibe.

### 3.8 Concept page — `/wiki/guide/heat`, `/circuits`, `/logistics`

*Intent:* how a *system* behaves, which no per-item page can explain.

Hand-written prose with diagrams where they help. These explain mechanism and
causation — the "why refractory is the wrong wall" class of fact, which is a
conclusion and not a table column.

*Rule:* concept pages state no numbers of their own. They name the material and
link to it.

### 3.9 Tutorial page — `/wiki/guide/<n>-<name>`

*Intent:* get a specific thing done, start to finish, by someone who has not done
it before.

Fixed shape, every tutorial:

- **You will need** — a checklist of items with icons, linked.
- **Steps** — numbered, imperative, one action each.
- **When it works** — what success looks like on screen. Players need to know
  they are finished.
- **When it does not** — the failure list. Named symptom → cause → fix. This
  section is the point of the page and is usually the longest.
- **Next** — a link to the tutorial that follows.

*Rule:* a tutorial may not mention anything the reader cannot yet have. The
ordering in §4 exists so this is checkable rather than aspirational.

---

## 4. The tutorials

Explicit and in order. Each is one page, each assumes only the ones above it.

**1. Your first ten minutes.** You spawn holding a Bolter. Move, dig, and make
the four things that matter: torches, a Flint Striker, a Workbench, a bed. Ends
with a bench placed and the crafting panel open at it. *(All 16 Hand recipes are
available here and only a handful are worth naming — the rest are discovery.)*

**2. The dark is not scenery.** Light is what stops things spawning on you.
Torches, where they reach, why a lit room stays empty, and why turning the
lighting display off does not make the dark safe. The single highest-value
survival lesson and currently taught by being killed.

**3. Making fire.** The Flint Striker exists because nothing else in the early
game is hot enough. What ignites at what temperature, what a fire spreads to, and
how to put one out before it takes your bench with it.

**4. Digging properly.** The mining ladder — Hand Drill (18 cells), Rock Auger
(28), Thermal Lance (42), Disruptor (64) — what each one unlocks rather than just
speeds up, and the Harvesting Sickle as the one that is not a tier. Plus the
shape of a tunnel that does not collapse sand on you.

**5. The crafting ladder.** Hand → Bench → Anvil → Chemistry Bench → Assembly
Table → Blast Furnace. What each station is for, what it costs, and the fact that
stations are *placed materials* so they can be moved and lost.

**6. Ore into bars.** Smelting, fuel, and why the furnace refuses. The
temperature each metal needs, laid against what each fuel actually reaches.

**7. Coke, and the sealed retort.** The mechanic that is **invisible in game** —
there is no retort item and no recipe, so a player who does not read the Fuel
tooltip will never find it. Build a box, pack it with fuel, heat it from the
*outside* past 125 °C, vent the gas through Gas Sieve cells, let it cool before
opening. Iron or stone walls, never refractory. This page is the strongest single
argument for having a wiki at all.

**8. Going down.** The three layers, the sealed bands between them, what lives at
each depth and what you need before you try. Where the difficulty actually steps.

**9. Fighting back.** Bolter → Multitool and modules. That damage lives on the
*module*, not the tool, is the thing nobody guesses. Then armour and the
resistance lines.

**10. Gearing up.** Equipment slots, the six trinket slots, the
largest-never-summed rule — which is counter-intuitive and costs players real
material when they stack two cheap lenses expecting addition.

**11. Farming and eating.** Seeds, soil, water, light, and bread. Includes which
seeds are placed as materials and which are used as items, because they are
genuinely inconsistent right now and a tutorial that pretends otherwise will read
as broken.

**12. Bees and wax.** The three wild hives, beeswax versus wax (different
materials — the recipes want beeswax), and what souring a colony with coal does.

**13. Circuits.** `CIRCUITS.md` is already this page.

**14. Logistics.** `LOGISTICS.md` is already this page.

**15. Leaving.** *Marked as endgame.* The Launch Assembly, the rocket, fuel,
crew, countdown, and that "Continue Exploring" gives you the world back intact.

### On the existing markdown

A correction to the checklist's earlier claim that four repo `.md` files are
"most of a wiki already" — **two are, two are not.**

- `CIRCUITS.md` and `LOGISTICS.md` are genuinely player-facing. "Turn on Circuit
  Wire in the left panel (or press X)" is wiki prose as written. These move in
  nearly as-is.
- `THERMAL_PRESSURE.md` is an engineering document: its sections are headed
  "implemented" and it ends in "Required diagnostics". It is the right *source*
  for tutorial 6 and the heat concept page, and the wrong *text* for either.
- `PROGRESSION.md` is a contributor design plan. Publishing it would be a
  mistake rather than a shortcut — it opens with five ways to silently corrupt
  save files and describes unbuilt content as though it shipped.

---

## 5. Layout and mechanics

```
tools/wiki.cpp          the generator. Links all of src/ except main.cpp and
                        network.cpp -- the test-harness set exactly.
scripts/run_wiki.sh     build it, run it, convert the icon sheet. Run by hand.
web/wiki/               output. COMMITTED, with a build stamp in each page.
web/wiki/_src/*.md      prose written FOR the wiki (the tutorials).
CIRCUITS.md, LOGISTICS.md   prose that already exists, read where it lives.
```

Same domain under `/wiki/`, so the Pages deploy stays one artifact and
`index.html` needs no changes.

```
/wiki/                      hub
/wiki/materials/            index + 116 pages
/wiki/items/                index + 290 pages
/wiki/recipes/              6 station pages + the reverse index
/wiki/creatures/            index + 27 pages
/wiki/devices/              index + 25 pages
/wiki/guide/                15 tutorials + 3 concept pages
/wiki/icons.png             one sprite sheet, addressed by CSS offsets
/wiki/wiki.css              one stylesheet
```

**Icons.** The generator writes one PPM sheet and `scripts/ppm_to_png.py`
converts it — exactly the pipeline `tools/cover.cpp` already uses, so no image
library enters the build. One sheet rather than 290 files: one request, and CSS
`background-position` picks the cell.

**Markdown.** Prose sources are named in a table in the generator mapping
source file → output page, so a document is read where it already lives:
`CIRCUITS.md` and `LOGISTICS.md` stay at the repository root rather than being
copied under `web/wiki/`, because a copy is a second thing to keep in step —
the exact failure this document argues against. New prose written for the wiki
goes in `web/wiki/_src/`, and neither is a special case. The generator renders
both with the same template as the
generated pages, so authored and derived pages share one header, nav and
stylesheet. That means a small Markdown subset in C++ — headings, paragraphs,
lists, tables, code, links, bold/italic — a few hundred lines, and it avoids a
Python dependency for the prose half.

---

## 6. The drift guard

A generator that silently emits a broken page is worse than a hand-written one,
because nobody reads it sceptically. So the generator fails loudly, and
`tests/wiki.cpp` runs in the ordinary suite and asserts:

- every stackable item reaches a page, and every page reaches an item
- every recipe's inputs and output resolve to real, named items
- every page the nav links to exists, and nothing is orphaned
- every icon referenced has a non-blank cell in the sheet — the same failure
  `tests/dropped_items.cpp` already guards in game, for the same reason: an
  invisible thing is the one problem a reader cannot work around
- no generated page is empty or truncated
- every `_src/*.md` file is reachable from the nav, and every tutorial has all
  five of its required sections

Without this, "generated from source" only means "wrong in a harder-to-notice
way".

---

## 7. Build order

Each stage ends in something live on the site.

**Stage 1 — the spine.** `tools/wiki.cpp` emitting two pages: the hub and the
material index. Plus `scripts/run_wiki.sh`, the template, the CSS, the icon
sheet and the Markdown subset. Ships a real `/wiki/` with one good table on it.

**Stage 2 — the reference.** Items, recipes both directions, creatures, devices.
The bulk of the content and little of the work — the same template over four more
tables. `tests/wiki.cpp` lands here.

**Stage 3 — the cross-links.** Every page's "used in", "made from", "drops
from", "burns into". All derived. This is what turns 450 pages into a wiki rather
than a dump, and it is exactly the part that would be unmaintainable by hand.

**Stage 4 — the tutorials.** The fifteen pages in §4, in order, plus the three
concept pages. Real authorship, and the only stage whose cost is writing rather
than coding.

**Stage 5 — polish.** Search over the generated index, depth-band and station
navigation, a "what can I make right now" view.

### Afterwards

**A new material, item, recipe, creature or device needs no wiki work at all.**
It appears because it is in the table; you run `run_wiki.sh` and commit. If
something ever needs a hand-written page to be *correct*, the generator is
missing a column — fix the generator, not the page.

---

## 8. What this does not solve

- **A generated page cannot explain why.** It will state that sealed fuel at
  125 °C becomes coke. It will not tell you to build the box from iron rather
  than refractory, because "refractory insulates and will leave your retort
  cold" is a conclusion, not a column. That is what tutorial 7 is for, and the
  coke chain is the proof: the entire mechanic is discoverable in game only by
  reading three tooltips.
- **Nothing here helps a player who never opens a browser.** This is a reference
  for people who go looking. It is not the onboarding item and finishing it does
  not close that one — though tutorials 1–3 are the obvious script for whatever
  in-game hinting eventually lands.
