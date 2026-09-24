#include "world.h"
#include "materials.h"
#include "item.h"
#include "sprite.h"
#include "entity.h"
#include "player.h"
#include "light.h"
#include "render.h"
#include "multiplayer.h"
#include <stdio.h>
#include <string.h>

/* Exercise the real spawner with fixed terrain and several random streams.
   Clearing creatures after arrival models repeated kills, but deliberately
   does not reset the spawn clock: rate and species mix both matter in play. */
static int failures = 0;
static const int CX = 1800;
static const u32 SEEDS[] = { 0x13579BDFu, 0x2468ACE1u, 0xDEADBEEFu };

static void check(bool ok, const char* what) {
    if (!ok) {
        if (failures < 30) fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Scene {
    int camX, camY, floorY;
};

static void solveLight(World& w, const Scene& s) {
    w.setLiveWindow(s.camX - LIGHT_MARGIN_CELLS - 40,
                    s.camY - LIGHT_MARGIN_CELLS - 40,
                    s.camX + VIEW_CELLS_W + LIGHT_MARGIN_CELLS + 40,
                    s.camY + VIEW_CELLS_H + LIGHT_MARGIN_CELLS + 40);
    lightClearDynamic();
    lightCompute(w, s.camX, s.camY);
}

static Scene buildScene(World& w, bool cave, bool floor = true) {
    w.reset(false);
    const int floorY = cave ? 3808 : 2400;
    const int playerY = floorY - 24;
    if (cave) {
        memset(w.zone, ZONE_LAYER1, sizeof(w.zone));
        /* A sealed roof keeps the cavern dark even in the middle of the day.
           Tall enough to offer several distinct local crowding regions. */
        for (int y = playerY - 480; y < playerY - 300; ++y)
            for (int x = CX - 950; x <= CX + 950; ++x)
                w.setCell(x, y, MAT_STONE);
    }
    if (floor)
        for (int y = floorY; y <= floorY + 500; ++y)
            for (int x = CX - 950; x <= CX + 950; ++x)
                w.setCell(x, y, MAT_STONE);

    g_player.reset((float)CX, (float)playerY);
    Scene s = { CX - VIEW_CELLS_W / 2,
                playerY - VIEW_CELLS_H / 2, floorY };
    solveLight(w, s);
    return s;
}

static void checkPlacement(const World& w, const Scene& s, const Entity& e,
                           bool lightFieldValid = true) {
    const EntityDef& d = ENT_DEFS[e.type];
    const int x = (int)e.x, y = (int)e.y;
    const bool outside = x + d.w <= s.camX || x >= s.camX + VIEW_CELLS_W ||
                         y + d.h <= s.camY || y >= s.camY + VIEW_CELLS_H;
    check(outside, "the entire actual spawn box is outside the view");
    const int fieldX = lightFieldValid ? g_lightAnchorX : s.camX - LIGHT_MARGIN_CELLS;
    const int fieldY = lightFieldValid ? g_lightAnchorY : s.camY - LIGHT_MARGIN_CELLS;
    check(x >= fieldX && y >= fieldY &&
          x + d.w <= fieldX + LIGHT_CELLS_W &&
          y + d.h <= fieldY + LIGHT_CELLS_H,
          "the entire actual spawn box is inside the solved light rectangle");
    const float dx = e.centreX() - g_player.centreX();
    const float dy = e.centreY() - g_player.centreY();
    const float distance2 = dx * dx + dy * dy;
    check(distance2 >= 150.0f * 150.0f,
          "the final position keeps its distance after finding ground");
    check(distance2 <= (float)(ENT_DESPAWN_DIST * ENT_DESPAWN_DIST),
          "new creatures are inside the player's despawn radius");
    for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
        const PlayerSession& session = g_playerSessions[slot];
        if (!playerPresent(session) || &session.body == &g_player) continue;
        const Player& peer = session.body;
        const int viewX = (int)peer.centreX() - VIEW_CELLS_W / 2;
        const int viewY = (int)peer.centreY() - VIEW_CELLS_H / 2;
        check(x + d.w <= viewX || x >= viewX + VIEW_CELLS_W ||
              y + d.h <= viewY || y >= viewY + VIEW_CELLS_H,
              "the actual spawn box is outside every connected player's view");
        const float peerDX = e.centreX() - peer.centreX();
        const float peerDY = e.centreY() - peer.centreY();
        check(peerDX * peerDX + peerDY * peerDY >= 150.0f * 150.0f,
              "the actual spawn keeps its distance from every connected player");
    }
    bool wet = false;
    for (int yy = y; yy < y + d.h; ++yy)
        for (int xx = x; xx < x + d.w; ++xx)
            if (MATS[w.at(xx, yy).mat].kind == KIND_LIQUID) wet = true;
    check(!solidBox(w, x, y, d.w, d.h) && !wet,
          "the actual box is clear of terrain and liquid");
    if (!d.flies) {
        check(solidBox(w, x, y + d.h, d.w, 1, SOLID_FLOOR),
              "walkers have support at their actual feet, including odd heights");
        check(y + d.h == s.floorY, "walkers stand exactly on the flat floor");
    }
    const int zone = w.zoneAt((int)e.centreX(), (int)e.centreY());
    check(!d.isBoss && !d.tame, "only ordinary enemies spawn naturally");
    if (zone == ZONE_SKY)
        check(isNight() && d.surfaceAtNight,
              "the final surface position obeys the time and species rules");
    else
        check((d.layerMask & (1 << caveLayerOf(zone))) != 0,
              "the final cave position belongs to the species' layer");
}

struct Samples {
    int total, walkers, flyers, husks, minGap;
};

static Samples sample(World& w, const Scene& s, u32 seed, int frames,
                      bool lightFieldValid = true) {
    entReset();
    g_rng = seed;
    Samples result = { 0, 0, 0, 0, frames };
    int lastFrame = -1;
    for (int frame = 0; frame < frames; ++frame) {
        entSpawnTick(w, g_player, s.camX, s.camY, lightFieldValid);
        int arrivals = 0;
        for (int i = 0; i < MAX_ENTITIES; ++i) {
            Entity& e = g_entities[i];
            if (e.type == ENT_NONE) continue;
            ++arrivals;
            ++result.total;
            if (ENT_DEFS[e.type].flies) ++result.flyers;
            else ++result.walkers;
            if (e.type == ENT_HUSK) ++result.husks;
            checkPlacement(w, s, e, lightFieldValid);
            if (lastFrame >= 0 && frame - lastFrame < result.minGap)
                result.minGap = frame - lastFrame;
            lastFrame = frame;
            e.type = ENT_NONE;
        }
        check(arrivals <= 1, "one tick never creates a burst of creatures");
    }
    return result;
}

static void checkPopulation(World& w, const Scene& s, u32 seed,
                            int expectedCap, int flyerCap) {
    entReset();
    g_rng = seed;
    bool seen[MAX_ENTITIES] = {};
    int total = 0, flyers = 0;
    for (int frame = 0; frame < 6000; ++frame) {
        entSpawnTick(w, g_player, s.camX, s.camY, true);
        total = flyers = 0;
        for (int i = 0; i < MAX_ENTITIES; ++i) {
            const Entity& e = g_entities[i];
            if (e.type == ENT_NONE) continue;
            ++total;
            if (ENT_DEFS[e.type].flies) ++flyers;
            if (!seen[i]) {
                checkPlacement(w, s, e);
                seen[i] = true;
            }
        }
        check(total <= expectedCap, "persistent population stays within its cap");
        check(flyers <= flyerCap, "persistent flying population stays within its cap");
    }
    printf("population seed %08x: %d creatures, %d flyers (cap %d)\n",
           (unsigned)seed, total, flyers, expectedCap);
    check(total == expectedCap, "valid habitat can fill its population allowance");
}

static int takeArrivals(const World& w, const Scene& s) {
    int count = 0;
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        Entity& e = g_entities[i];
        if (e.type == ENT_NONE) continue;
        checkPlacement(w, s, e);
        ++count;
        e.type = ENT_NONE;
    }
    check(count <= 1, "changing habitat cannot create a spawn burst");
    return count;
}

static void checkHabitatTransition(World& w) {
    g_worldTime = DAY_LENGTH * 3 / 4;
    const Scene surface = buildScene(w, false);
    entReset();
    g_rng = SEEDS[0];
    bool spawnedSurface = false;
    for (int frame = 0; frame < 600 && !spawnedSurface; ++frame) {
        entSpawnTick(w, g_player, surface.camX, surface.camY, true);
        spawnedSurface = takeArrivals(w, surface) > 0;
    }
    check(spawnedSurface, "the transition starts with an actual surface arrival");
    if (!spawnedSurface) return;

    /* World::reset clears terrain but never calls entReset. Rebuilding the
       scene therefore preserves both timers, just as changing the player
       whose surroundings are sampled must preserve them in multiplayer. */
    const Scene cave = buildScene(w, true);
    int firstCave = -1;
    /* Return before the 120-tick surface cooldown ends, so this actually
       measures when the surface resumes rather than when the fixture returns. */
    for (int elapsed = 1; elapsed <= 100; ++elapsed) {
        entSpawnTick(w, g_player, cave.camX, cave.camY, true);
        if (takeArrivals(w, cave) && firstCave < 0) firstCave = elapsed;
    }
    check(firstCave > 0 && firstCave <= 100,
          "a recent surface arrival does not impose its slower clock on caves");

    const Scene returnSurface = buildScene(w, false);
    int firstReturn = -1;
    for (int elapsed = 101; elapsed <= 260; ++elapsed) {
        entSpawnTick(w, g_player, returnSurface.camX, returnSurface.camY, true);
        if (takeArrivals(w, returnSurface)) {
            check(elapsed >= 120,
                  "cave arrivals cannot shorten the outstanding surface cooldown");
            if (firstReturn < 0) firstReturn = elapsed;
        }
    }
    printf("habitat transition: cave at +%d ticks, surface again at +%d ticks\n",
           firstCave, firstReturn);
    check(firstReturn >= 120 && firstReturn <= 150,
          "surface spawning resumes when its own cooldown expires");
}

int main() {
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();
    g_lightOn = true;
    World& w = g_world;

    g_worldTime = DAY_LENGTH * 3 / 4;
    const Scene surface = buildScene(w, false);
    check(isNight(), "the surface fixture is at night");
    check(lightAtWorld(CX, surface.floorY - 20) <= 40,
          "the open surface really is dark enough to spawn");
    int surfaceTotal = 0;
    for (unsigned i = 0; i < sizeof(SEEDS) / sizeof(SEEDS[0]); ++i) {
        const Samples s = sample(w, surface, SEEDS[i], 36000);
        printf("surface seed %08x: %d arrivals, %d walkers, %d flyers, %d Husks, gap %d\n",
               (unsigned)SEEDS[i], s.total, s.walkers, s.flyers, s.husks, s.minGap);
        check(s.total >= 270 && s.total <= 330,
              "surface replacements follow the faster night pace");
        check(s.minGap >= 120, "surface kills do not bypass the night cooldown");
        check(s.walkers * 100 >= s.total * 55,
              "ground enemies are a clear majority on open flat ground");
        check(s.husks * 100 >= s.total * 15,
              "Husks are a regular part of surface encounters");
        surfaceTotal += s.total;
        checkPopulation(w, surface, SEEDS[i], 6, 1);
    }

    PlayerSession& peer = g_playerSessions[1];
    peer.body.reset((float)(CX + 300), g_player.centreY());
    peer.connected = true;
    const Samples multiplayer = sample(w, surface, SEEDS[0], 18000);
    check(multiplayer.total > 50,
          "a nearby player's view still leaves valid sites on the opposite side");
    peer.connected = false;

    /* An offset camera exposes the difference between validating the probe
       and validating the place a walker actually reaches. */
    Scene offset = surface;
    offset.camX += 331;
    offset.camY += 103;
    solveLight(w, offset);
    const Samples offsetSamples = sample(w, offset, SEEDS[0], 12000);
    check(offsetSamples.total > 30, "the offset-camera placement checks are non-vacuous");

    g_worldTime = DAY_LENGTH / 4;
    solveLight(w, surface);
    const Samples day = sample(w, surface, SEEDS[0], 6000);
    check(day.total == 0, "the open surface does not spawn in daylight");

    /* A shallow underground band ends above the surface floor. A cave probe
       must not turn into a daytime surface arrival after its ground search.
       Use the documented zone-only fallback so daylight cannot mask the bug. */
    for (int cy = (surface.floorY - 64) >> CHUNK_SHIFT;
         cy < (surface.floorY - 32) >> CHUNK_SHIFT; ++cy)
        for (int cx = 0; cx < CHUNKS_X; ++cx)
            w.zone[cy * CHUNKS_X + cx] = ZONE_LAYER1;
    const Samples boundary = sample(w, surface, SEEDS[1], 12000, false);
    check(boundary.total > 50, "the zone-boundary fixture still offers valid cave sites");

    const Scene cave = buildScene(w, true);
    check(lightAtWorld(CX - 330, cave.floorY - 20) <= 40,
          "the sealed cavern is dark in daylight");
    int caveTotal = 0;
    for (unsigned i = 0; i < sizeof(SEEDS) / sizeof(SEEDS[0]); ++i) {
        const Samples s = sample(w, cave, SEEDS[i], 36000);
        printf("cave seed %08x: %d arrivals, %d walkers, %d flyers, %d Husks, gap %d\n",
               (unsigned)SEEDS[i], s.total, s.walkers, s.flyers, s.husks, s.minGap);
        check(s.total > 350 && s.total <= 450,
              "the cave retains its productive spawn pace");
        check(s.minGap >= 80 && s.minGap <= 90,
              "cave replacements keep the existing cooldown");
        check(s.husks > 10, "Husks remain available underground");
        caveTotal += s.total;
        checkPopulation(w, cave, SEEDS[i], 7, 7);
    }
    check(caveTotal * 10 > surfaceTotal * 13,
          "cave repopulation remains faster than open-night replacements");

    g_worldTime = DAY_LENGTH * 3 / 4;
    const Scene sky = buildScene(w, false, false);
    for (unsigned i = 0; i < sizeof(SEEDS) / sizeof(SEEDS[0]); ++i)
        checkPopulation(w, sky, SEEDS[i], 1, 1);

    checkHabitatTransition(w);

    if (failures) {
        fprintf(stderr, "%d spawn balance check(s) failed\n", failures);
        return 1;
    }
    puts("surface nights are slower and ground-led; caves retain their pressure");
    return 0;
}
