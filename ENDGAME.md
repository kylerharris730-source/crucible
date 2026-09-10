# Ascent: proposed rocket and ending

**Built** (2026-09-10), all three stages: the hull and the assembly device, the
readiness and countdown, and the ascent, the victory flag and the win screen.

Two deliberate departures from what is written below, both recorded here rather
than quietly done. There is no five-second cinematic separate from the world --
the ascent IS the cinematic, played in the live view with the crew riding the
hull -- and the second button says "Menu" rather than "Main Menu", because this
game has no title screen to return to; it opens the pause menu, which is where
Quit lives. Everything else is as designed.

The rest of this file is the design. The Effigy unlocks escape; building and launching
the machine finishes it. First playable version should suit a full co-op run
without introducing another material tier or a long post-boss grind.

## Rocket

A roughly 28x80-cell upright rocket, almost three player heights tall. Pale
titanium hull, dark tungsten engine and feet, copper pipework, an amber Ascent
Core visible behind a central window, and a narrow blue cockpit window. Angular
fins and exposed plumbing keep it industrial rather than a clean modern capsule.
The pad, fuel connection, hatch, and engine must read at normal world scale.

Use the existing Assembly Station to craft a launch assembly from late-game
metals and a Field Relay. Place it on a solid surface at the surface, with a clear
vertical launch corridor. Interacting opens its checklist:

- Hull assembled.
- Ascent Core installed (one from the Effigy).
- Fuel loaded.
- Launch corridor clear.
- Crew ready.

Start with existing Fuel; tune its cost through the family playtest. Accept
inventory loading for the first version so escape does not require a new refinery
system. Leave a clearly drawn inlet for later physical delivery/automation.
Installation and fuel loading should visibly illuminate different parts of the
rocket. Show missing requirements directly, not only after pressing Launch.

Mining an idle assembly returns the rocket and its stored cargo. Launch consumes
its installed core and fuel only at ignition, not when the countdown starts.
No equipment, inventory, or world deletion on victory.

## Launch sequence

1. Ready checks: surface, stable pad, clear sky, core, fuel, living crew nearby.
2. Every connected player boards/marks Ready. Show each player's number/color.
3. Host confirms Launch; a five-second countdown begins. Anyone can cancel.
4. Recheck requirements throughout countdown. Disconnects, damage, destroyed
   support, or a blocked corridor cancel safely without consuming cargo.
5. At ignition, save the launch state and start a short shared cinematic:
   clamps release, engine warms from orange to pale yellow, dust rolls outward,
   then the rocket accelerates upward. Fade through blue sky into stars.
6. Record victory and show the result to the whole crew.

During the cinematic, players are not left standing defenseless in the live world.
The launch state is host-authoritative and synchronized; a reconnect must not
consume another core or replay rewards. Persistent completion and launch stages
need explicit save-version handling before implementation ships.

## Win screen

Use the game's charcoal panels, restrained amber borders, and pale text. A small
rocket coasts above the planet; keep enough empty space for the ending to breathe.

    ASCENT COMPLETE
    You made it home.

    [Crew portraits wearing their actual armor, player numbers/colors]
    The Effigy defeated · Rocket launched

    Continue Exploring     Main Menu

Only show statistics that are actually tracked; never invent a completion time.
If playtime tracking is added, clearly define active-world time rather than
wall-clock time between saves. Future stats can include deepest depth and deaths.

Continue Exploring returns the crew safely to the launch pad, with their gear,
world edits, and permanent victory flag intact. The empty pad becomes a small
commemorative landmark. Hosts control shared-world transitions; guests can leave
to their own menu without ending the host's game.

## Implementation order and acceptance

1. ~~Rocket art, assembly recipe, placement, loading UI, and recoverable
   cargo.~~ **Done.** 28x80 hull on its own canvas with a lit core window and
   fuel line; `DEV_ROCKET`, the first device that is not 14 cells square, with
   a footprint shaped to its art; 90 titanium, 50 tungsten and a Field Relay at
   the Assembly Table; placed by its feet on ground that can hold it; one
   button that installs the core, loads fuel, and gives both back; and mining
   it returns the assembly with its cargo. `tests/rocket.cpp` and
   `tests/rocket_art.cpp`.
2. ~~Save-backed, host-authoritative readiness/countdown/launch state.~~
   **Done.** The whole launch state lives in `Device` fields the rocket does
   not otherwise use -- stage, countdown, and a ready bit per player slot --
   which needed no new packet, no new save section and no version gate: a
   Device is already replicated field by field and already written to the save.
   The host confirms, anyone cancels, and `rocketFault` is the one definition
   of "ready" that the panel, the button and the per-frame recheck all read, so
   the pad cannot say ready and then refuse. Cancels on a blocked corridor, a
   destroyed pad, a death, or a crewmate walking away. `tests/rocket_launch.cpp`.
3. ~~Launch cinematic, victory screen, and safe return to the world.~~
   **Done.** Ignition spends the core and the fuel and lifts the hull out of
   the grid; it is drawn climbing rather than moved, over the top of the crew
   pinned inside it, with a short plume and fire into the pad. When it clears
   the sky the crew are put back on the ground at the pad -- at that moment
   rather than when the screen is dismissed, because the ending is exactly when
   somebody might close the game, and a crew parked in the sky by a quit would
   be a lost save. The victory flag is its own additive save section, so a world
   made before any of this loads unwon and nothing needed version-gating. What
   is left standing is the pad, drawn from the bottom rows of the same art, and
   digging it hands back nothing. `tests/rocket_ascent.cpp`.

Test single player, two-player desktop, and browser co-op before the full run.
Cover missing fuel/core, blocked sky, mixed readiness, cancellation, disconnects,
save/reload at each launch stage, mining loaded rockets, repeat interaction, and
returning after victory. Existing worlds must remain loadable and explorable.
