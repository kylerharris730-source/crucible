# The wiki — build steps

The followable version of [WIKI.md](WIKI.md). That document says what the wiki
*is*; this one is the order it gets built in, one step at a time.

**Every step obeys the same three rules:**

1. **It is one commit.** If a step cannot be committed on its own without
   breaking the site, it is two steps.
2. **It names its own verification.** "Done" is a command whose output is
   checked, not a feeling. A step with no way to be wrong is a step that is
   silently wrong.
3. **The site keeps working.** `/wiki/` has been live since step 1.8 and never
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

- [x] **1.1 The generator exists and runs.**
      `tools/wiki.cpp` writing a single hard-coded `web/wiki/index.html`.
      `scripts/run_wiki.sh` compiling it against the link set and running it.
      *Verify:* `bash scripts/run_wiki.sh` exits 0; `web/wiki/index.html` is
      non-empty.
      *Why first:* it tests the riskiest assumption — that the link set builds
      and the output path is right — before any content depends on it.
      *Done 2026-09-11.* Links clean with **no libraries at all**, as predicted.
      Objects cache in `build/wikiobj` (29 files once, then nothing), and the
      generator refuses to run outside the repository root rather than writing
      `tools/web/wiki` and publishing nothing. The counts it prints are live, so
      the tables are genuinely reachable: 116 / 293 / 290 / 180 / 141 / 6 / 27 /
      25 / 208, matching the plan exactly.

- [x] **1.2 The page template.**
      One function taking title + body and emitting doctype, `<head>`, nav,
      footer. Footer carries the **build stamp**: the short commit the generator
      was built from, passed in as `-DWIKI_BUILD_ID` exactly as `build_web.sh`
      does for the game.
      *Verify:* the stamp in the HTML equals `git rev-parse --short=12 HEAD`.
      *Done 2026-09-11.* Stamp matches, and carries the game version beside it.
      Two things fell out of building it: nav entries carry a `built` flag so a
      section appears only once its pages exist — which is what keeps "the site
      never regresses" true at every commit rather than only at the end — and
      every link is depth-relative, so a generated page can be opened straight
      off disk to proofread. A page that only works when served is a page nobody
      proofreads. The stamp defines go to the generator's translation unit only;
      on all 29 shared objects they would invalidate the whole cache every run.

- [x] **1.3 The stylesheet.**
      `web/wiki/wiki.css`, variables copied verbatim from `index.html`. Dark,
      flat, no rounded cards, `image-rendering: pixelated` on every icon.
      *Verify:* open it in the browser beside the main page; they look like one
      project. This step is judged by eye, and says so.
      *Done 2026-09-11.* Checked served over `python -m http.server 8099 -d web`,
      **not** over `file://` — a local file opens as a snapshot with no
      stylesheet resolved, which looks exactly like a broken page. Worth knowing
      for every later visual check. One thing the eye caught: the brand and nav
      inherited the prose-link underline; chrome is furniture, not prose.

- [x] **1.4 The icon sheet.**
      Loop `dropArt()` over every stackable item into one PPM grid at 14×14 per
      cell; `ppm_to_png.py` → `web/wiki/icons.png`. Emit the CSS offset class per
      item into `wiki.css`.
      *Verify:* the sheet has exactly one cell per stackable item; **no cell is
      blank** (the generator refuses to finish if one is); the PNG opens and the
      icons are crisp, not smoothed.
      *Done 2026-09-11.* 224×266, **290 cells, 290 CSS classes**, 14 KB.
      One thing the plan had wrong: PPM cannot carry this. Art stores 0 for
      transparent and flattening it onto any background colour makes every icon
      a rectangle of that colour — exactly the "big squares" complaint that
      started the dropped-item work. `ppm_to_png.py` now also reads **PAM
      (P7 / RGB_ALPHA)** and emits colour type 6; the P6 path `cover.cpp` uses is
      regression-tested and unchanged. The `.pam` is an intermediate like a
      `.o`, converted and deleted, and gitignored.

- [x] **1.5 The material index.**
      `/wiki/materials/` — all 116 rows, icon + name + kind + density +
      conductivity + ignition point, sortable by any column, with a filter box.
      Table complete in the HTML; JS only sorts and filters.
      *Verify:* 116 `<tr>`; spot-check three rows against `MATS[]` by hand;
      disable JS and confirm the table still reads.
      *Done 2026-09-11.* **115 rows, not 116** — `MAT_COUNT` counts `MAT_EMPTY`,
      and air is not a substance anyone looks up. Spot-checks match (Stone
      255/85, melts to lava at 185 °C; Water 100/180, boils 100, freezes 0).
      Sort and filter both work; no row carries `hidden` in the source, so the
      table is complete with JS off.
      Two things the build caught. **`MATS[]` temperatures are stored offset by
      40**, so printing the raw byte would have put "175" on the page for
      something that ignites at 135 °C — the house rule about units broken by
      one subtraction. And the hot/cold columns cannot be called "Boils" and
      "Freezes": the same field melts stone to lava and cooks sand to glass, so
      the commonest case would be a plainly wrong word on two thirds of the
      rows. They are "Melts or boils" and "Freezes or sets".

- [x] **1.6 The Markdown subset.**
      Headings, paragraphs, lists, tables, fenced code, links, bold/italic.
      Enough for `CIRCUITS.md` and no more.
      *Verify:* render `CIRCUITS.md`; its 5 headings and its table survive;
      nothing is emitted as literal `##`.
      *Done 2026-09-11.* 5 headings (1 h1 + 4 h2), no literal `##`; code spans,
      links, bold and lists all render. **`CIRCUITS.md` has no table**, so the
      table path was exercised against a fixture rather than shipped untested —
      header row, alignment row consumed, inline markup inside cells, and a
      fence leaving `|` and `**` literal.
      One bug found by looking at the page, not the code: hard-wrapped **list
      items** dropped their continuation into a paragraph after the list, so a
      bullet ended mid-sentence and loose text followed it. Paragraphs and list
      items now share one continuation routine; paragraphs on the circuits page
      fell 16 → 10.
      Plan change recorded in WIKI.md: prose sources are named in a table and
      read where they already live, instead of being copied into
      `web/wiki/_src/`. A copy is a second thing to keep in step.

- [x] **1.7 The hub.**
      `/wiki/` — two columns, **Learn** and **Look up**, the latter with live
      counts ("116 materials"). No content of its own.
      *Verify:* every link resolves to a file that exists.
      *Done 2026-09-11.* **39 links across the site, 0 broken**, checked by
      walking every generated page. The "Look up" column is driven by the same
      `SECTIONS` table the nav is, so the hub cannot offer a door the nav has
      not got — or one whose pages do not exist yet. Two columns at desktop
      width, one on a narrow screen; checked at both rather than assumed.

- [x] **1.8 Publish.**
      Confirm no `.gitignore` rule swallows `web/wiki/`, commit the output, push,
      and check the live site. Add the `/wiki/` link to `web/index.html`.
      *Verify:* `git status` lists the generated files as tracked; the deployed
      page loads over HTTPS. **Remember Pages serves `max-age=600`** — a stale
      view for up to ten minutes is the cache, not a bug.
      *Done 2026-09-11.* Nothing under `web/wiki/` is ignored but the `.pam`
      intermediate; 54 relative links across the whole of `web/` resolve, and
      the game page links to the wiki from both its header and its call to
      action. Pages already triggers on `web/**`, so no workflow change.
      One thing publishing turned up that the plan had missed: **`sitemap.xml`
      listed one URL and asked in its own comment to be updated by hand**, which
      is fine for one page and a broken promise by page four hundred. The
      generator now writes `wiki/sitemap.xml` from the pages it actually wrote
      and `robots.txt` names it as a second sitemap. A reference site that
      cannot be found by search is a reference site nobody reads, so that is
      part of publishing rather than a nicety.

---

## Stage 2 — the reference

The bulk of the content and little of the work: the same template over four more
tables.

- [x] **2.1 Material detail pages.** 116 pages, the §3.3 section order, empty
      sections **omitted not blank**.
      *Verify:* every index row links to a page that exists; three pages
      hand-checked against the tables; no page contains the word "none".
      *Done 2026-09-11.* **115 pages, 115 row links, 0 missing**, and no page
      says "none". Slug collisions are a hard failure rather than a silent
      overwrite.
      The hand-check earned its place twice. A "cheapest tool that clears it"
      column was written first and is **wrong**: every mining tier carries the
      same `minePower` on purpose — the ladder is speed and reach, not
      hardness — so it printed "Hand Drill or better" on every page and read as
      a tier gate that does not exist. It now says so outright. And liquids were
      getting a Mining section: a fluid&rsquo;s strength means "a shot spends
      pierce crossing it", not "bring a better pick", so that fact moved to
      Behaviour where it is true.

- [x] **2.2 Item index and item pages.** 290 pages, stat block specialised by
      kind, the authored `description` quoted **verbatim**.
      *Verify:* all 180 descriptions appear byte-identical to `ITEMS[].description`.
      *Done 2026-09-11.* **176 item pages; all 180 descriptions byte-identical**
      — checked by dumping `ITEMS[].description` from the game and diffing
      against the rendered pages, materials included.
      Scope call: the Items section covers only ids at or above `MAT_COUNT`.
      Materials share the item id space and already have richer pages of their
      own, so listing them twice would give a reader two pages about stone that
      agree today and could disagree tomorrow. The index says so rather than
      quietly being short.
      A `static_assert` ties the kind labels to `enum ItemKind`: a new kind
      would otherwise be labelled by whatever sat at that index — silently, and
      plausibly.
      A whole-site link sweep (3,632 links) found **8 dead links to
      `empty.html`** — materials whose transition target is `MAT_EMPTY`, which
      is an ordinary outcome (fire burns out) but has no page. Now written as
      "nothing — it is gone", which is also what it means. Reading the code did
      not find this; sweeping the output did.

- [x] **2.3 Recipe pages.** One per station, in the game's own panel order.
      *Verify:* the 141 recipes appear exactly once each, summed across the six
      pages; input counts match the table with no rounding.
      *Done 2026-09-11.* **141 rows, 16/22/40/5/43/15**, matching the per-station
      counts exactly. The generator hard-fails if any recipe reaches no page — a
      station id outside the table would otherwise drop its recipes off the site
      silently, and a reader cannot notice the absence of something they never
      knew existed. Rows are in `RECIPES[]` order so the page and the in-game
      panel agree; a wiki that sorts them "better" than the game is one you
      cannot read alongside it.

- [x] **2.4 Creature index and pages.** 27 pages grouped by depth band, bosses
      marked as spoilers behind a heading.
      *Verify:* every `ENT_DEFS` entry has a page; `isBoss` entries are all
      inside the spoiler section.
      *Done 2026-09-11.* **26 pages, 22 ordinary + 4 bosses**, and all four
      bosses (Brood Mother, Widow, the Censer, the Effigy) are inside the
      spoiler block. Grouped by depth, because "am I too deep" is the most
      useful thing the section can answer.
      **Known gap: creature pages have no art.** Creature sprites are their own
      canvases at six sizes, from 22×36 up to the Effigy&rsquo;s 96×112, so a
      second variable-cell sheet is a piece of work rather than a line of this
      step. Recorded here instead of quietly shipping text pages as though that
      were always the intent — see 5.5.

- [x] **2.5 Device pages.** 25 pages, footprint, behaviour, and **limits with
      numbers**.
      *Verify:* the heat lamp page states 100 °C. That is the canary — if the
      number a play session was lost to is not on the page, the page type has
      failed at its one job.
      *Done 2026-09-11.* **The canary passes.** The page reads: "It cannot go
      past 100 °C. That is the limit of the machine, not of your settings."
      Every device prints its range and its ceiling in words, not only as a
      number a reader might skim past.
      *Committed together with 2.4* — the two were built in one pass and could
      not be separated into two honest commits afterwards. Noted rather than
      faked.

- [x] **2.6 `tests/wiki.cpp`.** The §6 drift guard: items ↔ pages both ways,
      recipes resolve, nav links exist, nothing orphaned, no icon blank, no page
      empty or truncated, every `_src/*.md` reachable.
      *Verify:* it passes — then **break one thing on purpose** (delete a page,
      blank an icon) and confirm it fails. A guard never seen to fail is not
      known to work.
      *Note:* the harness runs from `build/tbin`, so paths resolve from there,
      not the repo root.
      *Done 2026-09-11.* **75 tests pass** (was 74). It checks 354 pages and
      5,898 internal links against the live tables.
      Proven non-vacuous by breaking four things on purpose, each caught by the
      right check: a deleted page, a page truncated to 300 bytes, a removed icon
      class (289 for 290 items), and one dead link.
      The design decision that matters: it reads the **committed output on
      disk** and compares it to the tables, so it is also the staleness alarm
      this manual-regeneration design needs. Between a change to the game and
      the next `run_wiki.sh`, the published site is wrong and nothing else would
      notice. A failure therefore reads "run `scripts/run_wiki.sh`" rather than
      "the generator is broken".
      `slugify` is deliberately duplicated from the generator rather than
      shared: a shared header would let both drift together, where two copies
      mean a changed naming rule makes every page "go missing" loudly.

---

## Stage 3 — the cross-links

What turns 450 pages into a wiki rather than a dump — and the part that would be
flatly unmaintainable by hand.

- [x] **3.1 "Used in" and "made from".** Both directions of `RECIPES[]` on every
      material and item page.
      *Verify:* pick a material used by several recipes; every one appears.
      Confirm the two directions are consistent — if A is "used in" B, B's page
      says "made from" A.
      *Done 2026-09-11.* **Copper lists all 34 of its uses**, matching the
      measured count, and **308 forward references all have a matching
      reverse** — checked by walking every page rather than asserted. Both
      directions come out of one pass over one table, so they cannot disagree by
      construction. Each line also names the station, because the errand a
      greyed-out recipe sends you on is half the information.
      `MAX_REFS` was set to 24 by guess and Iron failed the build immediately;
      measured, the busiest ingredient is Copper at 34, so the cap is 64 and
      overflow is a hard failure. A page silently missing half its uses is worse
      than a build that stops, because nobody would notice.
      Creature drops are on the same block — "dropped by" is the other half of
      "where do I get one", and no recipe table holds it.

- [x] **3.2 Heat chains.** `igniteTemp → burnsTo`, `boilTemp → boilsTo`,
      `coolTemp → coolsTo`, rendered as a linked chain, plus `g_matDecaysTo`.
      *Verify:* walk the fuel chain — Fuel → Coke → Coke Ember — entirely by
      clicking, and check every temperature against the table.
      *Done 2026-09-11.* The walk **failed first time**, and the failure is the
      point: Fuel linked only to Fuel Fire. **Fuel → Coke is not in any table** —
      it is the retort rule in `world.cpp`, and a retort is not an item, not a
      recipe and not a row, so no amount of reading the tables finds it.
      Fixed with an explicitly authored `CODE_RULES[]` list in the generator —
      a LIST rather than a loosened rule, the same shape
      `tests/item_descriptions.cpp` already uses. Four entries, all in the coke
      chain. Kept short on purpose: a long list there means the generator is
      missing a column. The drift guard checks it by name, since an authored
      list is the one part that can rot.
      Also added, all genuinely derivable: alloying, wetting and dissolving,
      which are reactions with a partner rather than with a temperature.
      Temperatures re-checked against `MATS[]`: Fuel ignites 135 °C, Coke
      150 °C, thermal mass 2× and 4×. All correct.

- [x] **3.3 Mining and drops.** `g_matStrength` → which tool tier clears it,
      `g_matDropsAs`, `g_matSmeltYield`.
      *Verify:* a material whose drop differs from itself shows the difference;
      the tool tier named actually has the radius to do it.
      *Done 2026-09-11.* Four materials drop something other than themselves
      (the two seed pods, Flower → Flower Seed, Open Door → Door) and each
      shows it. The second half of this check is **moot** and 2.1 says why: the
      mining ladder is uniform by design, so there is no tier to name.

- [x] **3.4 Creature drops and their uses.** Drop → item page → what it is for.
      *Verify:* a charm reachable from its creature in two clicks.
      *Done 2026-09-11.* **One click, both ways.** Rock Mite → Carapace Charm,
      and the charm page says "Dropped by Rock Mite — rarely, about 1 kill in
      50". Landed with 2.4 and 3.1 rather than needing work of its own.

- [x] **3.5 Orphan sweep.** Extend `tests/wiki.cpp`: every page reachable from
      the hub in ≤3 clicks, and nothing links to a page that links nowhere.
      *Verify:* the test reports the click depth, so a regression is visible as a
      number rather than a pass/fail.
      *Done 2026-09-11.* **355 pages reachable, furthest 2 clicks**, 7,765
      internal links (5,898 before the cross-links).
      Both branches proven non-vacuous. Stripping Tungsten&rsquo;s link from the
      index did **not** orphan it — cross-links still reached it — but the depth
      moved 2 → 3, which is exactly the regression signal a number gives and a
      pass/fail would have hidden. Stripping Clone&rsquo;s link, which nothing
      else references, failed the check outright.

---

## Stage 4 — the tutorials

The only stage whose cost is writing. Fifteen pages from WIKI.md §4, each with
all five required sections (**You will need / Steps / When it works / When it
does not / Next**), in dependency order so "mentions nothing the reader cannot
yet have" is checkable.

- [x] **4.1 Concept pages move in.** `CIRCUITS.md` → `/wiki/guide/circuits`,
      `LOGISTICS.md` → `/wiki/guide/logistics`, near-verbatim (tutorials 13–14).
      *Verify:* both render fully; every number they quote is replaced by a link
      to the generated page holding it.

- [x] **4.2 The first three.** *Your first ten minutes*, *The dark is not
      scenery*, *Making fire*. The highest-value pages on the site: light and
      ignition are currently taught by dying.
      *Verify:* read 1 as someone with nothing, and check every item named is
      reachable from the 16 Hand recipes or the starting kit.

- [x] **4.3 Tools and the ladder.** *Digging properly*, *The crafting ladder*.
      *Verify:* the four mining radii (18/28/42/64) come from the table, not the
      prose.

- [x] **4.4 Heat and the retort.** *Ore into bars*, then **tutorial 7, the coke
      retort** — the page that justifies the project, since there is no retort
      item and no recipe and the whole mechanic is invisible in game.
      *Verify:* follow the page in a real game and produce coke without
      consulting anything else. If that fails, the page is wrong, not the reader.

- [x] **4.5 Depth and combat.** *Going down*, *Fighting back*, *Gearing up*.
      *Verify:* "damage lives on the module, not the tool" and the
      largest-never-summed rule are both stated outright.

- [x] **4.6 The soft systems.** *Farming and eating*, *Bees and wax*.
      *Verify:* the seed inconsistency is described honestly rather than
      smoothed over, and beeswax is distinguished from wax — the recipes want
      **beeswax**, and a page that conflates them sends the reader after a
      material with no source.

- [x] **4.7 The ending.** *Leaving*, behind a spoiler heading.
      *Verify:* nothing on any non-spoiler page links into it without warning.

- [x] **4.8 The heat concept page.** Written **from** `THERMAL_PRESSURE.md`, not
      copied from it — the source ends in "Required diagnostics" and is an
      engineering document.
      *Verify:* no "implemented" headings survive; no diagnostics section.

---

### Stage 4, as built

All eight steps landed in one pass: **15 tutorials + 1 concept page**, 370 pages
on the site, 8,170 links, 0 broken.

The generator **refuses to build** a tutorial missing any of its five sections
(You will need / Steps / When it works / When it does not / Next) — proven by
renaming one heading and watching the build stop.

**The strongest check on the site now exists.** A tutorial is authored, so it is
the one part that can be wrong about the game; full prose cannot be checked
mechanically but the load-bearing *numbers* can. `tests/wiki.cpp` re-reads iron
ore&rsquo;s melting point, coke ember&rsquo;s burn temperature and the heat
lamp&rsquo;s cap out of the tables and confirms the tutorials still quote them.
Proven by **retuning iron ore from 190 °C to 196 °C in `materials.cpp`** and
watching the suite fail with "the smelting guide does not quote iron ore's
196 °C" — the wiki untouched. That is the project&rsquo;s central claim
demonstrated on the hand-written half.

Three things the writing turned up, all corrections to what I had written:

1. **Left-click builds, right-click digs.** The opposite of the assumption, and
   the first thing a new player hits.
2. **The starting weapon is the "Bolt Caster"**, not the "Bolter" — that is the
   enum name, not the display name.
3. **Flower Seed has been fixed since `CHECKLIST.md` was written.** It is now
   `ITEMK_MATERIAL` and in `g_matIsSeed`, so it germinates where it settles like
   oak and birch. Only **Grass Seed** still uses the convert-a-cell verb. The
   tutorial had to be rewritten — it described the old broken behaviour, which
   would have been actively wrong on a published page. See the note under
   "Flowers are planted the wrong way" in `CHECKLIST.md`.

---

## Stage 5 — polish

- [x] **5.1 Search.** Client-side over a generated index; works with JS off by
      degrading to the indexes.
      *Done 2026-09-12.* **363 pages indexed.** "iron" returns 7, with Iron
      before Iron Sword before Molten Iron — prefix matches ordered ahead of
      substring ones, which is the only cleverness in it. Deliberately a plain
      substring match: a fuzzy matcher that silently reorders is worse than no
      search on a reference site, because a reader cannot tell whether what they
      wanted is absent or merely ranked fourteenth.
      The index is the one second copy of anything on the site, so worth being
      precise: WIKI.md forbids a copy that can DISAGREE, and this one is emitted
      in the same pass from the same page list, so it cannot name a page that
      does not exist or miss one that does. A projection, not a duplicate.
      Written as JS rather than JSON so there is no fetch — a `file://` copy of
      the wiki searches as readily as the served one. `?q=` pre-fills, so a
      search is a shareable link.
- [x] **5.2 Navigation by shape.** Depth bands for creatures, stations for
      recipes, tiers for items.
      *Done 2026-09-12.* Depth chips on the creature index, **cumulative** —
      "Down to layer 1" gives 6 of 22, because the question is "what might I
      meet on the way down", not "what is filed under layer 2". A creature is
      ranked by the shallowest layer it spawns in.
      **Tiers for items were dropped, and the reason is worth keeping:** items
      have no tier column. There is nothing to derive, and inventing one by
      hand would be exactly the hand-maintained fact this project exists to
      avoid. The kind chips already there are the real, derivable axis.
      Also fixed: bosses have no layer bits, which read as "nowhere it spawns on
      its own" in a table cell. For a boss that is the design rather than an
      oddity, so it now says "summoned".
- [x] **5.3 "What can I make now?"** Pick the tools and stations you have; the
      page filters the 141 recipes to what is reachable.
      *Done 2026-09-12.* All 141 in one table, station chips **cumulative** —
      picking Bench gives **38 of 141**, which is 16 hand + 22 bench exactly,
      because a bench does not stop you making things by hand.
      The cumulative rule lives in the markup (`class="cumulative"`,
      `data-kind` as a rank) rather than as a special case in the script, so one
      filter serves both this page and the creature depths.
- [x] **5.5 Creature art.** A second sprite sheet for the 26 creatures. Their
      canvases are six different sizes (22×36 up to 96×112), so unlike the item
      sheet the cells are not uniform and the packing is real work. Raised by
      2.4, which shipped creature pages as text.
      *Done 2026-09-12 — and the premise above was wrong.* Measured, only
      **five** creatures are rig-drawn at their own sizes (Shambler, Thresher,
      Widow, Censer, Effigy). The other **21 are ordinary 14×14 sprites**,
      exactly the size the item sheet already uses. So there is no second sheet
      and no non-uniform packing: 21 more cells on the sheet that exists, sheet
      224×280, **311 cells**, 15 KB.
      The five keep no icon and say why on their own pages — squeezing a
      96×112 boss into a 14×14 cell would make it look small, which is the one
      thing a boss must not look like.
      A worked example of the rule this project keeps relearning: the step was
      filed as "real work" on an assumption, and measuring it first turned a
      project into a loop.
- [x] **5.4 A staleness check.** `run_wiki.sh` warns when the committed output's
      build stamp is behind `HEAD`, so a stale wiki announces itself.
      *Done 2026-09-12.* Reads the stamp out of the page already on disk and
      counts the commits since. Proven by rewriting the stamp three commits back:
      "the committed wiki was built from ce18e734c1ef, 3 commit(s) back".
      A notice, not an error — being behind is the normal state at the moment
      you run this script, which is precisely when it fires.

---

## The rule for afterwards

**A new material, item, recipe, creature or device needs no wiki work.** It
appears because it is in the table: run `scripts/run_wiki.sh`, commit, push.

If something ever needs a hand-written page to be *correct*, the generator is
missing a column. Fix the generator, not the page.
