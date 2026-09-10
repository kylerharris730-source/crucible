# Sprite language

Inventory art is presented on a **21×21 pixel reading grid**, cached 2× with
nearest-neighbour scaling and displayed at a **34×34 default cap**, adjusted by
the player's UI scale. Compact text lists may use smaller icons. Counts may
overlay an edge but must not squash the square art.

The simulation-facing sprite master remains 14×14 because machines and
creatures use those pixels as world-scale art. Inventory rendering converts
that master to 21×21 with an exact 3:2 nearest-neighbour resample. This keeps
world footprints, collisions, and silhouettes stable while making every item
sprite 50% larger in each dimension. Materials are generated directly at
21×21, using the phase and silhouette language below.

The world remains LUT-rendered; item sprites communicate what a stack *is*
without adding per-cell render cost.

## Material silhouettes

- Refined metals are beveled ingots: bright top, midtone face, dark end.
- Ores are angular dark host rock with thick mineral veins, never ingots.
- Stone and coal are faceted chunks; sand, dirt, and fuel are granular piles.
- Wood has end grain and long bark lines; birch has pale bark and dark dashes.
- Glass is an open frame with a diagonal reflection; masonry has mortar joints.
- Liquids are shaded droplets, gases overlapping puffs, and flames tapered tongues.
- Seeds use paired kernels; plants use stems, grain heads, or cotton bolls.
- Rope is a twisted coil, rubber a dark ring, chitin a segmented shell, and web
  a radial mesh. Sieves retain visible holes; springs show an upward water jet.
- Stations and torches reuse their authored object sprites, not rock shapes.

`src/material_icon.cpp` owns inventory-only material art. Most colors start from
the world table, but material-specific palettes take priority over literal terrain
swatches. Copper is salmon-red metal with peach highlights, bronze muted ochre,
and gold bright yellow. Copper ore includes a little green oxidation. Iron is
neutral silver, steel blue-gray, tin pale green-silver, titanium pale lavender,
and tungsten dark slate. Use broad coherent lighting planes, not random speckles
on every surface. Texture is reserved for genuinely granular or fibrous material.
Copper and bronze weapon icons and held weapon colors match their ingot palettes.
None of this changes simulation colors, material IDs, or physical behavior.

## Phase language

Molten materials use droplets with hot highlights in their existing heat palette.
Ice is pale and hard-edged. Vapours use the gas silhouette rather than the liquid
drop. These are phase cues, not arbitrary repaints.

`tests/material_icons.cpp` checks coverage, deterministic pixels, and transparent
margins, and optionally writes an art dump. Run `scripts/preview_material_icons.py`
with that dump and a PNG output path (requires Pillow) for a labeled contact sheet
showing every material at 34px and 63px. Inspect it after art changes.

## Machines, character, and future enemies

Machines use dark casings, a single functional face colour, and transparent
space around their working silhouette. Character and enemy sprites should use
the same constraints: readable silhouette first, one or two identity colours
second, texture last. Rigged 2D parts are a good next step for characters:
they keep a stable body design while posing limbs procedurally, without the
visual noise or runtime cost of full 3D rendering.

## The shared palette, and the fact that it is full

Hand-drawn icons in `sprite.cpp` are ASCII art over **one shared palette**
(`paletteOf`), not per-sprite palettes. That is deliberate: `T` is always a
highlight, `S` is always steel, and the two handle colours are the two multitool
tiers, so a set of icons that is meant to look like a set cannot drift apart one
sprite at a time.

**Every letter and digit is now spoken for.** The Bolt Caster's wooden stock had
to take `#`. The next additions face a choice between starting on punctuation
and reusing a colour that already means something else, and the second of those
is exactly how a shared palette stops being shared — so prefer punctuation, and
give it a comment saying what the colour *means* rather than what it looks like.

Three things this doc can only assert and a picture can settle:

- **Use the full reading grid.** Inventory silhouettes should occupy most of
  the 21×21 canvas while retaining at least one pixel of transparent breathing
  room. Fine texture does not justify a small silhouette. The 34px default cap
  provides additional breathing room around the enlarged art. Counts belong at the
  slot edge and must never reduce or distort the square reserved for the icon.

- **Check art by rendering it.** Every art problem this project has had — the
  head that merged into the torso, the visor on the crown, the crouch that read
  as a child, the husk that was a green blob rather than a humanoid — passed
  every numeric check that existed and was obvious the instant somebody looked
  at a PNG.
- **Check it at the size it is actually seen.** The Bolt Caster's first draft
  was the Multitool Mk I silhouette in the Mk I colours, two rows shorter, on
  the theory that size would carry the difference. At true hotbar scale it was
  simply a slightly smaller Mk I. What fixed it was **value**: the multitools
  are light (pale steel, cream handle), the crude starter weapon is dark (rough
  wood, dark barrel). Light against dark survives being three pixels tall.
  Eleven rows against thirteen does not.

Held melee weapons are procedural world sprites rather than rotated inventory
icons. Their world silhouette must preserve the same identifying furniture as
the icon: swords have a wrapped grip, a perpendicular crossguard, a broad lower
blade, and a tapering bright point; spears remain a narrow shaft and head. When
reach changes, update both the attack segment and resting held length, then keep
the guard and grip fixed-size so a longer blade does not turn them into stripes.
A held sword uses the same hilt-to-tip length as its swing; only a spear shortens
at rest, because extending the shaft is part of its stabbing motion.

Drone Armour is a three-piece visual family: all pieces use the same blue-steel
body colour and cyan control strip, while each keeps a different equipment
silhouette (visor, harness, split greaves). Set membership must be readable from
the repeated trim without making the three slot roles look interchangeable. The
Drone Beacon repeats that cyan signal colour but uses antenna arcs and a cased
puck, so it reads as the set's controller rather than a fourth armour piece.

## Widow encounter rhythm

The Widow uses a single replicated `WidowMove` state, not independent attack
clocks. Approach and short backpedals establish spacing. A web volley raises
the front with pale silk light for 30 ticks; a pounce gathers into a low amber
crouch for 32 ticks. Both snapshot the target before the tell. No spit during
a pounce and no steering after takeoff. Recovery settles the body for 54 ticks
(42 below half health), providing an opening rather than another immediate attack.
A one-time 60-tick violet moult marks half health; afterwards pursuit is faster
and the existing web fan wider, but commitment and recovery remain.

At large gaps the approach scuttle is faster, rather than shortening tells or
removing recovery to win a footrace. Nearby targets provoke spacing for silk;
midrange alternates volleys and pounces, and distant visible targets provoke a
pounce. Occluded or out-of-range targets are approached rather than attacked
without warning. Failed ballistic solves still exit into recovery.

`tests/widow.cpp` checks move exclusivity, aim commitment, recovery, the half-health
break, and the existing silk/pursuit behavior. `tests/widow_art.cpp` with
`scripts/preview_widow.py` renders the actual in-game tells for visual review.

## Launch assembly

The rocket is 28x80 on its own canvas -- the largest single piece of art in the
game, and the only placed object drawn from something other than the shared
14x14 device sprite. Pale titanium hull with one lit flank, a banded tube, a
dark tungsten engine and bell, copper pipework down both sides, two swept fins
and four legs standing it clear of the pad. Every colour is a character the
shared palette already defined; nothing new was added to `paletteOf`, which is
full.

Two parts of it are state rather than structure. The amber core window and the
cyan fuel line are tagged in a mask baked from the same art table, and devDraw
draws them as dark metal until the Ascent Core and the fuel are aboard -- so a
rocket read from across the valley says how far along it is before you open its
panel. The footprint follows the picture rather than the bounding box: cells are
written only where the art is opaque, so the machine does not stand in a slab of
its own sky.

`tests/rocket_art.cpp` with `scripts/preview_rocket.py` renders all three states;
`tests/rocket.cpp` covers the object.

## Effigy final boss

The Effigy uses a 96×112 world canvas (formerly 72×88), with 16 distance-driven
walk frames, four breathing frames, and an eight-pose fire ritual. Its forward
toe and eye slit face right in source art; mirror the whole figure for leftward
travel. The backward-folding shin is anatomy, not permission to point the toes
backward. Lift the knee during forward recovery, then plant it on the backward
stroke. Chest sway, counter-rotating head, delayed wrists, and a swinging heart
give the cage weight. Keep gaps between the ribs visible.

Fire rituals halt the walk and lift the arms. Three fixed floor footprints,
30 cells apart, remain visible for 84 ticks before brimfire appears. Their edge
lines are persistent, not a whole-screen flash; rising marks show time passing.
Render these warnings independently of body visibility and terrain lighting.
The crown previews its five-ray fan or eight-ray halo for 54 ticks; the arm orbs
mark a committed lunge target for 40 ticks, then recover rather than tracking
the player indefinitely. These timers and aim coordinates use replicated entity
state, not local animation clocks.

`tests/effigy_art.cpp` and `scripts/preview_effigy.py` produce a contact sheet
and animated walk/ritual previews. `tests/effigy.cpp` checks warning timing,
locked targets, both crown patterns, and existing multipart boss behavior.

## Worn armor

`src/worn_armour.cpp` builds armor on the player's humanoid skeleton. Each
equipped head, body, or feet piece contributes its own palette and geometry;
mixed sets work without requiring a set bonus. Empty slots retain the base suit.
Steel and titanium have only head/body pieces, so do not invent matching greaves.

| Family | Worn design |
| --- | --- |
| Iron | Neutral iron plates, rounded helmet ridge, simple pauldrons and knee guards |
| Steel | Blue-steel cuirass and ridged helmet, pale metal fittings |
| Titanium | Pale lavender lightweight shell with mint fittings and small shoulders |
| Drone | Teal visor receiver, narrow cyan-strapped harness and control pack, shin strips |
| Ranger | Olive pointed hood/brim, tan sash, rear coat skirt, light greaves |
| Vanguard | Wine-red broad plate, cheek guard, warm metal pauldrons and gauntlets |
| Thurible | Ember-gold crown receiver and control harness with pale hot trim |
| Ashen | Ash-gray hood and long coat, ember-tan sash and light greaves |
| Brimsteel | Brimstone-red heavy plate and closed cheek guard, gold-hot fittings |
| Cinderweave | Quilted rust-brown lagging, sealed hood with rolled collar and jaw filter, banded trunk |
| Fumarole | The same quilting in bright ember orange, with a heavier sleeve over it |
| Pelt | Slate-blue padding, sealed hood, banded trunk |
| Hoarfrost | Pale ice quilting under a heavier sleeve, white fittings |

The four resistance suits share a fourth silhouette, `quilt`: bulk on the trunk
in three bands rather than shoulders on it, and a sealed hood with a rolled
collar instead of a ridge or a brim. They are worn for a PLACE rather than for a
fight, and the silhouette is what says so at a glance. The later suit of each
pair adds a heavy sleeve, which reads as plate over the padding.

Decorations are child bones: never stamp a stationary helmet or skirt over an
animated character. Keep armor palettes intact in multiplayer; exposed suit
fabric, belt, and the existing numbered badge retain player identity colors.
All designs use the existing collision boxes, lighting, facing, and damage flash.
Eight cached loadouts avoid rebaking during normal animation, including four-player
play. Equipment changes select or bake a loadout; no save or protocol changes.

`tests/worn_armour.cpp` checks every pose, individual pieces, mixed sets, removal,
cache eviction, and coverage for wearable armor. Its optional dump is rendered by
`scripts/preview_worn_armour.py` at 2x game scale and 3x inspection scale.

## Circuit signals

Virtual circuit signals `1` through `9` use violet seven-segment chips with a
dark casing. A signal shown in a picker, control, or network readout therefore
has the same icon-and-name treatment as a material signal. Material signals
reuse their regular item art rather than inventing a separate circuit version.
