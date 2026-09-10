# Ascent: proposed rocket and ending

Design draft, not implemented. The Effigy unlocks escape; building and launching
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

1. Rocket art, assembly recipe, placement, loading UI, and recoverable cargo.
2. Save-backed, host-authoritative readiness/countdown/launch state.
3. Launch cinematic, victory screen, and safe return to the world.

Test single player, two-player desktop, and browser co-op before the full run.
Cover missing fuel/core, blocked sky, mixed readiness, cancellation, disconnects,
save/reload at each launch stage, mining loaded rockets, repeat interaction, and
returning after victory. Existing worlds must remain loadable and explorable.
