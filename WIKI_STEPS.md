# The wiki — build steps

The followable version of [WIKI.md](WIKI.md). That document says what the wiki
*is*; this one is the order it gets built in, one step at a time.

**Every step obeys the same three rules:**

1. **It is one commit.** If a step cannot be committed on its own without
   breaking the site, it is two steps.
2. **It names its own verification.** "Done" is a command whose output is
   checked, not a feeling. A step with no way to be wrong is a step that is
   silently wrong.
3. **The site keeps working.** `/wiki/` is live from step 1.8 onward and never
   regresses. Nothing ships half-rendered.

**Progress is tracked by ticking the boxes in this file**, in the same commit as
the work. This file is the source of truth for where we are.

---

## Facts the steps rely on

Measured, so no step rests on a guess:

| Thing | Value | Where |
|---|---|---|
| Link set | all `src/*.cpp` except `main.cpp`, `network.cpp` | the test-harness set; links with **no Windows libs** |
| Materials | `MATS[]`, `MAT_COUNT` = **116** | `materials.h`, plus **38** `g_mat*` parallel tables |
| Items | `ITEMS[]`, `ITEM_COUNT` = **293**, 290 stackable, 180 described | `item.h` |
| Recipes | `RECIPES[]`, `N_RECIPES` = **141** | `craft.h` |
| Stations | `STATION_NAMES[]`, 6: Hand, Bench, Anvil, Chemistry Bench, Assembly Table, Blast Furnace | `craft.h` |
| Creatures | `ENT_DEFS[]`, `ENT_COUNT` = **27** — `name,w,h,hp,touchDamage,speed,flies,layerMask,shotEvery,shotDamage,isBoss,dropMin/Max,sprite,heatTolerance` | `entity.h:250` |
| Devices | `DEVS[]`, `DEV_COUNT` = **25** | `device.h` |
| **Icons** | `dropArt(u16)` → a **14×14** `u32` canvas for **every** stackable item | `item.h:1148`; `dropArtBottom()` at 1155 |
| Icon safety | all 290 already proven non-blank, shaped, correctly anchored | `tests/dropped_items.cpp` |
| PNG path | generator writes PPM → `scripts/ppm_to_png.py` | same as `tools/cover.cpp` |
| Palette | `--bg --panel --edge --ink --dim --accent --ember --alarm` | `web/index.html:89` |

`dropArt()` is the single most useful discovery here: one call covers materials
and non-materials alike, and the in-game test already guarantees every canvas is
visible. The icon sheet is a loop over it.

---

## Stage 1 — the spine

Ships a live `/wiki/` with one genuinely good page. Proves every mechanism the
other four stages assume.

- [ ] **1.1 The generator exists and runs.**
      `tools/wiki.cpp` writing a single hard-coded `web/wiki/index.html`.
      `scripts/run_wiki.sh` compiling it against the link set and running it.
      *Verify:* `bash scripts/run_wiki.sh` exits 0; `web/wiki/index.html` is
      non-empty.
      *Why first:* it tests the riskiest assumption — that the link set builds
      and the output path is right — before any content depends on it.

- [ ] **1.2 The page template.**
      One function taking title + body and emitting doctype, `<head>`, nav,
      footer. Footer carries the **build stamp**: the short commit the generator
      was built from, passed in as `-DWIKI_BUILD_ID` exactly as `build_web.sh`
      does for the game.
      *Verify:* the stamp in the HTML equals `git rev-parse --short=12 HEAD`.

- [ ] **1.3 The stylesheet.**
      `web/wiki/wiki.css`, variables copied verbatim from `index.html`. Dark,
      flat, no rounded cards, `image-rendering: pixelated` on every icon.
      *Verify:* open it in the browser beside the main page; they look like one
      project. This step is judged by eye, and says so.

- [ ] **1.4 The icon sheet.**
      Loop `dropArt()` over every stackable item into one PPM grid at 14×14 per
      cell; `ppm_to_png.py` → `web/wiki/icons.png`. Emit the CSS offset class per
      item into `wiki.css`.
      *Verify:* the sheet has exactly one cell per stackable item; **no cell is
      blank** (the generator refuses to finish if one is); the PNG opens and the
      icons are crisp, not smoothed.

- [ ] **1.5 The material index.**
      `/wiki/materials/` — all 116 rows, icon + name + kind + density +
      conductivity + ignition point, sortable by any column, with a filter box.
      Table complete in the HTML; JS only sorts and filters.
      *Verify:* 116 `<tr>`; spot-check three rows against `MATS[]` by hand;
      disable JS and confirm the table still reads.

- [ ] **1.6 The Markdown subset.**
      Headings, paragraphs, lists, tables, fenced code, links, bold/italic.
      Enough for `CIRCUITS.md` and no more.
      *Verify:* render `CIRCUITS.md`; its 5 headings and its table survive;
      nothing is emitted as literal `##`.

- [ ] **1.7 The hub.**
      `/wiki/` — two columns, **Learn** and **Look up**, the latter with live
      counts ("116 materials"). No content of its own.
      *Verify:* every link resolves to a file that exists.

- [ ] **1.8 Publish.**
      Confirm no `.gitignore` rule swallows `web/wiki/`, commit the output, push,
      and check the live site. Add the `/wiki/` link to `web/index.html`.
      *Verify:* `git status` lists the generated files as tracked; the deployed
      page loads over HTTPS. **Remember Pages serves `max-age=600`** — a stale
      view for up to ten minutes is the cache, not a bug.

---

## Stage 2 — the reference

The bulk of the content and little of the work: the same template over four more
tables.

- [ ] **2.1 Material detail pages.** 116 pages, the §3.3 section order, empty
      sections **omitted not blank**.
      *Verify:* every index row links to a page that exists; three pages
      hand-checked against the tables; no page contains the word "none".

- [ ] **2.2 Item index and item pages.** 290 pages, stat block specialised by
      kind, the authored `description` quoted **verbatim**.
      *Verify:* all 180 descriptions appear byte-identical to `ITEMS[].description`.

- [ ] **2.3 Recipe pages.** One per station, in the game's own panel order.
      *Verify:* the 141 recipes appear exactly once each, summed across the six
      pages; input counts match the table with no rounding.

- [ ] **2.4 Creature index and pages.** 27 pages grouped by depth band, bosses
      marked as spoilers behind a heading.
      *Verify:* every `ENT_DEFS` entry has a page; `isBoss` entries are all
      inside the spoiler section.

- [ ] **2.5 Device pages.** 25 pages, footprint, behaviour, and **limits with
      numbers**.
      *Verify:* the heat lamp page states 100 °C. That is the canary — if the
      number a play session was lost to is not on the page, the page type has
      failed at its one job.

- [ ] **2.6 `tests/wiki.cpp`.** The §6 drift guard: items ↔ pages both ways,
      recipes resolve, nav links exist, nothing orphaned, no icon blank, no page
      empty or truncated, every `_src/*.md` reachable.
      *Verify:* it passes — then **break one thing on purpose** (delete a page,
      blank an icon) and confirm it fails. A guard never seen to fail is not
      known to work.
      *Note:* the harness runs from `build/tbin`, so paths resolve from there,
      not the repo root.

---

## Stage 3 — the cross-links

What turns 450 pages into a wiki rather than a dump — and the part that would be
flatly unmaintainable by hand.

- [ ] **3.1 "Used in" and "made from".** Both directions of `RECIPES[]` on every
      material and item page.
      *Verify:* pick a material used by several recipes; every one appears.
      Confirm the two directions are consistent — if A is "used in" B, B's page
      says "made from" A.

- [ ] **3.2 Heat chains.** `igniteTemp → burnsTo`, `boilTemp → boilsTo`,
      `coolTemp → coolsTo`, rendered as a linked chain, plus `g_matDecaysTo`.
      *Verify:* walk the fuel chain — Fuel → Coke → Coke Ember — entirely by
      clicking, and check every temperature against the table.

- [ ] **3.3 Mining and drops.** `g_matStrength` → which tool tier clears it,
      `g_matDropsAs`, `g_matSmeltYield`.
      *Verify:* a material whose drop differs from itself shows the difference;
      the tool tier named actually has the radius to do it.

- [ ] **3.4 Creature drops and their uses.** Drop → item page → what it is for.
      *Verify:* a charm reachable from its creature in two clicks.

- [ ] **3.5 Orphan sweep.** Extend `tests/wiki.cpp`: every page reachable from
      the hub in ≤3 clicks, and nothing links to a page that links nowhere.
      *Verify:* the test reports the click depth, so a regression is visible as a
      number rather than a pass/fail.

---

## Stage 4 — the tutorials

The only stage whose cost is writing. Fifteen pages from WIKI.md §4, each with
all five required sections (**You will need / Steps / When it works / When it
does not / Next**), in dependency order so "mentions nothing the reader cannot
yet have" is checkable.

- [ ] **4.1 Concept pages move in.** `CIRCUITS.md` → `/wiki/guide/circuits`,
      `LOGISTICS.md` → `/wiki/guide/logistics`, near-verbatim (tutorials 13–14).
      *Verify:* both render fully; every number they quote is replaced by a link
      to the generated page holding it.

- [ ] **4.2 The first three.** *Your first ten minutes*, *The dark is not
      scenery*, *Making fire*. The highest-value pages on the site: light and
      ignition are currently taught by dying.
      *Verify:* read 1 as someone with nothing, and check every item named is
      reachable from the 16 Hand recipes or the starting kit.

- [ ] **4.3 Tools and the ladder.** *Digging properly*, *The crafting ladder*.
      *Verify:* the four mining radii (18/28/42/64) come from the table, not the
      prose.

- [ ] **4.4 Heat and the retort.** *Ore into bars*, then **tutorial 7, the coke
      retort** — the page that justifies the project, since there is no retort
      item and no recipe and the whole mechanic is invisible in game.
      *Verify:* follow the page in a real game and produce coke without
      consulting anything else. If that fails, the page is wrong, not the reader.

- [ ] **4.5 Depth and combat.** *Going down*, *Fighting back*, *Gearing up*.
      *Verify:* "damage lives on the module, not the tool" and the
      largest-never-summed rule are both stated outright.

- [ ] **4.6 The soft systems.** *Farming and eating*, *Bees and wax*.
      *Verify:* the seed inconsistency is described honestly rather than
      smoothed over, and beeswax is distinguished from wax — the recipes want
      **beeswax**, and a page that conflates them sends the reader after a
      material with no source.

- [ ] **4.7 The ending.** *Leaving*, behind a spoiler heading.
      *Verify:* nothing on any non-spoiler page links into it without warning.

- [ ] **4.8 The heat concept page.** Written **from** `THERMAL_PRESSURE.md`, not
      copied from it — the source ends in "Required diagnostics" and is an
      engineering document.
      *Verify:* no "implemented" headings survive; no diagnostics section.

---

## Stage 5 — polish

- [ ] **5.1 Search.** Client-side over a generated index; works with JS off by
      degrading to the indexes.
- [ ] **5.2 Navigation by shape.** Depth bands for creatures, stations for
      recipes, tiers for items.
- [ ] **5.3 "What can I make now?"** Pick the tools and stations you have; the
      page filters the 141 recipes to what is reachable.
- [ ] **5.4 A staleness check.** `run_wiki.sh` warns when the committed output's
      build stamp is behind `HEAD`, so a stale wiki announces itself.

---

## The rule for afterwards

**A new material, item, recipe, creature or device needs no wiki work.** It
appears because it is in the table: run `scripts/run_wiki.sh`, commit, push.

If something ever needs a hand-written page to be *correct*, the generator is
missing a column. Fix the generator, not the page.
