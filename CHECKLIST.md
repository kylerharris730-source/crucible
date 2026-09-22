# Cinderlift checklist

What is outstanding. Tracked in git on purpose — this used to live only in
chat, which meant it was re-derived from memory every session and quietly lost
things. `HANDOFF.md` is **not** this file: it is gitignored scratch, is usually
stale, and should not be trusted for release state.

Released: **v0.6.7** (2026-09-22). `main` is level with it.

---

## Needs you — I cannot do these from here

- [ ] **Report the launcher to Microsoft as a false positive.** Submit
      `cinderlift-launcher.exe` at
      <https://www.microsoft.com/en-us/wdsi/filesubmission>. Treats the
      symptom, one build at a time.
- [ ] **Get the binaries code-signed.** This is the actual fix for the
      antivirus problem, and it also removes the SmartScreen "unknown
      publisher" warning. **SignPath Foundation** is free for open-source
      projects. Everything on our side is already in place — `res/version.rc`
      fills in the publisher and product fields a signature is checked
      against.
- [ ] **Decide the donation link.** `DONATE_URL` in `web/index.html` is still
      unset, so nothing renders.

## Ship it

- [x] **Cut v0.6.7.** Tagged 2026-09-22, nine commits past v0.6.6. Fluids,
      frame time, and reach.

      **Liquids level instead of standing in blocks**, and water no longer
      boils into one-wide spikes: lifted parcels only stack where something
      beside them holds them, and spill onto the surface otherwise. Steam
      drives a slug of water up a pipe instead of stalling once gas gets in
      between the parcels, and bubbles rise through water instead of being
      carried sideways by it.

      **The sim and the renderer got faster.** A failed gas-pressure search
      makes its chunk wait before looking again (10.1 -> 6.5 ms on the
      lava-into-water bench at 8 threads, and no frames over 16.6 ms); light
      sampling and view drawing run on the sim's thread pool; `dirtyArea` has a
      one-chunk fast path. The browser build's frame scheduling and pixel
      path too.

      **Base tool reach is half again as far**, 56 cells to 84. The reach
      accessories keep their bonuses.

      **powderlike**, a second front-end over the same simulation, is the
      tuning bench for all of that. Not a release artefact.

      **The wiki says where materials come from** -- every material page reads
      the reaction and phase tables backwards, so Steel finally says how steel
      is made.

- [x] **Cut v0.6.6.** Tagged 2026-09-15, one commit past v0.6.5.

      **Titanium smelts in a fuel fire.** Ore and metal both melt at 201 C now,
      under fuel fire's 202, where they were 208 and 205 and coke-only. Coal
      still cannot reach it, and tungsten stays coke's alone. The smelting,
      coke-retort and going-down guides, the Fuel and Coke tooltips and the
      home page say so, and the wiki is regenerated.

- [x] **Cut v0.6.5.** Tagged 2026-09-15, two commits past v0.6.4. Creatures.

      **Bees find their way round a base.** Each hive keeps a breadth-first
      route field rooted at its door, so a colony flies over walls, out of rooms
      by their doors, up to ledges and into flower shelves, and never picks a
      flower it cannot reach. The flower search also stopped missing single-row
      beds three scans in four. Measured in `tests/bee_routes.cpp`: an open bed
      went from 10 round trips to 109, and every walled layout from 0 to 45-77.

      **Rock mites no longer chew through walls.** They route and hop like the
      other walkers instead (`tests/mite_walls.cpp`). The Brood Mother still
      ploughs through rock; that is her fight.

- [x] **Cut v0.6.4.** Tagged 2026-09-15, three commits past v0.6.3. Saves.

      **An autosave slot.** Every five minutes of play, and on quit, the world
      goes to its own slot on the save screen, so forgetting to save loses
      minutes rather than an evening. A due autosave waits for a calm moment --
      no boss alive, nobody dead -- but only for so long.

      **Saving and loading are faster.** A save went from about 190 ms to under
      40 and a load from about 186 to about 65, on the spare cores and a faster
      compressor. The file format did not change: checked section by section
      against the old code on real saves, and held by `tests/save_codec.cpp`.
      Saves move freely between old and new builds.

- [x] **Cut v0.6.3.** Tagged 2026-09-14, two commits past v0.6.2. A patch
      release for multiplayer, and one convenience.

      **The Emberwing Feather works on a joined client.** A client rebuilds its
      body from every state packet and replays unacknowledged input on top, and
      the packet never carried the air-jump memory -- so a jump held through a
      fall re-fired the feather on every packet, which looked like flying while
      the host kept pulling the body back down. Reproduced in
      `tests/air_jump_replay.cpp` before the fix (the client drifted 27.6 cells
      from the host) and exact after it. Network format changed again, so
      everyone needs this build to play together.

      **Shift-click** sends a stack where it belongs, as in Minecraft: armour,
      trinkets and drones put themselves on, modules go into a drone, a chest
      stores and returns, and everything else crosses between the hotbar and the
      pack.

- [x] **Cut v0.6.2.** Tagged 2026-09-14, thirty-one commits past v0.6.1. The
      wiki release, and the machines release.

      **The wiki is live** at <https://cinderlift.com/wiki/>: 373 pages generated
      from the game's own tables -- every material, item, recipe in both
      directions, creature and device -- plus fifteen tutorials, search, and a
      "what can I make right now" view. The site now sends new players to the
      first tutorial and its controls strip is correct (left builds, right digs,
      C crafts).

      **Miners and placers were redesigned** around two settings, a facing and
      the size of the square they work, with a per-pulse or always-on trigger
      and a pale aura over exactly the cells they will touch. They connect to
      item pipes now. On the way: the placer stopped sucking what it had just
      placed back in through its face.

      **The character can be switched off in multiplayer**, for building like the
      sandbox in a shared world. The host applies it; nothing treats a player
      in that mode as present. Network protocol changed, so everyone needs this
      build to play together.

      Also a hand-crafted GlowFluid recipe, and the Thermocouple back on the
      wiki after a loop that started at 1 left it off.

- [x] **Cut v0.6.1.** Tagged 2026-09-11 from `c0f21eb`. A patch release for one
      bug that was reported twice and was worth shipping on its own: creatures
      spawning in lit areas.

      Neither half of it was in the spawner or in the light solver. The
      renderer registered the four dynamic light sources and then solved the
      field; the spawner cleared that list and solved without registering any
      of them, so it judged darkness by a field holding every lamp that is a
      CELL and none that is an OBJECT -- your light drone, a pedestal, a worn
      lantern. And the spawn rule consulted `g_lightOn`, which is whether
      lighting is DRAWN, so turning the lights off to inspect a contraption
      also turned off what your torches were buying.

      Carried in with it, from work in flight at the time: the fuel-to-coke
      retort chain, the Cinderling Ash's own ember material, and the bat's
      double jump.

- [x] **Cut v0.6.0.** Tagged 2026-09-10 from `fcc7586`, forty-three commits
      past v0.5.0. The release the game can be FINISHED in: layer 3 is a place
      with its own creatures, the Censer and the Effigy are in it, and the
      rocket at the end of them can be built, fuelled, crewed and launched.
      Not a 1.0 -- the shop, the wiki and the onboarding pass are still open,
      and 1.0 should mean polished rather than merely complete.

      Also in it: ten creature charms and four boss sigils, three post-Censer
      armour lines and two resistance lines, six trinket slots, a sixty-slot
      pack, wax with a survival source and three wild hives to get it from,
      boss health bars, the lance/orbit/shield drones made to work, dropped
      items drawn as themselves, and flowers that were silently broken since
      they were written.

- [x] **Cut v0.5.0.** Tagged 2026-09-06 from `ccfef57`, thirty-eight commits
      past v0.4.5. Not a 0.4.6: a layer-2 boss, a new tool tier and a change to
      how the whole world is lit is a minor version. Shipped in it -- the hive
      and bees, the Widow, Multitool Mk III and the seven shot modifiers, the
      mining rebalance, discovered-space lighting, the flint striker fix, and
      aqua regia removed.

## Beginner friendliness

- [ ] **A shop.** New ground: there is **no currency and no trading in the
      game at all** today, so this is three decisions before it is any code.
      - *What is money?* A new coin item, or an existing material (gold) doing
        double duty? A coin is cleaner to balance; gold-as-money is one less
        concept and fits a game about smelting.
      - *Where does it live?* The crafting-station pattern already exists
        (`STATION_BENCH`, `STATION_ANVIL`, …) and a shop could be one more
        placed station with its own panel — cheapest by far, and it reuses the
        crafting UI. The alternative is a wandering NPC trader, which means new
        entity behaviour and pathing.
      - *What is it for?* If it sells what you could mine anyway it is a
        shortcut that erodes the progression ladder. The beginner-friendly
        version is probably that it sells the **first** rung of each ladder
        cheaply (a copper pick, a striker, seeds) so a new player who dug
        themselves into a hole can recover — and sells nothing past iron.
- [ ] **Other onboarding ideas** — not started, listed so they are not lost:
      a first-session objective or two, and a way to re-read the controls
      without the pause menu.

## The wiki

- [x] **An extensive wiki on the website.** Live at
      <https://cinderlift.com/wiki/>. Built 2026-09-12 over the 33 steps in
      [WIKI_STEPS.md](WIKI_STEPS.md); the design is [WIKI.md](WIKI.md).

      **372 pages**, 9,000+ internal links, none broken. 115 materials, 176
      items, 141 recipes over six stations, 26 creatures, 25 devices, 15
      tutorials and a concept page — plus search, and a "what can I make right
      now" view that narrows all 141 recipes to the stations you have built.

      **The reference half is generated from the game's own tables** by
      `tools/wiki.cpp`, so it cannot quote a number the game has since changed.
      Every icon is drawn by the game's own renderer. Run
      `bash scripts/run_wiki.sh` when you want it updated and commit the
      output; nothing in CI does it, and the script tells you how many commits
      behind the committed wiki is.

      **`tests/wiki.cpp` is the drift guard**, and it covers the hand-written
      half too: it re-reads iron ore's melting point, coke ember's burn
      temperature and the heat lamp's cap out of the tables and checks the
      tutorials still quote them. Proven by retuning iron ore 190 → 196 °C in
      `materials.cpp` with the wiki untouched, and watching the suite fail.

      The page that justified the project is
      [Coke, and the sealed retort](https://cinderlift.com/wiki/guide/coke-retort.html)
      — there is no retort item, no recipe and no device, so the whole route to
      titanium and tungsten was discoverable only by reading one tooltip.

## Engineering debt

- [x] **A way to run the tests.** `mingw32-make test` builds and runs all 50,
      or `mingw32-make test T=melee_test` for one. Work lives in
      `scripts/run_tests.sh`; objects are shared and rebuilt only when stale.
      Build failures are reported separately from test failures, because a
      sweep once reported 37 failures that were really one mistyped script
      name and the two looked identical.
- [x] **`network_mismatch` runs now.** The runner compiles `network.cpp`
      twice with different `CINDERLIFT_BUILD_ID` values -- it is the only file
      that reads it -- links two binaries, and has the host launch the client.
      Verified non-vacuous: with both builds given the SAME id the test fails,
      as it should.
- [ ] **CI still does not run the tests.** Now that one command does it, the
      Pages workflow could -- though the suite is Windows-only today
      (winsock, `CreateProcess`), so it would need a Windows job.

## Flowers are planted the wrong way

- [ ] **`Flower Seed` does not behave like `Oak Seed`, and should.** Oak is a
      MATERIAL you place or scatter; it germinates when it settles on soil with
      air above. The flower seed is an ITEM of kind `ITEMK_SEED` -- the
      grass-seed verb -- so it converts a cell instead of becoming one: you
      must aim at the strip of empty air directly above soil, and it drops a
      finished flower with no growth. Aiming at the ground does nothing, which
      is what it feels like when it 'does not place right'.

      **Possibly already fixed — worth checking in play.** As of 2026-09-12 the
      tables say `ITEM_FLOWER_SEED` is `ITEMK_MATERIAL` and `MAT_FLOWER_SEED` is
      in `g_matIsSeed` with density 120, matching oak. That is exactly the shape
      this entry asks for, so the work may have landed since this was written.
      Only `ITEM_GRASS_SEED` still uses the convert-a-cell verb. Found while
      writing the farming tutorial, which had to be rewritten because it
      described the old behaviour.

      Attempted and reverted on 2026-09-01. Two findings worth keeping:

      1. `g_matIsSeed` is a hand-maintained list, and a seed missing from it is
         **silently inert** -- it falls, settles, and is never reported to the
         grower, with nothing anywhere complaining. Worth deriving from
         `TREE_KINDS` when this is picked up again.
      2. The blocker: a new seed material falls **straight through dirt and
         grass**, where `MAT_OAK_SEED` rests on top of them -- with material
         rows that are identical apart from density (116 vs 120, both well
         under soil's 140). Unexplained. Whatever the cause, it has to be
         understood before this ships; a seed that sinks into the ground is
         worse than the current awkward verb.

      Also note the tree grower is the wrong shape for a one-cell blossom --
      lean, lumps, spread and pods are all meaningless -- so germinating
      directly into `MAT_FLOWER` is probably right, rather than adding a
      species to `TREE_KINDS`.

## The long arc

What *done* looks like -- bosses, populating all three layers, the rocket, and
the bees-and-wax idea -- lives in [ROADMAP.md](ROADMAP.md).

- [x] **Wax and web had no survival source.** Closed 2026-09-10, the second
      way round: the modifiers are made of something else and silk stays a pure
      hazard. Aqua regia was on this list too and was removed outright on
      2026-09-06 rather than given a source, because it did not feel right in
      play -- the transmute table and rule went with it, since a mechanism with
      nothing using it is worse than no mechanism.

      Web was never unobtainable -- mining it banks it, `g_matDropsAs` is
      identity -- but it decays with a mean life around 255 frames, so the
      window was about four seconds, mid-boss-fight, against recipes wanting
      four to eight cells each across six of the seven modifiers. A collection
      minigame nobody asked for.

      What shipped:

      - All six modifier recipes ask for `MAT_BEESWAX`. **Beeswax, not
        `MAT_WAX`** -- they are different materials and the first attempt at
        this swap used the wrong one, which has no source either, so six
        recipes went from asking for something you cannot keep to asking for
        something that does not exist. A hive extrudes beeswax; that is what
        they ask for, and `tests/wild_hives.cpp` names the material explicitly
        because the loose version of that check passed the broken build.
      - Worldgen seeds **three wild hives**, one per third of the map, each in
        a carved bowl. A hive used to be something you built, and building one
        is not a thing you can do before you have any wax to want -- so the
        supply now exists before you have made anything.
      - Coal wax and coal honey render back into coal at the bench, which stops
        souring a colony being a quiet mistake.

      Nothing in the crafting table asks for web any more, and the test enforces
      that rather than trusting it.

## Known and deliberate

Not bugs, and not scheduled — written down so they stop being rediscovered.

- **powderlike is not in releases, on purpose.** Added 2026-09-17:
  `build_powderlike.bat` / `mingw32-make powderlike` builds
  `build\powderlike.exe`, a second front-end over the same simulation with no
  character, no camera, no saving, and a cell scale that makes the world
  512x384, 1024x768 or 2048x1536 cells. Nothing in the launcher, the CI
  workflow or the release notes knows about it, and it has no version resource,
  because it is a window onto the physics rather than a product with its own
  support burden. It is also not covered by `tests/` -- it is a shell, and the
  simulation underneath it is what the suite tests. If it ever ships, it needs
  a `VER_TARGET` in `res/version.rc` and a line in the workflow first.

- **Bees cannot be picked up, and that is fine.** `ITEM_BEE` and
  `ITEM_COAL_BEE` have no survival source: right-click capture was built and
  removed on 2026-09-01 because right-click is also the dig verb, so every
  swing near a hive pocketed a bee. Closed as deliberate on 2026-09-06 -- the
  items still release a bee if you somehow have one, and a hive is placed
  rather than moved. A net or a hive-hands-you-one gesture is a nice-to-have,
  not a gap.

- **R-tap still teleports free in survival**, which leaves the Warp Wand with
  little reason to exist. You chose to keep the free teleport; the wand stays
  redundant until that changes.
- **The site can lag a push by up to 10 minutes.** GitHub Pages serves
  `max-age=600`, so a returning visitor can hold a cached wasm that long. A
  hard refresh skips it. This is also why the in-game and on-page version
  numbers exist — see `scripts/version.sh`.
