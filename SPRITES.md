# Sprite language

Inventory art is presented on a **21×21 pixel reading grid**, cached 2× with
nearest-neighbour scaling and displayed as a 38×38 screen icon. This is the
inventory standard: every slot intended to identify an item must reserve at
least that much square icon space; counts may overlay an edge but must not
squash the art.

The simulation-facing sprite master remains 14×14 because machines and
creatures use those pixels as world-scale art. Inventory rendering converts
that master to 21×21 with an exact 3:2 nearest-neighbour resample. This keeps
world footprints, collisions, and silhouettes stable while making every item
sprite 50% larger in each dimension. Materials are generated directly at
21×21, using the phase and silhouette language below.

The world remains LUT-rendered; item sprites communicate what a stack *is*
without adding per-cell render cost.

## Material silhouettes

- Static blocks are squared samples with chipped highlights.
- Powders are triangular piles: gravity is visible even in an inventory slot.
- Liquids are low, wavy fills; their edge is never a rigid rectangle.
- Gases are sparse overlapping puffs, leaving transparent air around them.
- Seeds and plants favour a clear stem or husk silhouette over texture.

Every material begins with its table colour, then receives a deterministic
highlight/shadow pattern. This preserves the useful colour identity of the
simulation while keeping Stone, Clay, Iron, and Ceramic readable as different
objects rather than anonymous swatches.

## Phase language

Molten materials keep the silhouette of a liquid but add bright gold fissures.
Frozen forms are pale, hard-edged blocks. Vapours use the gas silhouette and
are lighter than their parent liquid. These are phase cues, not arbitrary
repaints: an unfamiliar material should still read as solid, powder, liquid,
gas, or molten at a glance.

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
  room. Fine texture does not justify a small silhouette. The 38px display cap
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

## Circuit signals

Virtual circuit signals `1` through `9` use violet seven-segment chips with a
dark casing. A signal shown in a picker, control, or network readout therefore
has the same icon-and-name treatment as a material signal. Material signals
reuse their regular item art rather than inventing a separate circuit version.
