#include "materials.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
  /* Grass is dirt that something is growing on, so every physical column here
     is dirt's. It falls, piles and wets identically; only the colour and the
     spreading rule differ. Making it behave differently would mean a block of
     turf dug up and dropped acted unlike the dirt it plainly is. */
  { "Grass", KIND_POWDER, 140, 150,   12,   0,   0, 192,  2,   80,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x5E9A3C, 0x3F7028, 0x375E22, 0x264A18, 0 },
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
  /* The lamp. Deliberately a COLD light: heatCond is air's, spawnTemp is
     ambient, and it has no boil or ignite row at all, so hanging one in a
     wooden room lights the room and does not eventually burn it down.

     That is worth stating because the alternative was free: fire and lava
     already glow, and a torch could have been "a fire that does not go out".
     But every light source in the game would then also be a heat source, and
     the moment you want a lit room you would be committing to a thermal
     problem you never asked for. Heat and light are separate systems here
     (g_matLight is not derived from temperature), and the lamp is the material
     that makes that separation visible. */
  { "Lamp",  KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,    6,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xFFF2C4, 0xFFD87A, 0xFFF2C4, 0xFFD87A, 0 },
  /* The torch: the cheap light, and the first thing in the table you can walk
     through (see g_matPassable in materials.h).

     Cold, for the reason the lamp's note gives at length -- it applies with more
     force here, not less. A torch is exactly the thing you want to stud a wooden
     corridor with, so making it hot enough to look like a flame is making it hot
     enough to burn the corridor down some minutes later, in a way you would
     never connect to the decision. The colour carries the fire; the temperature
     column does not have to.

     Dimmer than the lamp on purpose, and by enough to feel: 150 against 255
     reaches 50 cells through air where the lamp reaches 85. That is the whole
     difference between them apart from collision, so it has to be a real one, or
     the lamp becomes the torch you happen to have more of.

     STR_LOOSE, softer than the lamp's STR_SOFT, so the starting tool clears one.
     A light you can walk through is a light you will put in the wrong place. */
  { "Torch", KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,    6,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xFFC46A, 0xF08A2A, 0xFFC46A, 0xF08A2A, 0 },
  /* --- the ores ------------------------------------------------------------
     Powders, so a vein you break comes away as rubble you can shovel into a
     furnace, and so a heap of ore behaves like a heap of anything else. They
     slide poorly (90 against sand's 235): broken rock stacks in steep piles
     rather than pouring flat, which also means an ore heap stays where you
     shovelled it instead of spreading across the furnace floor.

     heatMassShift 1 is the number that makes a furnace a furnace. Ore holds
     twice the heat of ordinary material, so it comes up to temperature slowly
     and a single heater touching one corner will not smelt a pile -- you have to
     commit real heat to it and wait. Dropping this to 0 turns smelting into
     something that happens the instant you place a heater, which is the whole
     process the ore exists to create.

     boilTemp is the smelting point and boilsTo the molten metal; the slag half
     comes from g_matSmeltYield. See the note in materials.h.

     Both read as rock with something IN it rather than as a new kind of stone:
     grey base, and a second colour the eye reads as the metal. Against stone's
     cool 0x6E747C they are warmer and more mottled, which is what makes a vein
     legible in an unlit cave wall at two pixels a cell. */
  { "CuOre", KIND_POWDER, 168,  90,   40,   0,   0,   0,  0,  100,  1,   0,   0,    0,  MAT_EMPTY, degC(165), MAT_COPPER_MELT, 0, MAT_EMPTY, 0, 0x5E6A62, 0x3FA07A, 0x5E6A62, 0x3FA07A, 0 },
  { "FeOre", KIND_POWDER, 170,  90,   40,   0,   0,   0,  0,  100,  1,   0,   0,    0,  MAT_EMPTY, degC(190), MAT_IRON_MELT,   0, MAT_EMPTY, 0, 0x6A625C, 0x9E6A42, 0x6A625C, 0x9E6A42, 0 },
  /* Molten slag. Density 190 is the load-bearing number in the whole smelting
     system: it must be below molten iron's 215 and molten copper's 218, because
     that inequality IS the separation. Nothing sorts the melt -- the denser metal
     sinks through the lighter slag using the same rule that lets steam bubble up
     through water, and if this number ever creeps above either metal the furnace
     silently starts delivering slag at the bottom and metal on top.

     Thick: viscosity 120 means it refuses to flow sideways about half the time,
     so it pools and crusts where it formed instead of running away across the
     floor and taking the metal with it. dispersion 3 for the same reason.

     heatMassShift 2 so a fresh pour stays liquid long enough to actually
     separate. A slag that froze on contact would trap the metal inside it. */
  { "MoltSlag",KIND_LIQUID,190,  0,    0,   3, 120,   0,  0,  110,  2,   0, degC(190), degC(120), MAT_SLAG, 0, MAT_EMPTY, 0, MAT_EMPTY, 0, 0x8A5030, 0x5A3020, 0x8A5030, 0x5A3020, 0 },
  /* Frozen slag: the waste. Static rather than a powder, so it forms a CRUST
     over the metal that has to be broken rather than a heap that slumps off it
     -- "break what is left over" is the step, and a powder would do it for you.
     Dark and glassy so a finished furnace reads at a glance: black crust on top,
     metal underneath. STR_LOOSE in materials.cpp, because a byproduct you cannot
     remove with the starting tool would dead-end the whole progression. */
  { "Slag",  KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,   60,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x3A322E, 0x2A2422, 0x3A322E, 0x2A2422, 0 },
  /* The cells of a device. Conducts heat like a machine (200, near iron) for one
     specific reason: a thermocouple has to be able to FEEL the furnace it is
     bolted to, and it senses by reading temp[] at its own cells. An insulating
     casing would leave it reporting room temperature next to molten iron.

     No phase changes at all -- a machine that melted while doing its job would
     be a machine you could not use anywhere interesting. Flat dark colour,
     because what you actually see is the device's own sprite drawn over the top;
     this is only what shows if that ever fails, and a flat colour makes such a
     failure obvious rather than plausible. */
  /* --- the heat ladder -----------------------------------------------------
     Fire spawns at 205 C and cannot smelt anything. That is not a bug and it is
     the reason this whole group exists: measured into a lidded, shallow charge of
     ore, a continually fed fire delivers a peak of 144 C, lava 175, plasma 201.
     A gas has almost no heat CAPACITY, so it arrives hot and has nothing to give.
     What melts metal is not a hot flame, it is a hot thing with mass.

     So the burning forms below are STATIC solids with enormous heatMassShift,
     not gases. They sit in the firebox where you put them and spend their heat
     slowly into whatever is above.

     Clay is IMPERMEABLE -- moisture capacity 0 -- and that is not flavour, it is
     what makes the lake a lake. At capacity 200 the bed drank its own pond:
     measured, 23673 cells of water fell to 12239 within 3000 frames, absorbed
     rather than spilled (the surface shrank inward instead of spreading). A clay
     bed holding water is the reason ponds exist. */
  { "Clay",  KIND_POWDER, 145,  60,   20,   0,   0,   0,  0,   70,  0,   0,   0,    0,  MAT_EMPTY, degC(120), MAT_CERAMIC, 0, MAT_EMPTY, 0, 0x8A7A6A, 0x6A5A4E, 0x5E5248, 0x453C34, 0 },
  /* Ceramic. heatCond 18 is the whole point of the material: it is the second-best
     insulator in the table after rubber, and unlike rubber it has no melting point
     at all, so it is the first thing you can build a furnace OUT of. Everything
     else that survives furnace temperatures (graphene) also conducts superbly,
     which makes it a radiator rather than a lining. */
  { "Ceramic",KIND_STATIC,255,   0,    0,   0,   0,   0,  0,   18,  2,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0xB89A80, 0x9A7C64, 0xB89A80, 0x9A7C64, 0 },
  /* Coal. Ignites at 90 C, which is chosen against what can actually reach it
     rather than picked for flavour: measured, a wood fire burning ON a pile warms
     it to about 57 C, while burning WOOD reaches about 98 C. So a bare flame will
     not light coal and a proper wood fire will -- which is both how coal is really
     lit and a small piece of progression, since it means the first firebox needs
     kindling. At 120 C nothing available early could light it at all.

     On its own it is a copper-grade heat source and cannot touch iron, which is
     the gap fuel exists to close. */
  { "Coal",  KIND_POWDER, 130, 120,   90,   0,   0,   0,  0,   40,  1,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY, degC(90), MAT_EMBER, 0, 0x2A2A2E, 0x18181C, 0x2A2A2E, 0x18181C, 0 },
  /* Burning coal. Static so it stays in the firebox, mass 3 so it holds eight
     times the heat of ordinary material, and it ENDS by cooling rather than by a
     decay roll: it is spent when it has given its heat away, which is what
     "burns slowly" actually means and what makes a bigger firebox burn longer.

     It burns at 175 C, not 215, and THAT is what keeps coal off iron rather than
     any amount of tuning its drive. g_matDrive clamps at the source's own
     temperature, so it sets how FAST heat arrives and not how hot things get --
     given enough frames even a drive of 2 saturates a charge, and coal at 215 C
     smelted iron 99% of the time. Copper ore needs 165 C and iron 190, so a fuel
     that simply does not burn past 185 can do the one and never the other -- at
     175 it was marginal, delivering exactly 165 and smelting only 37% of a copper
     charge; 185 clears copper comfortably and still leaves iron well out of
     reach. Coal
     not being hot enough for iron is the entire reason fuel exists.

     heatMassShift is the lever that decides how much a fuel can DELIVER, and it is
     worth understanding why rather than tuning it blind. Conduction moves
     (adiff * cond) >> 9 into the neighbour, but the source only drops that amount
     shifted down by its mass -- so at shift 4 an ember gives away sixteen units of
     heat for every one it loses. That is what lets a finite firebox hold a charge
     near temperature instead of levelling with it and stopping. */
  { "Ember", KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,  220,  4,   0, degC(185), degC(70), MAT_EMPTY, 0, MAT_EMPTY, 0, MAT_EMPTY, 0, 0x8A2A10, 0x5A1808, 0x8A2A10, 0x5A1808, 0 },
  /* Coal slaked in water. Ignites at wood's own 80 C -- a prepared fuel should be
     the easy thing to light, not another hurdle -- and burns far hotter. See
     g_matWetInto for how you make it. */
  { "Fuel",  KIND_POWDER, 140, 100,   80,   0,   0,   0,  0,   45,  1,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY, degC(80), MAT_FUELFIRE, 0, 0x2E3A34, 0x1C2622, 0x2E3A34, 0x1C2622, 0 },
  /* Burning fuel. Maximum conductivity and mass 4 -- sixteen times the heat
     capacity -- so it can hold a charge above iron's smelting point for long
     enough to get through it, which ember cannot. */
  { "FuelFire",KIND_STATIC,255,  0,    0,   0,   0,   0,  0,  255,  5,   0, degC(215), degC(95), MAT_EMPTY, 0, MAT_EMPTY, 0, MAT_EMPTY, 0, 0xFFD8A0, 0xFF8830, 0xFFD8A0, 0xFF8830, 0 },
  { "Device",KIND_STATIC, 255,   0,    0,   0,   0,   0,  0,  200,  0,   0,   0,    0,  MAT_EMPTY,   0, MAT_EMPTY,   0, MAT_EMPTY,      0,  0x2A2F3A, 0x2A2F3A, 0x2A2F3A, 0x2A2F3A, 0 },
};

u32 g_colorLut[MAT_COUNT * 256];
u32 g_heatLut[256];
u8  g_heatAlpha[256];
u8  g_matGlows[MAT_COUNT];
u8  g_matDecay[MAT_COUNT];
u8  g_matStrength[MAT_COUNT];
u8  g_matPassable[MAT_COUNT];
u8  g_matSmeltYield[MAT_COUNT];
u8  g_matConducts[MAT_COUNT];
u8  g_matWetInto[MAT_COUNT];
u8  g_bgRetain[MAT_COUNT];
u8  g_matDrive[MAT_COUNT];
u8  g_matLight[MAT_COUNT];
u8  g_matOpacity[MAT_COUNT];
u8  g_lightShade[256];
u32 g_bgColorLut[MAT_COUNT * 16];
u32 g_skyLut[SKY_BAND];
u32 g_caveLut[16];

/* The durability ladder, in one place, ordered so the ranking is readable at a
   glance -- which is the whole reason this is not a MATS[] column.

   Anything absent from this table is STR_NOTHING and a shot flies straight
   through it. That default is right rather than lazy: it covers air, every gas
   and every liquid, and "you cannot shoot a hole in water" is the correct
   behaviour for all of them. A new material only needs a row here if it is
   something you could reasonably expect to stop a projectile.

   Molten forms are deliberately soft -- MoltIron is STR_NOTHING while Iron is
   STR_METAL. Melting a wall is a way THROUGH it, which is the sort of thing the
   heat model already makes possible and the tech tree should reward. */
static void initStrength() {
    for (int m = 0; m < MAT_COUNT; ++m) g_matStrength[m] = STR_NOTHING;

    /* Every liquid, by kind rather than by name. Doing it from MATS[] means a
       liquid added later is covered without anyone remembering to come here --
       and "shots do not notice water" is exactly the kind of omission that
       would go unreported for weeks because it looks like a physics quirk. */
    for (int m = 1; m < MAT_COUNT; ++m)
        if (MATS[m].kind == KIND_LIQUID) g_matStrength[m] = STR_FLUID;

    g_matStrength[MAT_SAND]        = STR_LOOSE;
    g_matStrength[MAT_DIRT]        = STR_LOOSE;
    g_matStrength[MAT_GRASS]       = STR_LOOSE;

    g_matStrength[MAT_TORCH]       = STR_LOOSE;
    /* Slag is deliberately the softest solid in the table. It is waste, and a
       byproduct the starting tool could not clear would dead-end the whole
       progression the moment the player smelted anything. */
    g_matStrength[MAT_SLAG]        = STR_LOOSE;

    g_matStrength[MAT_ICE]         = STR_SOFT;
    g_matStrength[MAT_LAMP]        = STR_SOFT;
    g_matStrength[MAT_WOOD]        = STR_SOFT;
    g_matStrength[MAT_RUBBER]      = STR_SOFT;

    g_matStrength[MAT_STONE]       = STR_ROCK;
    /* Ceramic is as hard as the rock it replaces -- a furnace you could scratch
       apart with the starting tool would not be worth firing the clay for. Clay
       and coal are loose ground you dig with anything. */
    g_matStrength[MAT_CERAMIC]     = STR_ROCK;
    g_matStrength[MAT_CLAY]        = STR_LOOSE;
    g_matStrength[MAT_COAL]        = STR_LOOSE;
    g_matStrength[MAT_FUEL]        = STR_LOOSE;
    /* Ore is as hard as the rock it sits in, so whatever gets you through stone
       gets you the ore -- finding a vein should never also mean needing a
       different tool for it. */
    g_matStrength[MAT_COPPER_ORE]  = STR_ROCK;
    g_matStrength[MAT_IRON_ORE]    = STR_ROCK;

    /* Devices are metal: a real tool takes one apart, the starting one does not.
       They are deliberately NOT indestructible like the heater and cooler --
       a machine you place should be a machine you can dismantle. */
    g_matStrength[MAT_DEVICE]      = STR_METAL;

    g_matStrength[MAT_IRON]        = STR_METAL;
    g_matStrength[MAT_COPPER]      = STR_METAL;
    g_matStrength[MAT_MERCURY_ICE] = STR_METAL;

    g_matStrength[MAT_GRAPHENE]    = STR_HARD;

    /* The border is made of Wall, so anything softer than ABSOLUTE here would
       let a stray shot punch a hole in the edge of the world -- and every
       movement rule in world.cpp assumes that ring is intact and skips its
       bounds checks accordingly. Clone, Void, Heater and Cooler are machines
       rather than scenery: destroying one by waving a gun at it would make
       every contraption built around them fragile in a way nothing warns about. */
    g_matStrength[MAT_WALL]        = STR_ABSOLUTE;
    g_matStrength[MAT_CLONE]       = STR_ABSOLUTE;
    g_matStrength[MAT_VOID]        = STR_ABSOLUTE;
    g_matStrength[MAT_HEATER]      = STR_ABSOLUTE;
    g_matStrength[MAT_COOLER]      = STR_ABSOLUTE;
}

/* Who glows, and what stops light. See g_matLight in materials.h for why this
   is a table of its own and not read off the temperature field.

   The attenuation numbers are chosen as REACHES, which is the only way they
   can be reasoned about: light of strength s crosses s/opacity cells, so at
   LIGHT_MAX these are

       air        3  ->  85 cells    daylight into a cave, a lamp's radius
       gas        5  ->  51          smoke and steam dim a room a little
       liquid    12  ->  21          you can see underwater, not far
       solid     38  ->   6          six cells of rock and it is properly dark

   Air started at 6, for 42 cells. Both revisions of that number came from
   looking at the screen rather than from theory, and both went the same way,
   which is worth recording because the instinct is to go the other way:

     at 6 (42 cells) a lamp lit a circle a tenth of the screen across, and in a
     room with a corridor off it the light died before it reached the doorway,
     so there was nothing to judge how light turns corners by.

     at 4 (63 cells) a lamp on the ceiling of an ordinary 46-cell-tall room put
     the floor at a third of full brightness -- and a third of a BACKDROP
     colour, which is already darkened to read as unreachable, is nearly black.
     The room was lit and still looked unlit.

   Cells here are two screen pixels, so a reach in cells is roughly half of it
   in pixels, and every figure that sounds generous is half as generous as it
   sounds. 85 cells is a third of the view across: a lamp you light a room
   with, and daylight that reaches properly into a cave mouth.

   LIGHT_MARGIN tracks this number -- see the note there before changing it.

   The solid figure is the one that decides how a hillside looks, and it is
   pulled hard in both directions. Longer, and light penetrating the ground
   gives it depth instead of a silhouette -- but a room with a thin roof fills
   with daylight, since the same number governs both. Measured, with the shade
   floor at 16%:

       att   ground fades over   room under a 6-cell roof
        44        5 cells                 16%   dark
        38        7 cells                 28%   dim
        26        8 cells                 50%   lit
        20       11 cells                 62%   lit

   26 and below is where a roof you would actually build stops working, so the
   useful range is narrow and 38 sits at the far end of it. Note that a 2-cell
   roof leaks badly at EVERY value in this table -- 68% even at 44 -- so thin
   roofs were never dark and this change did not make them worse. A wall that
   is meant to keep the daylight out has to be a real wall. */
static void initLight() {
    for (int m = 0; m < MAT_COUNT; ++m) {
        g_matLight[m]   = 0;
        switch (MATS[m].kind) {
        case KIND_EMPTY:  g_matOpacity[m] = 3;  break;
        case KIND_GAS:    g_matOpacity[m] = 5;  break;
        case KIND_LIQUID: g_matOpacity[m] = 12; break;
        default:          g_matOpacity[m] = 38; break;
        }
    }

    /* The lamp is the only thing you build for light, so it is the brightest
       and everything else is measured against it. */
    g_matLight[MAT_LAMP]     = LIGHT_MAX;
    /* The torch. 150 was the first guess and it was wrong, in a way only a
       screenshot found: it lit nothing. The trap is that emission sets PEAK
       BRIGHTNESS and REACH with one number -- reach is emission/attenuation, and
       attenuation belongs to the medium, not the source -- so "dimmer" cannot
       mean "bright but local", it means murky everywhere. At 150 the torch
       rendered its OWN CELL at 69% and a corridor built to its measured spacing
       still read as unlit beside an identical corridor lit by lamps.

       A reach test hid this, and it is worth naming why: it asked for light > 0,
       and reported 49 cells. At the far end of a linear falloff the value is 1,
       and 1 is not light, it is arithmetic. Half brightness or better -- with
       unlit already rendering at 15% -- is the honest measure, and by it 150
       reached 20 cells, not 49.

       205 renders the torch's own cell at 85% and holds half brightness to 38
       cells, against the lamp's 100% and 55. That is a real ladder in both
       numbers rather than a difference you have to be told about, and it leaves
       the torch what it should be: a light that works. */
    g_matLight[MAT_TORCH]    = 205;
    g_matLight[MAT_PLASMA]   = 240;
    /* Molten slag is incandescent, so a working furnace lights its own room --
       which is worth having, since a furnace is somewhere you stand and wait. */
    g_matLight[MAT_SLAG_MELT] = 100;
    /* Burning things light the room they are burning in. Fuel brighter than coal,
       matching how much hotter it is. */
    g_matLight[MAT_FUELFIRE]  = 210;
    g_matLight[MAT_EMBER]     = 150;
    g_matLight[MAT_FIRE]     = 200;
    g_matLight[MAT_LAVA]     = 190;
    g_matLight[MAT_IRON_MELT]   = 170;
    g_matLight[MAT_COPPER_MELT] = 170;
    /* Cold fire is light too. It burns nothing, but a blue flame you cannot
       see by would be a strange thing to hold. */
    g_matLight[MAT_COLDFIRE] = 120;
    g_matLight[MAT_HEATER]   = 110;

    /* A source is transparent to its own light and to everyone else's --
       otherwise a wall of lamps lights only its front row, and a lava lake
       lights only its surface. */
    for (int m = 0; m < MAT_COUNT; ++m)
        if (g_matLight[m]) g_matOpacity[m] = 3;

    /* See g_lightShade in materials.h -- both for why the mapping is not the
       identity, and for why it must have no knee in it. */
    for (int l = 0; l < 256; ++l) {
        const double t = (double)l / (double)LIGHT_MAX;
        const double s = (double)LIGHT_MIN_SHADE
                       + (255.0 - (double)LIGHT_MIN_SHADE) * pow(t, LIGHT_GAMMA);
        g_lightShade[l] = (u8)(s + 0.5);
    }
}

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

/* Cave dark, and the sky ramp above it. */
static void initZoneColours() {
    /* Underground. Warm-shifted rather than neutral grey so it reads as rock
       rather than as an unlit hole, and barely speckled -- enough to have some
       grain at all, not enough to compete with anything in front of it. */
    const u32 CAVE_A = 0x14110F, CAVE_B = 0x1C1916;
    for (int k = 0; k < 16; ++k) g_caveLut[k] = lerpColor(CAVE_A, CAVE_B, k * 17);

    /* The sky: cold high air, through daylight blue, into a dusty haze near the
       ground. Three stops rather than two because a straight two-colour ramp
       reads as a flat wash, and the bend toward haze is what makes it look like
       distance rather than paint.

       A plain linear ramp, not weighted toward either end. The join with the
       underground is handled where it actually happens -- at the chunk
       boundary, by backdrop() -- rather than by trying to shape this curve to
       land on the cave colour at a depth that is different in every column. */
    const u32 STOPS[4] = { 0x24406E, 0x3D6698, 0x6E8CA8, 0x8FA0AC };
    const int nSeg = 3;
    const int span = SKY_BAND / nSeg;
    for (int y = 0; y < SKY_BAND; ++y) {
        const int seg = imin(nSeg - 1, y / span);
        const int t   = imin(255, ((y - seg * span) * 256) / span);
        g_skyLut[y] = lerpColor(STOPS[seg], STOPS[seg + 1], t);
    }
}

static void initBgColours() {
    for (int m = 0; m < MAT_COUNT; ++m) {
        const MatInfo& mi = MATS[m];
        for (int k = 0; k < 16; ++k) {
            const u32 face = lerpColor(mi.dryA, mi.dryB, k * 17);
            /* 34% of the way from a cold near-black toward the material's own
               colour. Low enough that background never competes with material
               for attention, high enough that stone and dirt are still
               tellable apart behind you -- which they have to be, since the
               background is how you read what layer you have dug into. */
            g_bgColorLut[(m << 4) | k] = lerpColor(0x090B11, face, 88);
        }
    }
    /* Nothing behind you at all is the void, not a dark grey. */
    for (int k = 0; k < 16; ++k) g_bgColorLut[(MAT_EMPTY << 4) | k] = 0x000000;
}

/* See g_matPassable in materials.h. A whitelist rather than a rule derived from
   some other column, because there is no honest rule to derive it from: nothing
   about being a light, or being cheap, or being static makes a thing walkable.
   It is a per-material decision and the list should stay short enough to read. */
/* See g_matSmeltYield in materials.h. Copper is the early metal: cooler to
   smelt and more generous, 55% metal against iron's 40%. Iron makes you shift
   half again as much rock for the same bar, which is what puts the two on a
   ladder rather than making one a recolour of the other. */
static void initSmelting() {
    for (int m = 0; m < MAT_COUNT; ++m) g_matSmeltYield[m] = 0;
    g_matSmeltYield[MAT_COPPER_ORE] = 140;   /* 55% */
    g_matSmeltYield[MAT_IRON_ORE]   = 102;   /* 40% */
}

/* See g_matConducts in materials.h. A short explicit list rather than a rule, for
   the reason given there: electrical and thermal conduction are different
   properties that merely correlate in this table today. */
static void initConducts() {
    for (int m = 0; m < MAT_COUNT; ++m) g_matConducts[m] = 0;
    g_matConducts[MAT_IRON]     = 1;
    g_matConducts[MAT_COPPER]   = 1;
    g_matConducts[MAT_GRAPHENE] = 1;
}

/* See g_matWetInto in materials.h. One entry, and it is the whole fuel step. */
static void initSlaking() {
    for (int m = 0; m < MAT_COUNT; ++m) g_matWetInto[m] = 0;
    g_matWetInto[MAT_COAL] = MAT_FUEL;
}

/* See g_bgRetain in materials.h. Ceramic is the reason this table exists: a
   chamber lined with it holds heat, which is what makes a furnace a building
   rather than a bonfire. Stone helps a little -- rock is not a bad insulator and
   a natural cave really does hold warmth better than open air -- and everything
   else is left at zero so the overwhelming majority of the world behaves exactly
   as it did before. */
static void initBgRetain() {
    for (int m = 0; m < MAT_COUNT; ++m) g_bgRetain[m] = 0;
    g_bgRetain[MAT_CERAMIC] = 220;
    g_bgRetain[MAT_RUBBER]  = 200;
    g_bgRetain[MAT_STONE]   = 60;
    g_bgRetain[MAT_WALL]    = 60;
}

/* See g_matDrive in materials.h. The two numbers here ARE the ladder: they decide
   what each fuel can smelt, and they were set against measured targets rather than
   picked -- copper ore needs 165 C and iron 190, and the baseline to beat is
   lava's 175 and plasma's 201. */
static void initDrive() {
    for (int m = 0; m < MAT_COUNT; ++m) g_matDrive[m] = 0;
    g_matDrive[MAT_EMBER]    = 10;
    g_matDrive[MAT_FUELFIRE] = 40;
}

static void initPassable() {
    for (int m = 0; m < MAT_COUNT; ++m) g_matPassable[m] = 0;
    g_matPassable[MAT_TORCH] = 1;
}

void initMaterials() {
    initStrength();
    initPassable();
    initSmelting();
    initConducts();
    initSlaking();
    initBgRetain();
    initDrive();
    initLight();
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

        /* Strength is filled from its own table below rather than here; see
           initStrength() for why the ladder lives in one block. */

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

    initBgColours();
    initZoneColours();
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
