#include "materials.h"
#include <stdio.h>
#include <stdlib.h>

/* One row per material, in enum order. The zero-heavy columns read cleanly if
   you think of them as "off unless set": most materials have no phase changes.

   Heat design notes:
   - Air (Empty) and Wall conduct poorly on purpose; give air a realistic
     conductivity and a single flame heats the whole room in seconds. Air's
     number applies only to air-against-something-else -- air mixing with air
     is a separate rate (AIR_MIX in world.h), because one number could not do
     both jobs. See the note on the Empty row.
   - Iron, Copper and Graphene are the opposite extreme. All three share the
     maximum conductivity the exchange rule can express (see below), and are
     ranked against each other by `heatSpread` -- how many cells of bar a heat
     front crosses per frame -- because `heatCond` had no room left above iron.
   - Stone melts to Lava; Lava freezes back to Stone well below that. The gap
     between those two is hysteresis -- without it a cell right at the melting
     point would flicker between the two states every frame.
   - Steam conducts very little and condenses only once quite cool, so it rides
     upward a long way before turning back to water.
   - Wood ignites into HOT fire (see igniteTemp handling in world.cpp), so a
     flame conducts into neighbouring wood until it too lights: fire spreads
     through a plank on its own.
   - Water freezes to Ice, with hysteresis for the same reason as stone/lava.

   Temperatures are written as degC(x) -- see the encoding note in materials.h.
   A bare 0 in a temperature column still means "disabled" (or, for spawnTemp,
   "start at ambient"), which is why it is never written as degC of anything.

   Three values had to be lowered to make room for the cold end of the scale,
   because the byte was full: the top is now +215 C rather than +255. Only the
   three hottest entries were affected, and the gaps that matter were kept:

     lava spawn   255 C -> 215 C   (the cap; lava is the hottest thing there is)
     fire spawn   235 C -> 205 C   (still the hottest gas, and still far above
                                    wood's ignition point, which is what the
                                    number is actually for)
     stone melts  220 C -> 185 C   (lowered by more than the offset on purpose,
                                    to keep ~30 units of headroom between the
                                    top of the scale and the melting point.
                                    That headroom is what lets an embedded
                                    heater melt rock: measured at 102 cells of
                                    lava after the change against 99 before.)

   Everything else simply shifted by the offset, so its distance from ambient
   -- which is what actually drives the sim -- is unchanged. Steam still has
   exactly 70 units to cool through before it condenses, so it rides just as
   far up as it always did.

   Lava's molten band did narrow, from 155 units to 115: it is spawn minus
   freeze, and freezing cannot drop below water's boiling point without
   breaking "lava is always hot enough to boil water". That sounds like it
   should shorten how long a puddle glows, and measured, it very nearly does
   not -- 2418 frames before against 2423 after for a thick blob, 81 against 71
   for a thin puddle. The narrower band is offset by a shallower gradient to
   ambient (195 units where it was 235), so lava sheds heat more slowly by
   almost exactly as much as it has less heat to shed. Worth knowing before
   "fixing" the band. */

/*  name     kind        dens slDry slWet disp jit  cap wick  cond mass sprd spawn coolT coolsTo   boilT boilsTo   ignT burnsTo  quench       dryA      dryB      wetA      wetB   */
MatInfo MATS[MAT_COUNT] = {
  /* Air's heatCond is only ever used AGAINST SOMETHING ELSE -- air-to-air goes
     through AIR_MIX instead (see world.h). So read this 6 as "how well a hot
     solid bleeds into open air", nothing more.

     It came down from 12 when air started mixing, because air that spreads
     heat also strips it off a hot solid, and lava was cooling in a quarter of
     the time. 6 rather than 5 on purpose: 5 puts the thick-blob lifetime back
     but overshoots badly on thin lava (1104 -> 2164 frames), which flattens the
     difference between a deep pool and a shallow puddle to nothing. At 6 all
     three test geometries land within 16% of the old build AND a thick blob
     still outlasts a thin puddle by 1.4x, which is the behaviour worth
     keeping. The curve is steep here -- 7 already halves lava's life -- so do
     not nudge this without re-running the puddle measurements. */
  { "Empty", KIND_EMPTY,    0,   0,    0,   0,   0,   0,  0,    6,  0,   0,  0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x0E0E12, 0x0E0E12, 0x0E0E12, 0x0E0E12, 0 },
  { "Wall",  KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   30,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x4C5158, 0x353A41, 0x4C5158, 0x353A41, 0 },
  { "Stone", KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   85,  0,   0,   0,    0,  MAT_EMPTY, degC(185), MAT_LAVA, 0, MAT_EMPTY,   0,  0x6E747C, 0x50555C, 0x6E747C, 0x50555C, 0 },
  { "Sand",  KIND_POWDER, 150, 235,   60,   0,   0, 128,  1,   90,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xE2CC86, 0xC7A85E, 0x8F7038, 0x6E5528, 0 },
  { "Dirt",  KIND_POWDER, 140, 150,   12,   0,   0, 192,  2,   80,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x8C6B45, 0x6B4F33, 0x4A3524, 0x33241A, 0 },
  { "Water", KIND_LIQUID, 100,   0,    0,   5,   0,   0,  0,  180,  0,   0,   0, degC(0), MAT_ICE, degC(100), MAT_STEAM, 0, MAT_EMPTY,   0,  0x3D7FD1, 0x2C5FA6, 0x3D7FD1, 0x2C5FA6, 0 },
  /* Ice is static, so it never flows and never gets displaced -- tryMove
     refuses to swap with any non-liquid, non-gas, which is what stops water
     tunnelling through a frozen sheet regardless of the densities. The density
     below is therefore never consulted; it is set lighter than water only so
     it does not read as a lie.

     Melting at +6 C rather than exactly 0 C is hysteresis against water's
     freeze at 0 C, the same trick stone and lava use: it leaves 40..45 as a
     band where both states are stable, so a cell sitting right at freezing
     cannot flip back and forth every frame.

     spawnTemp is well below melting on purpose. Hand-placed ice arrives at
     -16 C; without that it would spawn at ambient and melt on the very next
     frame, which looks like the brush is broken. */
  { "Ice",   KIND_STATIC,  92,   0,    0,   0,   0,   0,  0,  120,  0,   0, degC(-16), 0, MAT_EMPTY, degC(6), MAT_WATER, 0, MAT_EMPTY,  0,  0xC8E8F7, 0x92C4E2, 0xC8E8F7, 0x92C4E2, 0 },
  { "Steam", KIND_GAS,      8,   0,    0,   7, 150,   0,  0,    5,  0,   0, degC(115), degC(45), MAT_WATER, 0, MAT_EMPTY, 0, MAT_EMPTY,  0,  0xD2DAE6, 0x9AA6B6, 0xD2DAE6, 0x9AA6B6, 0 },
  /* Fire's boilTemp was spare (fire has nowhere hotter to go), so it now points
     at Plasma: fire driven all the way to the top of the scale becomes plasma.
     That is only reachable by feeding it external heat -- a heater, or lava
     tapped through a metal run -- since fire spawns 10 C short of it and cools
     from there. It is a deliberate reward for building the heat plumbing. */
  { "Fire",  KIND_GAS,      3,   0,    0,   3,  60,   0,  0,  200,  0,   0, degC(205), degC(100), MAT_EMPTY, degC(215), MAT_PLASMA, 0, MAT_EMPTY, MAT_WATER, 0xFFE7A0, 0xE8410C, 0xFFE7A0, 0xE8410C, 0 },
  /* Plasma. The brief was "a hotter fire", and the honest constraint is that
     there is no room to be hotter: fire spawns at 205 C and the scale ends at
     215 C. Ten degrees is nothing, so peak temperature is not where the
     difference can live. Three other columns carry it instead:

     heatMassShift 3 is the main one, and it is the whole reason plasma can do
     what fire cannot. Fire has mass 0: every degree it hands to a pot comes
     straight off its own temperature, so it drops to its 100 C cutoff and dies
     long before a full pot of water is gone -- which is exactly the wall you
     hit. Plasma delivers at the same full rate but feels only an eighth of the
     loss, so it stays in its working range roughly an order of magnitude
     longer. It is not hotter; it is inexhaustible, which is what actually
     boils a pot dry.

     quenchedBy is 0 rather than MAT_WATER. Fire is snuffed the instant water
     touches it, so it can never work submerged. Plasma flash-boils its way
     through instead, which is the behavioural difference you feel most.

     heatSpread 2 puts it on the same long-range conduction path as the metals,
     and the run rule is "any material with heatSpread > 0" -- so a plasma cloud
     conducts along its own body, and a plasma flame touching a graphene rod
     couples straight into it rather than only heating the one cell it sits on.

     Movement: jitter 30 against fire's 60, so it jets upward instead of
     billowing -- less lazy flame, more arc-torch. Density 1 (fire is 3) so
     plasma rises through fire rather than mingling with it.

     coolTemp 120 C is the anti-lingering knob, and which knob to use for that
     is genuinely counter-intuitive, so it is worth writing down. Plasma first
     shipped dying at 90 C and hung around far too long with nothing to heat.
     The obvious fix -- halve heatMassShift -- does NOT work: measured, 3 -> 2
     cut a cloud's life only 380 -> 357 frames while making it 36% worse at its
     actual job (pot dry 44 -> 60). Mass damps heat lost by CONDUCTION, and what
     makes a cloud linger in open air is the flat per-frame drift toward ambient
     (GAS_COOL), which mass does not scale at all.

     Raising the cutoff instead cost nothing and fixed it: 90 -> 120 C took the
     cloud 380 -> 211 frames with pot-dry unmoved at 44 -> 45. It works because
     plasma spends most of its life in a long cold tail that does no useful
     work; truncating that tail removes only the loitering.

     One thing this trades away: a lone plasma cell now expires FASTER than a
     lone flame (53 frames against fire's 114), because the cutoff lops off the
     tail fire still has. That is deliberate -- a high-energy state that is
     ferocious briefly and then recombines -- but it does mean "plasma outlives
     fire" is no longer true, and should not be re-asserted as if it were a
     regression. What plasma has over fire is delivered heat, not longevity.

     Free bonus of being glow-exempt (see g_matGlows): plasma renders the same
     blue at every temperature, so shortening its cold tail is invisible. There
     is nothing to see fade -- only a shorter time on screen. */
  { "Plasma",KIND_GAS,      1,   0,    0,   6,  30,   0,  0,  255,  3,   2, degC(215), degC(120), MAT_EMPTY, 0, MAT_EMPTY, 0, MAT_EMPTY,    0,  0xF2F8FF, 0x2A5CFF, 0xF2F8FF, 0x2A5CFF, 0 },

  /* ---- the cold set -----------------------------------------------------
     Everything below lives in the bottom 60 units of the scale, and that is
     the single most important fact about tuning any of it. The hot half runs
     ambient..215 C, nearly 200 units; the cold half is ambient..-40 C, sixty.
     Fire has room to be hotter than wood's ignition point by 120 degrees, but
     cold fire has the entire cryogenic range of the game to share with liquid
     nitrogen and frozen mercury. Thresholds down here are packed close
     together by necessity, not by choice, and there is nowhere to move them.

     Raising TEMP_OFFSET would buy cold headroom, but every degree comes off
     the top, where lava (215) and stone's melting point (185) are already
     tuned against each other with about 30 units of slack. That is a real
     option if the cold side ever needs to grow -- it is just not a free one,
     and it would invalidate the lava measurements in the notes above.

     A hard floor to remember: degC(-40) is 0, and 0 is the "disabled"
     sentinel in every temperature column. Nothing can have a threshold at
     exactly -40 C, so the coldest usable setpoint is -39. */

  /* Cold fire is fire's mirror: it rises, wanders and expires, but it chills
     rather than burns. Nothing new in world.cpp was needed for that -- heat
     conduction is symmetric, so a very cold cell with a high heatCond pulls
     warmth OUT of its neighbours exactly as fire pushes warmth in.

     Where fire dies by cooling (coolTemp 100 -> Empty), cold fire dies by
     WARMING, which is the boil column pointed at Empty: past +5 C it is gone.
     The two are the same mechanism read in opposite directions -- but for cold
     fire that mechanism alone is nowhere near enough, and it takes a timer
     (g_matDecay, 6/255 per frame) to actually finish it off. The arithmetic is
     in the g_matDecay note in materials.h; the short version is that its
     gradient to open air is so much smaller than fire's that conduction
     truncates to literally zero, leaving nothing to cool it to death. Untimed
     it lived 503 frames against fire's 94 and rode 359 cells up against fire's
     68, gathering in a layer under whatever it could not get past.

     The counterintuitive part, and the reason the timer is not just a nerf:
     killing it faster made it COLDER in practice. Spent cells were crowding out
     their own replacements, so a bed that turns over quickly keeps delivering
     fresh -39 C material where a lingering one delivers warmed-up leftovers.
     Measured in a graphene bucket of water: the water reaches -25 C instead of
     -18, peak ice goes 1198 -> 1459, and cells escaping past the bucket drop
     863 -> 42.

     6/255 puts cold fire almost exactly on top of fire, which is what "acts
     like fire but cold" ought to mean: 116 frames and 88 cells of rise against
     fire's 108 and 84. Beware measuring this with one run -- a puff's lifetime
     is the last of eleven cells to expire, so it is the maximum of eleven
     geometric draws and swings wildly. Single readings ranged 45..150 at fixed
     settings and even put decay 8 below decay 10; the table above is a 25-run
     mean, which is monotonic. Cooling power is nearly flat across this range
     (1459 ice at decay 6 against 1476 at 10), so this number trades lifetime,
     not potency.

     heatMassShift 2 rather than 1 is the other half. Mass decides how much cold
     ONE cell unloads before it warms up, and with the timer now bounding how
     long cells stick around, mass can be raised for punch without the material
     overstaying. Decoupling the payload from the lifetime is the whole point of
     having two knobs.

     It rises, which real cold gas does not do. That is deliberate and comes
     straight from the brief -- "acts like fire but cold" -- so it behaves like
     a flame you can build with, rather than like a heavy gas pooling on the
     floor. */
  { "ColdFire",KIND_GAS,    3,   0,    0,   3,  60,   0,  0,  200,  2,   0, degC(-39), 0, MAT_EMPTY, degC(5), MAT_EMPTY, 0, MAT_EMPTY,   0,  0xEAFBFF, 0x4FD2F0, 0xEAFBFF, 0x4FD2F0, 0 },

  /* Liquid nitrogen. Real LN2 boils at -196 C, which this scale cannot express
     at all -- so what is modelled is the BEHAVIOUR that matters: it is far
     colder than anything else placeable, and it boils away briskly in a room.
     Boiling at -25 C means an exposed pool is always boiling, exactly as real
     LN2 always is, and only stays liquid where something keeps it cold.

     It boils into cold fire rather than into nothing, which does double duty:
     it conserves the visual (a poured splash throws off a cold plume instead
     of silently vanishing) and it gives cold fire a natural source, the way
     burning gives fire one. Lighter than water, so it floats on top of it.

     heatMassShift 4 is high, and it is what makes LN2 usable rather than
     merely present. At mass 2 a poured pool of 820 cells boiled away in 28
     frames -- under half a second, gone before it could chill anything, and it
     froze no mercury at all. Each step up buys roughly double: 90 frames at
     mass 3, 158 at mass 4, 318 at mass 5. Mass 4 is the point where a splash
     lasts long enough to read as a splash and to do work (it freezes mercury
     reliably from mass 3 up) without a puddle sitting around like a permanent
     fixture. */
  { "LiqN2", KIND_LIQUID,  80,   0,    0,   6,   0,   0,  0,  200,  4,   0, degC(-39), 0, MAT_EMPTY, degC(-25), MAT_COLDFIRE, 0, MAT_EMPTY, 0, 0xD8F4FF, 0x9BDFF7, 0xD8F4FF, 0x9BDFF7, 0 },

  /* Mercury: the only liquid metal, and the only material with a phase change
     at BOTH ends of the scale, which is what makes it worth distilling.
     Flat-coloured like the other metals (see the note below them).

     Denser than everything that moves, so it sinks through water and sand and
     pools underneath -- which is most of the fun of pouring it into a scene.

     Freezing at -30 C is a deliberate departure from the real -38.8 C, and the
     reason is the floor: -38.8 would sit one degree off the coldest value the
     byte can hold, leaving no room for a cold source to actually get there and
     no gap for hysteresis. At -30 there are ten degrees of headroom below it,
     which is what makes freezing mercury a thing you can achieve with LN2 or
     cold fire rather than a thing that is technically representable.

     Boiling at 150 C is likewise far below the real 357 C, which is off this
     scale entirely -- but the number was set by measurement, not by taste. The
     first attempt put it at 200 C, just under lava, on the theory that lava
     should boil mercury. In a real walled boiler it barely worked: a HEATER,
     which is pinned at the top of the scale forever and is the strongest
     sustained source in the game, only carried a mercury charge to about
     140 C, because a vessel loses heat to the room faster than conduction
     tops it up. 200 C was technically reachable and practically not -- 32
     vapour cells against 94 at 150 C.

     150 C also lands it in a genuinely useful spot: 50 degrees clear of
     water's 100 C. A mixture can be held between the two, boiling the water
     off while the mercury stays put, which is the interesting half of
     distilling. The three boiling points now ladder: LN2 at -25, water at
     100, mercury at 150. */
  /* The colour is a silver rather than the near-white it started as (0xB9BEC4):
     down 12 in luminance and up in cool cast, which is what stops it reading as
     a pale wash. It cannot go much darker -- Steam's dark end is 0x9AA6B6 and
     the pale-grey neighbourhood here is crowded, so 0xA3ACB6 lands 45 units off
     Steam by a green-weighted RGB distance where this sits 181 clear. (Mercury's
     own vapour and frozen forms are deliberately NOT counted as collisions: they
     are the same substance and ought to look like it.) */
  { "Mercury",KIND_LIQUID,250,   0,    0,   5,   0,   0,  0,  240,  1,   0,   0, degC(-30), MAT_MERCURY_ICE, degC(150), MAT_MERCURY_GAS, 0, MAT_EMPTY, 0, 0xAAB3BC, 0xAAB3BC, 0xAAB3BC, 0xAAB3BC, 0 },

  /* Mercury vapour condenses at 180 C, twenty degrees under mercury's boiling
     point. That gap is hysteresis, the same trick stone/lava and water/ice
     use, and here it is doing the actual work of a still: a cell has to be
     carried a clear twenty degrees away from the boiler before it commits to
     the other phase, so vapour survives the trip to a cold surface instead of
     flickering back the moment it leaves the heat.

     Density 20 against steam's 8, so mercury vapour rides BELOW steam when
     both are present -- the two separate out on their own, which is the whole
     point of distilling a mixture.

     spawnTemp has to sit above the condensation point or a hand-placed cell
     would turn straight back into a droplet on its first frame and the brush
     would look broken. Same reasoning as ice's cold spawn. */
  { "HgVapour",KIND_GAS,   20,   0,    0,   5, 120,   0,  0,   20,  0,   0, degC(150), degC(130), MAT_MERCURY, 0, MAT_EMPTY, 0, MAT_EMPTY, 0, 0xCED3DA, 0xA7ADB6, 0xCED3DA, 0xA7ADB6, 0 },

  /* Frozen mercury melts at -24 C, six degrees above the -30 C it freezes at
     -- hysteresis again, without which a cell sitting exactly at the boundary
     would flip states every frame. Static, so a frozen puddle holds its shape
     and stops behaving like a liquid entirely.

     Spawned at -35 C rather than ambient for the same reason ice is: placed at
     room temperature it would melt on its first update and the brush would
     look broken. */
  { "FrozenHg",KIND_STATIC,255,  0,    0,   0,   0,   0,  0,  240,  1,   0, degC(-35), 0, MAT_EMPTY, degC(-24), MAT_MERCURY, 0, MAT_EMPTY, 0, 0x9FB0BE, 0x9FB0BE, 0x9FB0BE, 0x9FB0BE, 0 },
  /* All three metals are manufactured solids, so each gets a single flat colour
     (dryA==dryB==wetA==wetB) rather than the per-cell speckle sand and dirt get
     from a colour range. Speckle reads as loose grains; a milled bar should read
     as one uniform material. */
  { "Iron",  KIND_STATIC, 220,   0,    0,   0,   0,   0,  0,  255,  0,   2,   0,    0,  MAT_EMPTY, degC(200), MAT_IRON_MELT, 0, MAT_EMPTY, 0, 0x8A9099, 0x8A9099, 0x8A9099, 0x8A9099, 0 },
  /* Molten iron. Freezing back at 160 C rather than just under the 200 C melting
     point is forced, not chosen, and the same constraint sets molten copper and
     molten rubber. A phase change runs latentDrain(), which pulls the new cell
     30 units toward ambient -- so iron melting at 200 C arrives as molten iron
     at 170 C. If it re-froze anywhere above that it would solidify on its very
     next update, and the bar would sit flickering between the two states
     without ever flowing. Every freeze-back point here is therefore more than
     LATENT_HEAT below its own melting point, with room to spare.

     heatSpread drops to 0 in the molten state, and that is the actual point of
     the whole change. A solid copper bar carries heat five cells a frame; melt
     it and you are left with a puddle that conducts only to what it touches.
     Overheating your heat plumbing does not merely deform it, it destroys the
     property you built it for. */
  { "MoltIron",KIND_LIQUID,215,  0,    0,   4,   0,   0,  0,  255,  1,   0, degC(200), degC(160), MAT_IRON, 0, MAT_EMPTY, 0, MAT_EMPTY, 0, 0xFFC46A, 0xE59A3C, 0xFFC46A, 0xE59A3C, 0 },
  /* The three metals are ranked by `sprd`, NOT by `cond`, and that is forced
     rather than chosen: all three sit at cond 255, which is already 99.2% of
     the hard cap on a single exchange, and 256 or 100000 would be identical to
     it. Iron was at 255 from the start, so there was literally nothing to
     promote copper above. See the heatSpread note in materials.h.

     Iron 2 / copper 5 / graphene 28 is the ratio you feel: it is how many cells
     per frame a heat front advances along a bar, so at 60fps a copper bar
     carries warmth 5 cells a frame where iron manages 2. Real conductivity is
     about 1.7x copper over iron and graphene is off the scale entirely; 2.5x
     and 14x here are exaggerated because a sim at this size needs the
     difference to be visible in the width of a bar you would actually draw.

     Graphene at 28 is close to instant across any sheet you would hand-draw,
     which is the intent -- but it is also the one value here with a real cost,
     since the walk is O(sprd) per cell per frame. 28 was picked as the point
     where a screen-wide bar equalises in about two frames; going much higher
     buys nothing visible and is paid for every frame by every graphene cell. */
  { "Copper",KIND_STATIC, 224,   0,    0,   0,   0,   0,  0,  255,  0,   5,   0,    0,  MAT_EMPTY, degC(175), MAT_COPPER_MELT, 0, MAT_EMPTY, 0, 0xC87838, 0xC87838, 0xC87838, 0xC87838, 0 },
  /* Copper melts 25 C BEFORE iron, which is the right way round both physically
     (1085 C against 1538 C) and for play: the better conductor is the one that
     gives out first, so choosing copper over iron becomes a genuine trade
     rather than a free upgrade.

     175 rather than 185 is set by one measurement. A bed of lava carries a slab
     to about 178 C, so at 185 lava melted a token 3 cells of copper -- true in
     principle and invisible in play. At 175 lava melts copper properly while
     iron (200 C) shrugs it off, which turns a vague ordering into a rule worth
     knowing: LAVA MELTS COPPER BUT NOT IRON. See the molten iron note for why
     the freeze-back has to be 40 C down rather than just under the melt. */
  { "MoltCopp",KIND_LIQUID,218,  0,    0,   4,   0,   0,  0,  255,  1,   0, degC(175), degC(140), MAT_COPPER, 0, MAT_EMPTY, 0, MAT_EMPTY, 0, 0xE8873A, 0xCC6522, 0xE8873A, 0xCC6522, 0 },
  /* Graphene's dark indigo is not decoration. It started as a flat neutral grey
     (0x2F3540), which is the obvious colour for carbon and turned out to be
     nearly invisible: the palette already has three dark greys, and by a
     green-weighted RGB distance that grey sat 25 units from Wall's dark end --
     against 729 for Wall vs Stone, a pair the notes already describe as similar.
     On screen it read as wall. Holding it dark and low-chroma but pushing the
     cast to indigo gets it to 610, comfortably clear of everything, without
     turning it into a bright new primary. */
  /* Graphene has NO melting point, and that is now its defining advantage
     rather than a detail. Iron and copper both give out around 200 C, so above
     that graphene is the only thing left that still conducts. That is the whole
     shape of the conductor ladder: copper is fastest, iron takes more heat than
     copper, and graphene is the only one that survives real heat. */
  { "Graph", KIND_STATIC, 190,   0,    0,   0,   0,   0,  0,  255,  0,  28,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x1E1F4E, 0x1E1F4E, 0x1E1F4E, 0x1E1F4E, 0 },
  /* Lava spawns as hot as the u8 scale allows and stays molten all the way down
     to 100, which more than doubles the band it must fall through. The real
     work is done by the thermal mass of 3 (holds 8x the heat): it keeps a drawn
     puddle glowing for ~10s instead of ~1s while still conducting at full rate,
     so lava melts, lights and boils things exactly as before. Freezing at 100
     also guarantees any lava is hot enough to boil water. */
  { "Lava",  KIND_LIQUID, 200,   0,    0,   3,   0,   0,  0,  120,  3,   0, degC(215), degC(100), MAT_STONE, 0, MAT_EMPTY, 0, MAT_EMPTY,  0,  0xF0641E, 0x9A2408, 0xF0641E, 0x9A2408, 0 },
  { "Wood",  KIND_STATIC, 150,   0,    0,   0,   0,   0,  0,   70,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY, degC(80), MAT_FIRE,   0,  0x8A5A2C, 0x63401E, 0x8A5A2C, 0x63401E, 0 },
  /* Rubber: the best insulator in the game, and the only one that is a solid.
     heatCond 12 against wood's 70 and stone's 85 -- and conduction runs at
     min(condA, condB), so a rubber layer throttles heat crossing it whatever is
     on either side of it. Air itself is 6, so rubber is within a whisker of
     being as good as an air gap while still being something you can build a
     wall out of.

     It melts at 100 C, exactly water's boiling point, so the insulator fails at
     precisely the temperature the thing it is usually wrapped around starts to
     boil. That is the interesting limit: rubber is superb right up until it is
     asked to do anything genuinely hot -- which is why a proper
     high-temperature insulator is still a real gap in the material list. */
  { "Rubber",KIND_STATIC, 150,   0,    0,   0,   0,   0,  0,   12,  0,   0,   0,    0,  MAT_EMPTY, degC(100), MAT_RUBBER_MELT, 0, MAT_EMPTY, 0, 0x2E2E34, 0x212126, 0x2E2E34, 0x212126, 0 },
  /* Molten rubber, and the only viscous liquid in the table. The 230 in the
     jitter column is viscosity, not jitter -- for a LIQUID that byte means
     "chance out of 255 of refusing to flow sideways this frame", which is 90%
     here. See the field note in materials.h.

     Viscosity had to become its own axis because dispersion ran out. Molten
     rubber was already at dispersion 1, and 0 is not "thicker" -- it is "no
     longer a liquid": measured, a dispersion-0 pool poured down one side of a
     container had still not reached the far side after 6000 frames, and a
     vessel draining through its floor kept 270 of 2370 cells welded to the
     walls permanently. A probability gate degrades far more gracefully. A
     stuck cell just tries again next frame, so the liquid still finds its level
     and still drains completely -- it simply takes its time getting there.

     Dispersion is back to 2 for the same reason: with flow gated to one frame
     in ten, the rare frames it DOES move should be worth something, or the
     thing crawls one pixel at a time and looks broken rather than thick.

     Density 115 puts it above water's 100, so it sinks through water and sets on
     the bottom rather than skinning over on the surface. Solid rubber is 150,
     also above water -- though that number is never actually read: rubber is
     KIND_STATIC, and tryMove refuses to displace anything that is not a liquid
     or a gas, so a static material's density is decorative. Molten rubber's is
     the one that does anything.

     115 rather than something heavier keeps the ordering sensible against the
     rest: it sinks through water (100) but still floats on mercury (250), and
     it cannot displace powders at all regardless, since sand and dirt are not
     fluids as far as tryMove is concerned.

     heatMassShift 2 is what makes that density actually visible, and it was not
     obvious until measured. At mass 0 a blob poured onto water sank for a
     moment, cooled past its 65 C set point on the way down and solidified
     halfway -- and solid rubber is KIND_STATIC, so it stopped there and read as
     floating no matter what the density column said. Holding four times the
     heat keeps it molten long enough to reach the bottom before it sets.
     Measured against a pool: mean depth y=321 at mass 0 (above the water's
     339), y=364 at mass 2 (below it). Being an insulator, rubber holding onto
     its heat is the physically sensible reading anyway.

     Not placeable -- the palette lists Rubber only. Melting is something you do
     TO rubber rather than a thing you build with, and every palette row costs
     height for all the others. It is still fully simulated: exactly the
     treatment mercury vapour and frozen mercury already get. */
  { "MoltRub",KIND_LIQUID,115,   0,    0,   2, 230,   0,  0,   12,  2,   0, degC(100), degC(65), MAT_RUBBER, 0, MAT_EMPTY, 0, MAT_EMPTY, 0, 0x4A3A34, 0x352824, 0x4A3A34, 0x352824, 0 },
  /* Clone and Void are machines rather than substances, so they get flat
     colours (dryA==dryB==wetA==wetB). For Clone that is also load-bearing: it
     keeps the id of the material it copies in its unused `moisture` byte, and
     the colour LUT is indexed partly by moisture -- identical entries mean the
     stored id can never change how it renders. Both conduct heat poorly so a
     dispenser full of lava does not cook everything around it. */
  { "Clone", KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   40,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x3FA66A, 0x3FA66A, 0x3FA66A, 0x3FA66A, 0 },
  { "Void",  KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   40,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x6A2A7A, 0x6A2A7A, 0x6A2A7A, 0x6A2A7A, 0 },
  /* Heater and Cooler are machines too, and the only ones whose whole job is
     heat, so unlike Clone and Void they conduct WELL (200, close to iron) --
     a source that could not deliver would be decoration. What actually makes
     them permanent is not in this table: world.cpp pins their temperature
     every frame, so the setpoint below is only what they start at.

     Note the asymmetry in `spawn`: the heater's 255 is real, but the cooler
     wants to start at 0 and cannot say so here, because spawnTemp treats 0 as
     "use ambient". It is placed at ambient and pinned to 0 on its first
     update instead -- one frame of lag, invisible in practice. Do not try to
     fix that by making 0 meaningful; every other material relies on it.

     Both are flat-coloured like the other machines. The heater spends its life
     at 255, where the heat glow blends ~150/255 toward white, so in Glow view
     it reads as white-hot and its own red barely shows -- that is the point.
     Material view is where you see the device itself. */
  { "Heater",KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,  200,  0,   0, degC(215), 0, MAT_EMPTY,  0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xC4392B, 0xC4392B, 0xC4392B, 0xC4392B, 0 },
  { "Cooler",KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,  200,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x58C4DC, 0x58C4DC, 0x58C4DC, 0x58C4DC, 0 },
};

u32 g_colorLut[MAT_COUNT * 256];
u32 g_heatLut[256];
u8  g_heatAlpha[256];
u8  g_matGlows[MAT_COUNT];
u8  g_matDecay[MAT_COUNT];

/* --- the temperature ramp ------------------------------------------------
   The stops now run through ambient rather than starting there, because the
   scale has a cold half: below ambient it walks out to blue, above ambient to
   the old red-orange-white. Ambient itself is a neutral near-black with zero
   alpha, so the enormous majority of cells -- which sit exactly at ambient --
   still render as pure material with no tint at all.

   Ambient is a stop in its own right, and it must be: it is the pivot the
   alpha ramp returns to zero at, so "nothing to say about this cell" reads the
   same from either side.

   The stops are deliberately not evenly spaced. Cold gets two stops across 60
   units while heat gets three across 195, because the frozen range is short
   and would otherwise be a single flat wash of blue. */
static const int RAMP_N = 6;
static const int RAMP_T[RAMP_N] = {
    0,             /* -40 C, the floor and the cooler's setpoint */
    degC(-10),
    degC(20),      /* ambient -- the pivot */
    degC(90),
    degC(160),
    255            /* +215 C, the ceiling and the heater's setpoint */
};
static const u32 RAMP_C[RAMP_N] = {
    0x1E3CC8,      /* deep blue */
    0x59B4E6,      /* ice blue */
    0x14161C,      /* ambient: neutral, and invisible at alpha 0 */
    0x8C1A06,
    0xFF6A10,
    0xFFF2C0
};
/* Glow strength. Capped at ~150/255 rather than near-opaque, so hot sand still
   reads as sand instead of a white blob -- the material shows through even at
   the top of the range, and frozen water still reads as ice rather than a
   flat blue slab. */
static const int RAMP_A[RAMP_N] = { 150, 100, 0, 70, 115, 150 };

/* Piecewise ramp through the stops above, for either an interpolated colour or
   an interpolated alpha. */
static int rampFind(int t, int& f) {
    for (int i = 1; i < RAMP_N; ++i) {
        if (t <= RAMP_T[i]) {
            const int span = RAMP_T[i] - RAMP_T[i - 1];
            f = span ? ((t - RAMP_T[i - 1]) * 255) / span : 0;
            return i;
        }
    }
    f = 255;
    return RAMP_N - 1;
}

static u32 rampColor(int t) {
    if (t <= RAMP_T[0]) return RAMP_C[0];
    int f;
    const int i = rampFind(t, f);
    return lerpColor(RAMP_C[i - 1], RAMP_C[i], f);
}

static int rampAlpha(int t) {
    if (t <= RAMP_T[0]) return RAMP_A[0];
    int f;
    const int i = rampFind(t, f);
    return RAMP_A[i - 1] + ((RAMP_A[i] - RAMP_A[i - 1]) * f) / 255;
}

void initMaterials() {
    for (int m = 0; m < MAT_COUNT; ++m) {
        MatInfo& mi = MATS[m];
        /* Liquids keep their fall speed in the spare `moisture` byte, so a
           liquid that also absorbed water would have the two fight over it.
           Nothing does today; this makes the assumption fail loudly rather
           than as mysterious drifting velocities if one ever is added. */
        mi.invCapQ8 = mi.capacity ? (u16)((255 * 256) / mi.capacity) : 0;

        /* Full heat glow for everything except plasma -- see g_matGlows in
           materials.h for why it is the one exception. */
        g_matGlows[m] = (u8)(m != MAT_PLASMA);

        /* Only cold fire expires on a timer -- see g_matDecay in materials.h
           for why it cannot expire by cooling the way fire does. */
        g_matDecay[m] = (m == MAT_COLDFIRE) ? 6 : 0;

        for (int w = 0; w < 16; ++w) {
            /* Representative moisture for this bucket. Bucket 0 must map to
               exactly 0 so genuinely dry material renders fully dry. */
            int moisture = w ? (w * 16 + 8) : 0;
            int wetF = 0;
            if (mi.capacity) {
                wetF = (moisture * mi.invCapQ8) >> 8;
                if (wetF > 255) wetF = 255;
            }
            for (int t = 0; t < 16; ++t) {
                int tintF = t * 17;   /* 0..255 */
                u32 dry = lerpColor(mi.dryA, mi.dryB, tintF);
                u32 wet = lerpColor(mi.wetA, mi.wetB, tintF);
                g_colorLut[(m << 8) | (w << 4) | t] = lerpColor(dry, wet, wetF);
            }
        }
    }

    for (int t = 0; t < 256; ++t) {
        g_heatLut[t]   = rampColor(t);
        g_heatAlpha[t] = (u8)rampAlpha(t);
    }

    checkCloneColorInvariant();
}

/* Clone parks the id of the material it copies in its otherwise-unused
   `moisture` byte, and the renderer indexes the colour LUT partly by
   (moisture & 0xF0). So the id silently selects a wetness bucket.

   That is only harmless because every one of Clone's 16 buckets is the same
   colour, which follows from its flat palette and zero capacity. Both are
   deliberate, and neither is obviously load-bearing when you are looking at the
   table -- "give Clone a slight speckle" looks like a pure cosmetics change and
   would in fact make a dispenser's colour depend on what it had latched.

   Checked here rather than asserted as a bound on MAT_COUNT, because the count
   is not what matters: this is the property, so this is what gets tested. */
void checkCloneColorInvariant() {
    const u32 c0 = g_colorLut[(MAT_CLONE << 8) | 0x00];
    for (int w = 0; w < 16; ++w) {
        for (int t = 0; t < 16; ++t) {
            if (g_colorLut[(MAT_CLONE << 8) | (w << 4) | t] != c0) {
                fprintf(stderr,
                        "FATAL: Clone's colour varies by wetness/tint bucket, but it "
                        "stores a latched material id in its moisture byte -- a "
                        "dispenser would change colour depending on what it copied. "
                        "Give Clone a flat palette (dryA==dryB==wetA==wetB) and "
                        "capacity 0, or stop packing the id into moisture.\n");
                abort();
            }
        }
    }
}
