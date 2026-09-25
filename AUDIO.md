# Cinderlift sound plan

This is the baseline for the first complete SFX pass. `src/audio.h` is the cue
registry and gives every row below a stable `SFX_...` name and a matching
`res/sfx/*.wav` filename. All 107 cue files now exist. The four **prototype**
cues were hand-tuned with player feedback; the other 103 are generated first
passes awaiting an in-game listening pass. A cue slot is not a promise to make
noise every time its underlying simulation changes.

## Sound identity

Think gritty 8-bit hardware operating inside a cave. Use short pitched pulses,
filtered noise, tiny metallic resonances, and deliberate gaps. The pitch can be
musical, but ordinary machines should communicate a physical action rather than
play a tune. Mining has a soft sandy scrape with a low hit; an unmineable surface
gets the existing higher metal ding. Big damage cuts through both, while small
damage stays subtle. Boss and rocket cues may
be longer and more tonal so they feel like events, not routine work.

Author mono 16-bit PCM WAV at 22,050 Hz for now; the current playback layer
requires that exact format. Keep intentional transients below clipping, trim
dead air at the start, and leave a short tail where it helps the sound read.
One-shot gameplay cues should generally last 60–350 ms; warning/boss/rocket
cues can be longer. Keep UI cues dry, player actions in front, nearby machines
behind them, and ambient details quiet. Do not bury damage or a boss tell under
repetitive digging. A sound should still be identifiable through a laptop
speaker and at a low volume.

Make 2–4 close variations for high-frequency actions before final tuning. The
cue name represents the event family; variation selection must not touch world
generation or combat RNG. Vary tiny pitch/timbre changes, not the meaning of
the cue. Do not put loop points into one-shot files. Continuous ambience or
motors need a separate loop API when they are implemented.

## Event rules

- Play only from a confirmed gameplay result. Mining needs `digInto > 0`; a
  failed attempt is a hard-hit only when the aimed material exceeds tool power.
  Full inventory, filtered material, and empty air remain quiet.
- Coalesce a brush stroke or a machine row into an audible gesture, never a
  sound per cell or per particle. Fire, water, acid, and lava are sampled from
  meaningful nearby events or sparse ambience, not from every simulation tick.
- Creature sounds are local to the listener. Spawn/despawn is silent by default;
  call, attack, hit and death are separate moments. Limit the number of same
  species calls audible at once, especially flying swarms at night.
- In multiplayer, the local player hears local UI and action feedback. World
  events should play once when an authoritative result is observed, with
  distance attenuation. Never play on both prediction and replication.
- The registry contains a per-cue repeat interval and the current Windows
  backend caps simultaneous voices at 12. World sounds use distance and pan.
  Priority and master/SFX/ambience volume controls are still needed; damage
  and boss tells should win voices in a saturated scene.

## Inventory

Each name below is the `SFX_` enum suffix and its lowercase WAV basename unless
explicitly noted. Related materials share a cue family; the list avoids a WAV
for each of the hundreds of cell/material states.

| Cue | Trigger / intention |
| --- | --- |
| `MINE` **prototype** (`mining_signature.wav`) | A successful foreground bite; low crunchy feedback. |
| `MINE_TOO_HARD` **prototype** (`mining_too_hard.wav`) | Aimed solid exceeds tool strength; bright metallic refusal. |
| `PLAYER_DAMAGE` **prototype** (`damage_signature.wav`) | Local HP falls by at least 10 in one tick; the full damage cue. |
| `PLAYER_DAMAGE_SMALL` (`player_damage_small.wav`) | Local HP falls by 1–9 in one tick; a short, quieter falling hit with light grit. |
| `MACHINE` **prototype** (`machine_signature.wav`) | Successful device placement for now; later reuse only if a machine action suits it. |

The normal mining cue uses a 200 ms repeat floor. The fastest miner previously
repeated roughly every 100 ms, so holding right-click now yields at most half
that audible rate; single deliberate bites still sound immediately.

### Interface and inventory

| Cues | Trigger / intention |
| --- | --- |
| `UI_SELECT`, `UI_BACK`, `UI_ERROR`, `UI_TAB` | Confirm, dismiss, rejected action, and page/tab movement. |
| `UI_INVENTORY_PICKUP`, `UI_INVENTORY_DROP` | Stack attaches to or leaves cursor, including hotbar moves. |
| `UI_CRAFT` | Recipe actually creates an item, not every menu click. |
| `UI_SAVE`, `UI_LOAD` | Successful save/load completion. |
| `UI_PAUSE`, `UI_CHAT` | Pause toggle and incoming chat notification. |

### Player and traversal

| Cues | Trigger / intention |
| --- | --- |
| `STEP_EARTH`, `STEP_STONE`, `STEP_METAL`, `STEP_WET` | Grounded distance-based footsteps by surface family; no airborne spam. |
| `PLAYER_JUMP`, `PLAYER_LAND`, `PLAYER_FALL_HURT` | Real takeoff, landing, and damaging fall. |
| `PLAYER_HEAL`, `PLAYER_DEATH`, `PLAYER_RESPAWN` | At least 10 HP restored in one tick, death once, and body returns. Slow regeneration stays silent. |
| `PLAYER_BREATH_LOW`, `PLAYER_BURN`, `PLAYER_FREEZE` | Sparse survival warnings, not one sound per damage tick. |
| `EQUIP`, `THROW` | Tool/armor switch and a successfully released throwable. |

### Mining, building, and interaction

| Cues | Trigger / intention |
| --- | --- |
| `MINE_BACKGROUND`, `HARVEST` | Background scrape and plant-only cut. |
| `PLACE_EARTH`, `PLACE_STONE`, `PLACE_METAL`, `PLACE_LIQUID` | Successful material stroke grouped by physical character. |
| `DEVICE_PICKUP` | Whole machine returned to inventory. |
| `DOOR_OPEN`, `DOOR_CLOSE`, `CHEST_OPEN`, `CHEST_CLOSE` | State change after interaction. |

### Tools and combat

| Cues | Trigger / intention |
| --- | --- |
| `TOOL_FIRE_LIGHT`, `TOOL_FIRE_HEAVY` | Committed projectile shot by weight/power family. |
| `TOOL_BEAM_START`, `TOOL_BEAM_END` | Edge of a sustained beam, not every active frame. |
| `PROJECTILE_FLY`, `PROJECTILE_WALL`, `PROJECTILE_FLESH`, `PROJECTILE_BURST` | Distinct travel, solid impact, creature impact, and burst. Travel is only for special projectiles. |
| `MELEE_SWING`, `MELEE_HIT` | Attack release and confirmed contact. |

### Creatures

`ENEMY_*` identity cues are occasional calls or movement signatures, not a
mandatory cry on spawn. Shared hit/death sounds can vary by size or pitch later.

| Cues | Trigger / intention |
| --- | --- |
| `ENEMY_MITE`, `ENEMY_MOTH`, `ENEMY_SLIME`, `ENEMY_HUSK`, `ENEMY_BAT`, `ENEMY_SPITTER` | Layer-one identities: skitter, wing, wet corrosion, husk rasp/step, bat chirp, spitter windup. |
| `ENEMY_SHAMBLER`, `ENEMY_THRESHER`, `ENEMY_CULVERIN`, `ENEMY_WISP`, `ENEMY_STOOPER`, `ENEMY_SKIRMISHER` | Layer-two identities, with their movement or attack rhythm. |
| `ENEMY_ASHHOUND`, `ENEMY_EMBERWING`, `ENEMY_SLAGMAW`, `ENEMY_CINDERLING` | Deep-layer identities, shaped by heat and mass. |
| `ENEMY_HIT`, `ENEMY_DEATH`, `ENEMY_SHOT` | Shared impact, death, and ranged attack events. |
| `BEE_BUZZ`, `COAL_BEE_BUZZ` | Tame hive creature activity, distant and sparse. |

The summoned training dummy is intentionally silent except for shared impact
when useful for testing. The Censer limbs and Effigy arms/crown are parts of
their bosses, not separate enemies requiring a cue for every entity slot.

### Bosses and progression

| Cues | Trigger / intention |
| --- | --- |
| `BOSS_BROOD_CALL`, `BOSS_CENSER_CALL`, `BOSS_EFFIGY_CALL` | Distinct summon/entrance tells. |
| `BOSS_WIDOW_WEB`, `BOSS_WIDOW_LEAP` | The two Widow threats players must read. |
| `BOSS_PHASE`, `BOSS_DEFEAT` | Clear transition and reward moment; species timbre can be layered later. |

### Machines, signals, and crafting stations

| Cues | Trigger / intention |
| --- | --- |
| `CLOCK_PULSE`, `SENSOR_TRIP`, `SPARK`, `CIRCUIT_SWITCH` | Audible physical or logical state changes. Combinators and watchers share these. |
| `PIPE_TRANSFER`, `PLACER_CYCLE`, `MINER_CYCLE`, `SPOUT_CYCLE`, `DRAIN_CYCLE` | Successful transport or work cycle, rate-limited by device and distance. |
| `STATION_CRAFT` | Workbench/anvil/chemical/assembly/forge fabrication result. |
| `HIVE_RELEASE` | Hive launches bees; no continuous spawn ticking. |

Torches, heat lamps, beds, chests, pedestals, and the rocket use interaction,
world, or progression cues; they do not all need individual idle noises. A
thermocouple, block watcher, clock, pulse button and the three combinators
can speak through `SENSOR_TRIP`, `CLOCK_PULSE`, `CIRCUIT_SWITCH`, and `SPARK`.

### World and ambience

| Cues | Trigger / intention |
| --- | --- |
| `FIRE_IGNITE`, `FIRE_CRACKLE`, `FIRE_EXTINGUISH` | Ignition/extinction transitions and short sizzling crackles while flame is nearby; embers crackle more sparsely. |
| `WATER_SPLASH`, `LAVA_BUBBLE`, `ACID_HISS`, `ICE_CRACK` | Large or player-relevant physical interactions, never individual cells. |
| `EXPLOSION` | Blast event, sized by gain or later variants. |
| `CAVE_DRIP`, `SURFACE_WIND` | Sparse location ambience. Night should sound exposed, not simply louder. |

### Rocket and ending

| Cues | Trigger / intention |
| --- | --- |
| `ROCKET_READY`, `ROCKET_COUNTDOWN`, `ROCKET_ABORT` | Checklist completion, each countdown beat, and cancellation. |
| `ROCKET_IGNITE`, `ROCKET_ASCENT`, `VICTORY` | Ignition transient, sustained lift (future loop), and win screen sting. |

## Implementation path

The native Windows player loads `res/sfx/<filename>` and falls back to an
embedded copy of **all 107** sounds, so the standalone executable is audible.
It overlaps up to 12 cues. Gameplay hooks now cover local movement and health,
mining, placement, interaction, crafting, nearby enemy calls and combat,
machines, ambience, bosses, and rocket launch. Some specialized cues are
auditionable but await their precise gameplay events; do not wire a cue to an
unrelated event just to make it fire. The browser uses the same embedded cues
through Web Audio, with the same cooldowns, distance fade, and 12-voice limit.
It starts muted on a first visit; the pause-menu sound toggle remembers the
browser preference for later visits.

For the listening pass in a normal Windows build, press **F6** for the next cue,
**F7** to replay it, and **F8** for the previous cue. Its name appears on screen.
Then test natural triggers: hold right click to mine, build and use devices,
fight at night and in caves, and listen for competing sounds around a machine
base. The generator is `python tools/generate_sfx_library.py`; approved
changes to the four signatures come from `python tools/generate_signature_sfx.py`.
Run `python tools/embed_sfx.py` after changing any sound so the standalone
Windows build carries the new audio.
