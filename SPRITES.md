# Sprite language

Inventory art is 14×14 pixels, enlarged with nearest-neighbour scaling. The
world remains LUT-rendered; item sprites communicate what a stack *is* without
adding per-cell render cost.

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

Two things this doc can only assert and a picture can settle:

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

## Circuit signals

Virtual circuit signals `1` through `9` use violet seven-segment chips with a
dark casing. A signal shown in a picker, control, or network readout therefore
has the same icon-and-name treatment as a material signal. Material signals
reuse their regular item art rather than inventing a separate circuit version.
