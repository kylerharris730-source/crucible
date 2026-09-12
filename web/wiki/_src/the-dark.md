# The dark is not scenery

Darkness is not a visual effect in this game. It is the spawn condition.
Creatures appear in unlit cells, and a lit room stays empty — permanently, for
as long as it stays lit.

This is the highest-value thing on this site and it is currently taught by being
killed.

## You will need

- [Torches](../materials/torch.html) — 4 Wood + 1 Coal makes four, by hand

## Steps

1. **Light the room you are working in, not just the corridor you walked down.**
   A pocket of dark behind you is a spawner.
2. **Place torches as you dig**, not after. The cost of a torch is four wood;
   the cost of not placing one is whatever was standing behind you.
3. **Watch the corners.** Light falls off with distance, and a torch that
   reaches the middle of a room may leave the far corner dark enough to spawn in.
4. **Before you leave a base, walk it once looking for unlit cells.** Anything
   that spawns while you are away is there when you come back.

## When it works

You come back to a room you lit and it is still empty. That is the whole
mechanic: light is not a deterrent, it is a prevention.

## When it does not

**You turned the lighting off to look at a contraption and things started
spawning.** They did not, and this is worth being precise about: `K` cycles
whether lighting is *drawn*. It used to also turn off what your torches were
buying, which was a real bug and was fixed in v0.6.1. Turning the display off is
now safe.

**Things spawn in a room that looks lit to you.** Look at the *floor and
ceiling corners*, not the middle. Light is a field with falloff, not a radius
switch, and the dim edge of a torch's reach can still be dark enough.

**A drone, a pedestal or a worn lantern seems not to count.** It counts. Object
lights are registered the same way cell lights are — that was the other half of
the v0.6.1 fix. Before that the spawner judged darkness by a field containing
every lamp that is a *cell* and none that is an *object*.

**The surface fills up at night.** It is meant to. Some creatures spawn on the
surface after dark specifically; the
[creature index](../creatures/index.html) says which in the "Found" column.

**You lit everything and still got hit underground.** Light stops things
*spawning*. It does not stop something that spawned elsewhere from walking to
you.

## Next

[Making fire](making-fire.html) — which is a different problem from light, and
harder than it looks.
