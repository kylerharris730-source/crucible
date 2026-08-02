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
