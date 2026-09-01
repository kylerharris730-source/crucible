# Cinderlift checklist

What is outstanding. Tracked in git on purpose — this used to live only in
chat, which meant it was re-derived from memory every session and quietly lost
things. `HANDOFF.md` is **not** this file: it is gitignored scratch, is usually
stale, and should not be trusted for release state.

Released: **v0.4.5**. Two commits sit past it (see *Ship it* below).

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

- [ ] **Cut v0.4.6.** Unreleased on `main`:
      - the flint striker fix (a mouse button released outside the window is no
        longer held forever) — worth a Windows build on its own, since it is
        the bug you actually hit
      - the clean-checkout-is-not-dirty fix for the version label

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

- [ ] **An extensive wiki on the website.** The site is currently a single
      static `index.html` published to Pages, so this means real multi-page
      output. One strong recommendation before we start:
      **generate the reference half from the source, do not hand-write it.**
      The game already holds every fact in tables — `ITEMS[]`, `MATS[]`, the
      recipe list, ignition and melting points, the melee ladder. A generator
      in `tools/` (which is exactly what that directory is for; see
      `tools/cover.cpp`) emitting pages from those tables gives a wiki that
      **cannot drift from the game**, and it re-runs in CI on every push
      alongside the wasm build. Hand-written pages start wrong the first time
      a number is tuned — and this month alone we changed spear reach, sky
      colours and plant cover.
      Hand-write only the parts that are genuinely prose: how heat and
      pressure actually behave, how circuits work, a getting-started page.
      Several of those already exist as `.md` files in the repo
      (`THERMAL_PRESSURE.md`, `CIRCUITS.md`, `LOGISTICS.md`, `PROGRESSION.md`)
      and are most of a wiki already.
      - Open question: same domain under `/wiki/`, or a separate section? Same
        domain is simpler and keeps the Pages deploy as one artifact.

## Engineering debt

- [ ] **There is no way to run the tests.** No `make test`, no runner script —
      the suite can only be run by reconstructing the compile command by hand
      (every `src/*.cpp` except `main.cpp`, plus the test). This actively
      caused a wrong result: a sweep reported 37 failures that were really one
      missing script. A `test` target in the Makefile is maybe ten lines.
- [ ] **`network_mismatch` never runs.** It needs the same source compiled
      twice with different `CINDERLIFT_BUILD_ID` values, which no runner does,
      so it is skipped in every sweep and is effectively untested.

## Known and deliberate

Not bugs, and not scheduled — written down so they stop being rediscovered.

- **R-tap still teleports free in survival**, which leaves the Warp Wand with
  little reason to exist. You chose to keep the free teleport; the wand stays
  redundant until that changes.
- **The site can lag a push by up to 10 minutes.** GitHub Pages serves
  `max-age=600`, so a returning visitor can hold a cached wasm that long. A
  hard refresh skips it. This is also why the in-game and on-page version
  numbers exist — see `scripts/version.sh`.
