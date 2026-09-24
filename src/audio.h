#pragma once

/* One-shot cue catalog. The final field is the minimum interval between
   repeated plays of the same cue, in milliseconds. Missing files are silent. */
#define SOUND_CUES(X) \
    X(SFX_MINE, "mining_signature.wav", 200) \
    X(SFX_MINE_TOO_HARD, "mining_too_hard.wav", 220) \
    X(SFX_PLAYER_DAMAGE, "damage_signature.wav", 180) \
    X(SFX_MACHINE, "machine_signature.wav", 420) \
    X(SFX_UI_SELECT, "ui_select.wav", 70) \
    X(SFX_UI_BACK, "ui_back.wav", 70) \
    X(SFX_UI_ERROR, "ui_error.wav", 180) \
    X(SFX_UI_TAB, "ui_tab.wav", 100) \
    X(SFX_UI_INVENTORY_PICKUP, "ui_inventory_pickup.wav", 80) \
    X(SFX_UI_INVENTORY_DROP, "ui_inventory_drop.wav", 80) \
    X(SFX_UI_CRAFT, "ui_craft.wav", 180) \
    X(SFX_UI_SAVE, "ui_save.wav", 500) \
    X(SFX_UI_LOAD, "ui_load.wav", 500) \
    X(SFX_UI_PAUSE, "ui_pause.wav", 150) \
    X(SFX_UI_CHAT, "ui_chat.wav", 300) \
    X(SFX_STEP_EARTH, "step_earth.wav", 210) \
    X(SFX_STEP_STONE, "step_stone.wav", 210) \
    X(SFX_STEP_METAL, "step_metal.wav", 210) \
    X(SFX_STEP_WET, "step_wet.wav", 210) \
    X(SFX_PLAYER_JUMP, "player_jump.wav", 140) \
    X(SFX_PLAYER_LAND, "player_land.wav", 140) \
    X(SFX_PLAYER_FALL_HURT, "player_fall_hurt.wav", 450) \
    X(SFX_PLAYER_HEAL, "player_heal.wav", 300) \
    X(SFX_PLAYER_DEATH, "player_death.wav", 1000) \
    X(SFX_PLAYER_RESPAWN, "player_respawn.wav", 1000) \
    X(SFX_PLAYER_BREATH_LOW, "player_breath_low.wav", 1200) \
    X(SFX_PLAYER_BURN, "player_burn.wav", 700) \
    X(SFX_PLAYER_FREEZE, "player_freeze.wav", 700) \
    X(SFX_EQUIP, "equip.wav", 120) \
    X(SFX_THROW, "throw.wav", 140) \
    X(SFX_MINE_BACKGROUND, "mine_background.wav", 90) \
    X(SFX_HARVEST, "harvest.wav", 90) \
    X(SFX_PLACE_EARTH, "place_earth.wav", 90) \
    X(SFX_PLACE_STONE, "place_stone.wav", 90) \
    X(SFX_PLACE_METAL, "place_metal.wav", 90) \
    X(SFX_PLACE_LIQUID, "place_liquid.wav", 110) \
    X(SFX_DEVICE_PICKUP, "device_pickup.wav", 120) \
    X(SFX_DOOR_OPEN, "door_open.wav", 160) \
    X(SFX_DOOR_CLOSE, "door_close.wav", 160) \
    X(SFX_CHEST_OPEN, "chest_open.wav", 160) \
    X(SFX_CHEST_CLOSE, "chest_close.wav", 160) \
    X(SFX_TOOL_FIRE_LIGHT, "tool_fire_light.wav", 90) \
    X(SFX_TOOL_FIRE_HEAVY, "tool_fire_heavy.wav", 140) \
    X(SFX_TOOL_BEAM_START, "tool_beam_start.wav", 200) \
    X(SFX_TOOL_BEAM_END, "tool_beam_end.wav", 200) \
    X(SFX_PROJECTILE_FLY, "projectile_fly.wav", 180) \
    X(SFX_PROJECTILE_WALL, "projectile_wall.wav", 100) \
    X(SFX_PROJECTILE_FLESH, "projectile_flesh.wav", 100) \
    X(SFX_PROJECTILE_BURST, "projectile_burst.wav", 160) \
    X(SFX_MELEE_SWING, "melee_swing.wav", 120) \
    X(SFX_MELEE_HIT, "melee_hit.wav", 120) \
    X(SFX_ENEMY_MITE, "enemy_mite.wav", 700) \
    X(SFX_ENEMY_MOTH, "enemy_moth.wav", 700) \
    X(SFX_ENEMY_SLIME, "enemy_slime.wav", 700) \
    X(SFX_ENEMY_HUSK, "enemy_husk.wav", 700) \
    X(SFX_ENEMY_BAT, "enemy_bat.wav", 700) \
    X(SFX_ENEMY_SPITTER, "enemy_spitter.wav", 700) \
    X(SFX_ENEMY_SHAMBLER, "enemy_shambler.wav", 700) \
    X(SFX_ENEMY_THRESHER, "enemy_thresher.wav", 700) \
    X(SFX_ENEMY_CULVERIN, "enemy_culverin.wav", 700) \
    X(SFX_ENEMY_WISP, "enemy_wisp.wav", 700) \
    X(SFX_ENEMY_STOOPER, "enemy_stooper.wav", 700) \
    X(SFX_ENEMY_SKIRMISHER, "enemy_skirmisher.wav", 700) \
    X(SFX_ENEMY_ASHHOUND, "enemy_ashhound.wav", 700) \
    X(SFX_ENEMY_EMBERWING, "enemy_emberwing.wav", 700) \
    X(SFX_ENEMY_SLAGMAW, "enemy_slagmaw.wav", 700) \
    X(SFX_ENEMY_CINDERLING, "enemy_cinderling.wav", 700) \
    X(SFX_ENEMY_HIT, "enemy_hit.wav", 100) \
    X(SFX_ENEMY_DEATH, "enemy_death.wav", 180) \
    X(SFX_ENEMY_SHOT, "enemy_shot.wav", 170) \
    X(SFX_BEE_BUZZ, "bee_buzz.wav", 800) \
    X(SFX_COAL_BEE_BUZZ, "coal_bee_buzz.wav", 800) \
    X(SFX_BOSS_BROOD_CALL, "boss_brood_call.wav", 1200) \
    X(SFX_BOSS_WIDOW_WEB, "boss_widow_web.wav", 500) \
    X(SFX_BOSS_WIDOW_LEAP, "boss_widow_leap.wav", 500) \
    X(SFX_BOSS_CENSER_CALL, "boss_censer_call.wav", 1200) \
    X(SFX_BOSS_EFFIGY_CALL, "boss_effigy_call.wav", 1200) \
    X(SFX_BOSS_PHASE, "boss_phase.wav", 900) \
    X(SFX_BOSS_DEFEAT, "boss_defeat.wav", 1600) \
    X(SFX_CLOCK_PULSE, "clock_pulse.wav", 240) \
    X(SFX_SENSOR_TRIP, "sensor_trip.wav", 240) \
    X(SFX_SPARK, "spark.wav", 120) \
    X(SFX_CIRCUIT_SWITCH, "circuit_switch.wav", 180) \
    X(SFX_PIPE_TRANSFER, "pipe_transfer.wav", 300) \
    X(SFX_PLACER_CYCLE, "placer_cycle.wav", 300) \
    X(SFX_MINER_CYCLE, "miner_cycle.wav", 300) \
    X(SFX_SPOUT_CYCLE, "spout_cycle.wav", 350) \
    X(SFX_DRAIN_CYCLE, "drain_cycle.wav", 350) \
    X(SFX_STATION_CRAFT, "station_craft.wav", 250) \
    X(SFX_HIVE_RELEASE, "hive_release.wav", 400) \
    X(SFX_FIRE_IGNITE, "fire_ignite.wav", 300) \
    X(SFX_FIRE_CRACKLE, "fire_crackle.wav", 150) \
    X(SFX_FIRE_EXTINGUISH, "fire_extinguish.wav", 500) \
    X(SFX_WATER_SPLASH, "water_splash.wav", 250) \
    X(SFX_LAVA_BUBBLE, "lava_bubble.wav", 600) \
    X(SFX_ACID_HISS, "acid_hiss.wav", 500) \
    X(SFX_ICE_CRACK, "ice_crack.wav", 400) \
    X(SFX_EXPLOSION, "explosion.wav", 200) \
    X(SFX_CAVE_DRIP, "cave_drip.wav", 1000) \
    X(SFX_SURFACE_WIND, "surface_wind.wav", 1500) \
    X(SFX_ROCKET_READY, "rocket_ready.wav", 700) \
    X(SFX_ROCKET_COUNTDOWN, "rocket_countdown.wav", 600) \
    X(SFX_ROCKET_ABORT, "rocket_abort.wav", 1000) \
    X(SFX_ROCKET_IGNITE, "rocket_ignite.wav", 1800) \
    X(SFX_ROCKET_ASCENT, "rocket_ascent.wav", 1800) \
    X(SFX_VICTORY, "victory.wav", 3000)

enum SoundId {
#define SOUND_ENUM(id, path, cooldown) id,
    SOUND_CUES(SOUND_ENUM)
#undef SOUND_ENUM
    SFX_COUNT
};

void audioInit();
void audioUpdate();
void audioShutdown();
void audioSetMuted(bool muted);
bool audioMuted();
void audioPlay(SoundId id, float gain = 1.0f);
void audioSetListener(float x, float y);
void audioPlayAt(SoundId id, float x, float y, float gain = 1.0f);
const char* audioCueName(SoundId id);
