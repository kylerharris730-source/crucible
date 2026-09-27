# Cinderlift Steam store page draft

Prepared 2026-09-27. This is a working source for Steamworks fields and art,
not a page generated from HTML. Keep the public page aligned with features that
will be in the launch build; revise copy and captures as the Steam edition takes
shape.

## Positioning

**One-line idea:** A survival sandbox where heat, fluids, and electricity are
real materials to build with. Descend through an alien planet, make machines,
and build a way home.

**Audience:** Players who like Terraria-style exploration and crafting, falling
sand simulations, and making machines that work because of physical rules.

**Title:** Cinderlift (use this spelling everywhere).

## Steamworks > Edit Store Page > Description

### Short description (plain text; paste candidate)

Crash on an alien planet and build your way home in a falling-sand survival
sandbox. Dig through hostile layers, smelt and wire machines, fight creatures,
and turn heat, fluids, and electricity into the tools you need to escape.

### About This Game (paste candidate; format headings and bullets in the editor)

You crashed on an alien planet. The way home is a rocket, but the materials and
machinery to build it are buried far below you.

In Cinderlift, the world is made of simulated materials. Sand falls, water
flows, heat moves between neighboring cells, metal melts and cools, and
electrical pulses travel through conductors. These rules are the foundation for
survival, crafting, and the machines you invent.

**Dig deeper.** Explore a generated world with three underground layers,
different resources, dangerous environments, and creatures guarding your
progress.

**Make the heat you need.** Refine ores, manage fuel, and build working furnaces.
Temperature and material properties matter more than a recipe button.

**Build systems.** Connect chests, pipes, miners, placers, sensors, wires, and
logic devices to move items and control your machines.

**Fight your way out.** Craft equipment, face the bosses below, and assemble the
rocket that gets you off the planet.

### Copy to add only after it exists in the Steam build

- The cold and desert surface regions, their structures, enemies, and bosses.
- Named worlds and a Save & Quit home screen.
- The roaming shopkeeper and monster-part trade economy.
- The starter wiki book and in-game guide.
- Multiplayer claims only after testing a Steam install with players on
  different networks. Native/beta networking exists, but that real-world test
  is still outstanding in `CHECKLIST.md`.

## Basic info and other fields to verify in Steamworks

- **Type:** Game. **Platform:** Windows initially; do not select macOS/Linux
  until a supported build exists.
- **Language:** English interface. Mark subtitles/audio only if applicable.
- **Features:** Single-player. Consider Online Co-op after the two-house Steam
  build test. Do not check Steam Cloud, achievements, controller support, or
  Steam Deck compatibility on the strength of plans alone.
- **Genre/tag candidates:** Sandbox, Survival, Crafting, Automation, Physics,
  Exploration, 2D, Pixel Graphics. Choose the closest actual Steam tags after
  seeing its tag wizard; review the order against the game's first screenshots.
- **Developer/publisher:** Use the exact public name you choose for the Steam
  release. **Website:** `https://cinderlift.com/` in Steam's designated link
  field, not inside the description.
- **Release timing and price:** Set in the package/release tools after choosing
  the date and price. Current working price hypothesis from planning is US
  $4.99, still to be decided. Show "Coming Soon" or a broad window until the
  build and Steam's earliest-release date are clear.
- **Content survey and system requirements:** Complete from the actual near-final
  game and measured Windows hardware. Do not guess age-rating answers or minimum
  specs from development machines.

## Art and media needed

| Asset | Steam size | Status / direction |
| --- | ---: | --- |
| Header capsule | 920 x 430 | New key art with readable Cinderlift logo. |
| Small capsule | 462 x 174 | Logo must survive very small display. |
| Main capsule | 1232 x 706 | Same recognizable key art. |
| Vertical capsule | 748 x 896 | Tall composition, same identity. |
| Screenshots | At least 5; 1920 x 1080 minimum, 16:9 | Capture actual gameplay in the Steam build. |
| Shortcut icon | 256 x 256 | Derive from a simple game symbol. |
| App icon | 184 x 184 JPG | Derive from the same symbol. |
| Library capsule | 600 x 900 | Portrait key art and logo. |
| Library hero | 3840 x 1240 PNG | Landscape key art; reserve a clean space for separate logo. |
| Library logo | 1280 px wide and/or 720 px tall PNG | Transparent logo. |
| Library header | 920 x 430 | Can share the header-capsule composition. |
| Gameplay trailer | Recommended | Show the game loop and physics in the first seconds. |

The existing `web/og-cover.png` is a useful mood and logo reference but is only
1200 x 630. The three `web/shot-*.png` images are 1288 x 768 or 1200 x 630,
below Steam's screenshot minimum; do not upscale these as substitutes for new
in-game captures. The captures should cover: (1) surface and character/UI,
(2) heat or lava interacting with fluids, (3) crafting/refining, (4) a working
machine/circuit, and (5) deep exploration or boss combat. Later replace or add
shots that show the new biomes, shop, guide, and named-world screen when those
features are real. Screenshots must show gameplay, without marketing text or
concept art. Capsule art should include the title/logo, without price, review
quotes, or extra marketing copy.

## Path to a public Coming Soon page

1. Finish Steamworks partner verification and check whether **Create New App**
   is available. The Steam Direct fee has been paid; record its exact date and
   the earliest-release date the dashboard shows.
2. Create the Cinderlift app and enter the identity/basic-info fields. Set the
   website link separately from the description.
3. Paste/revise the copy above. Complete the content survey, release-date
   display, supported features, and system requirements using confirmed facts.
4. Create and upload the required capsule/library art and at least five new
   gameplay screenshots. Preview in Steam's store beta mode.
5. Complete every Steamworks store-presence item marked required, then click
   **Mark as Ready for Review**. Valve says review usually takes 3–5 business
   days and recommends submitting at least 7 business days before the intended
   page-publication date.
6. Once approved, use **Post as Coming Soon**. The page must be publicly Coming
   Soon for at least two weeks before release. Store copy and media can be
   updated later as the Steam edition is finished.

## Official references

- [Store page editor](https://partner.steamgames.com/doc/store/page)
- [Written descriptions](https://partner.steamgames.com/doc/store/page/description)
- [Asset sizes](https://partner.steamgames.com/doc/store/assets)
- [Screenshot and capsule rules](https://partner.steamgames.com/doc/store/assets/standard)
- [Review process](https://partner.steamgames.com/doc/store/review_process)
- [Coming Soon setup](https://partner.steamgames.com/doc/store/coming_soon)
