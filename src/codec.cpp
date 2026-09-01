#include "codec.h"

/* One visitor per struct. Every field is named exactly once; adding a field to
   a struct without adding it here means it does not replicate, which is a
   visible bug rather than a silent layout mismatch. */

static void codecFlight(Blob& b, FlightSpec& f) {
    b.f32f(f.thrust); b.f32f(f.riseCap); b.intf(f.fuel); b.f32f(f.refuel);
}

static void codecTemp(Blob& b, TempSpec& t) {
    b.intf(t.heat); b.intf(t.cold);
}

void codecPlayer(Blob& b, Player& p) {
    b.f32f(p.x); b.f32f(p.y); b.f32f(p.vx); b.f32f(p.vy);
    b.boolf(p.onGround); b.boolf(p.buried); b.boolf(p.alive);
    b.intf(p.facing); b.f32f(p.walkPhase); b.intf(p.frame); b.intf(p.airFrames);
    b.boolf(p.crouching);
    codecFlight(b, p.fly); b.f32f(p.fuel); b.boolf(p.thrusting);
    b.intf(p.hp); b.f32f(p.hurt); b.intf(p.hurtFlash); b.f32f(p.fallFromY);
    b.u8f(p.feltTemp);
    codecTemp(b, p.resist);
    b.boolf(p.swimming); b.boolf(p.underwater); b.intf(p.breath);
    b.boolf(p.climbing); b.f32f(p.speedMul); b.f32f(p.lastFall);
}

void codecItemStack(Blob& b, ItemStack& s) {
    b.itemf(s.item); b.u32f(s.count); b.u16f(s.inst);
}

void codecToolInst(Blob& b, ToolInst& t) {
    if (!b.countf(TOOL_SLOTS_MAX)) return;
    for (int i = 0; i < TOOL_SLOTS_MAX; ++i) b.itemf(t.slot[i]);
    b.intf(t.cooldown); b.boolf(t.used);
    b.u16f(t.energy); b.u16f(t.energyCapacity);
    b.u8f(t.energyRecharge); b.u8f(t.shotCursor);
    codecItemStack(b, t.payload);
}

void codecInventory(Blob& b, Inventory& v) {
    if (!b.countf(INV_SLOTS) || !b.countf(EQ_COUNT)) return;
    for (int i = 0; i < INV_SLOTS; ++i) codecItemStack(b, v.slot[i]);
    for (int i = 0; i < EQ_COUNT; ++i) codecItemStack(b, v.equip[i]);
    b.intf(v.selected);
    /* Clamped on the way IN, because this is where untrusted data becomes a
       subscript. Inventory::held() is a bare slot[selected] with no check of
       its own, and it is now called for every REMOTE player once a frame to
       draw what they are holding -- so a malformed or hostile packet setting
       this out of range would be read off the end of the array sixty times a
       second. The local path could never do it: selectHotbar() clamps. */
    if (!b.storing()) v.selected = imax(0, imin(INV_SLOTS - 1, v.selected));
    if (!b.countf(DRONE_BAY_COUNT) || !b.countf(Inventory::DRONE_MODULE_SLOTS_MAX)) return;
    for (int d = 0; d < DRONE_BAY_COUNT; ++d)
        for (int i = 0; i < Inventory::DRONE_MODULE_SLOTS_MAX; ++i)
            codecItemStack(b, v.droneModule[d][i]);
    for (int d = 0; d < DRONE_BAY_COUNT; ++d) b.u8f(v.droneLevel[d]);
}

void codecEntity(Blob& b, Entity& e) {
    b.u8f(e.type);
    b.f32f(e.x); b.f32f(e.y); b.f32f(e.vx); b.f32f(e.vy);
    b.intf(e.hp); b.intf(e.facing); b.boolf(e.onGround);
    b.intf(e.touchTimer); b.intf(e.hurtFlash); b.intf(e.actTimer); b.intf(e.shotTimer);
    b.f32f(e.aimX); b.f32f(e.aimY); b.intf(e.aimHold);
    b.intf(e.phase); b.f32f(e.animPhase);
    b.intf(e.telegraph); b.boolf(e.weightless);
    b.f32f(e.prevX); b.f32f(e.prevY); b.intf(e.stuck);
    b.i16f(e.home); b.u8f(e.soot);
}

void codecPickup(Blob& b, Pickup& p) {
    b.itemf(p.item); b.i16f(p.count);
    b.f32f(p.x); b.f32f(p.y); b.f32f(p.vx); b.f32f(p.vy);
    b.i16f(p.delay); b.u16f(p.inst);
    b.boolf(p.used);
}

void codecProjectile(Blob& b, Projectile& p) {
    b.f32f(p.x); b.f32f(p.y); b.f32f(p.vx); b.f32f(p.vy); b.f32f(p.gravity);
    b.i32f(p.power); b.i32f(p.damage); b.boolf(p.hostile);
    b.i32f(p.pierce); b.i32f(p.life); b.i32f(p.blast);
    b.i16f(p.bounces); b.f32f(p.homing);
    b.u32f(p.colour); b.u8f(p.payload); b.u8f(p.effect); b.u8f(p.owner); b.boolf(p.alive);
}

void codecDevice(Blob& b, Device& d) {
    b.u8f(d.type); b.i32f(d.x); b.i32f(d.y); b.i32f(d.value);
    b.boolf(d.firing); b.boolf(d.poked); b.boolf(d.latched); b.boolf(d.enabled);
    b.i32f(d.received);
    b.u8f(d.mat); b.i32f(d.count); b.u8f(d.mat2); b.i32f(d.count2);
    b.i16f(d.pipeFrom); b.u8f(d.face);
    b.i32f(d.phase); b.i32f(d.reading); b.boolf(d.used);
}

void codecDrone(Blob& b, Drone& d) {
    b.u8f(d.type);
    b.f32f(d.x); b.f32f(d.y); b.f32f(d.vx); b.f32f(d.vy);
    b.intf(d.shotCool); b.intf(d.effectCool);
    b.f32f(d.phase); b.intf(d.burst);
}

void codecSpark(Blob& b, Spark& s) {
    b.i32f(s.x); b.i32f(s.y); b.i16f(s.dx); b.i16f(s.dy);
    b.u16f(s.pulse); b.u32f(s.stepped);
    b.i16f(s.cycleX); b.i16f(s.cycleY); b.u8f(s.cycleSteps); b.boolf(s.used);
}

void codecShedSpark(Blob& b, ShedSpark& s) {
    b.f32f(s.x); b.f32f(s.y); b.f32f(s.vx); b.f32f(s.vy);
    b.i32f(s.life); b.boolf(s.used);
}

void codecCircuitConfig(Blob& b, CircuitDeviceConfig& c) {
    b.u8f(c.signal); b.u8f(c.signalA); b.u8f(c.signalB); b.u8f(c.signalOut); b.u8f(c.op);
}

void codecCircuitWire(Blob& b, CircuitWire& w) {
    b.u8f(w.a); b.u8f(w.b); b.u8f(w.portA); b.u8f(w.portB); b.boolf(w.used);
}

void codecRoom(Blob& b, Room& r) {
    b.i32f(r.seedX); b.i32f(r.seedY);
    b.i32f(r.x0); b.i32f(r.y0); b.i32f(r.x1); b.i32f(r.y1);
    b.i32f(r.cells); b.boolf(r.used);
}

void codecTree(Blob& b, Tree& t) {
    b.u8f(t.kind); b.i32f(t.x); b.i32f(t.y);
    b.i32f(t.step); b.i32f(t.tick); b.u32f(t.salt); b.boolf(t.used);
}

void codecTorch(Blob& b, TorchFixture& t) { b.i32f(t.x); b.i32f(t.y); }

void codecOverlay(Blob& b) {
    if (!b.countf(MAX_TOOL_INST)) return;
    for (int i = 0; i < MAX_TOOL_INST; ++i) codecToolInst(b, g_toolInst[i]);
    if (!b.countf(MAX_ENTITIES)) return;
    for (int i = 0; i < MAX_ENTITIES; ++i) codecEntity(b, g_entities[i]);
    if (!b.countf(MAX_PICKUPS)) return;
    for (int i = 0; i < MAX_PICKUPS; ++i) codecPickup(b, g_pickups[i]);
    if (!b.countf(MAX_PROJ)) return;
    for (int i = 0; i < MAX_PROJ; ++i) codecProjectile(b, g_proj[i]);
    if (!b.countf(MAX_DEVICES)) return;
    for (int i = 0; i < MAX_DEVICES; ++i) codecDevice(b, g_devices[i]);
    if (!b.countf(MAX_SPARKS)) return;
    for (int i = 0; i < MAX_SPARKS; ++i) codecSpark(b, g_sparks[i]);
    if (!b.countf(MAX_SHED)) return;
    for (int i = 0; i < MAX_SHED; ++i) codecShedSpark(b, g_shed[i]);
    if (!b.countf(MAX_DEVICES)) return;
    for (int i = 0; i < MAX_DEVICES; ++i) codecCircuitConfig(b, g_circuitConfig[i]);
    if (!b.countf(MAX_CIRCUIT_WIRES)) return;
    for (int i = 0; i < MAX_CIRCUIT_WIRES; ++i) codecCircuitWire(b, g_circuitWires[i]);
    if (!b.countf(MAX_ROOMS)) return;
    for (int i = 0; i < MAX_ROOMS; ++i) codecRoom(b, g_rooms[i]);
    if (!b.countf(MAX_TREES)) return;
    for (int i = 0; i < MAX_TREES; ++i) codecTree(b, g_trees[i]);

    /* Torches are a variable-length list rather than a fixed pool. */
    if (b.storing()) {
        u32 count = (u32)torchCount(); b.u32f(count);
        TorchFixture* data = const_cast<TorchFixture*>(torchData());
        for (u32 i = 0; i < count; ++i) codecTorch(b, data[i]);
    } else {
        u32 count = 0; b.u32f(count);
        /* Each fixture is eight bytes on the wire; refuse a length which
           cannot possibly be present rather than reserving on a bad number. */
        if (!b.ok || count > 1000000u || (size_t)count * 8u > b.n - b.at) { b.ok = false; return; }
        std::vector<TorchFixture> fixtures(count);
        for (u32 i = 0; i < count; ++i) codecTorch(b, fixtures[i]);
        if (b.ok) torchLoad(fixtures.empty() ? 0 : &fixtures[0], (int)fixtures.size());
    }

    b.u32f(g_worldTime); b.u32f(g_bossesBeaten); b.u32f(g_rng);
}
