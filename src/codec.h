#pragma once
#include "player.h"
#include "item.h"
#include "entity.h"
#include "projectile.h"
#include "device.h"
#include "room.h"
#include "tree.h"
#include "light.h"       /* g_worldTime */
#include "multiplayer.h"
#include <vector>
#include <string.h>

/* Field-wise encoding for everything that crosses a machine boundary.
 *
 * The state packet used to be raw `sizeof()` blocks of these structs, guarded
 * by a size check. That is only safe while both peers are the same executable,
 * which the build handshake does enforce -- but it means padding, field order,
 * enum width and struct growth are all part of the wire format, and a peer
 * built by a different compiler cannot be supported without rewriting this
 * anyway. The size guard also fails silently in the direction that matters: two
 * different layouts which happen to total the same number of bytes are
 * accepted and decode to nonsense.
 *
 * Read and write are ONE function per struct, not two. Two hand-written
 * halves of a codec drift, and a codec that drifts corrupts state rather than
 * failing -- the same reasoning that made build.bat discover its sources
 * rather than list them. A `Blob` is in exactly one mode, and every visitor is
 * written as though it were storing: the reading mode fills the same
 * references it would otherwise have read.
 *
 * Scalars are little-endian; floats are IEEE-754 bit patterns. Both are
 * assumptions about the CPU rather than about the compiler, which is the whole
 * point of the change. */
struct Blob {
    std::vector<u8>* out;      /* non-null when storing */
    const u8*        in;
    size_t           n, at;
    bool             ok;

    /* Storing. */
    explicit Blob(std::vector<u8>& sink) : out(&sink), in(0), n(0), at(0), ok(true) {}
    /* Loading. */
    Blob(const u8* data, size_t len) : out(0), in(data), n(len), at(0), ok(true) {}

    bool storing() const { return out != 0; }
    bool done() const { return storing() || at == n; }

    void u8f(u8& v) {
        if (out) { out->push_back(v); return; }
        if (at + 1 > n) { ok = false; v = 0; return; }
        v = in[at++];
    }
    void u16f(u16& v) {
        u8 a = (u8)v, b = (u8)(v >> 8);
        u8f(a); u8f(b);
        if (!out) v = (u16)((u16)a | ((u16)b << 8));
    }
    void u32f(u32& v) {
        u16 a = (u16)v, b = (u16)(v >> 16);
        u16f(a); u16f(b);
        if (!out) v = (u32)a | ((u32)b << 16);
    }
    void i16f(i16& v) { u16 t = (u16)v; u16f(t); if (!out) v = (i16)t; }
    void i32f(i32& v) { u32 t = (u32)v; u32f(t); if (!out) v = (i32)t; }
    /* `int` is 32 bits on every target this game builds for, but pinning the
       wire width means a 64-bit build would not silently change the format. */
    void intf(int& v)  { i32 t = (i32)v; i32f(t); if (!out) v = (int)t; }
    void boolf(bool& v) { u8 t = v ? 1u : 0u; u8f(t); if (!out) v = t != 0; }
    void f32f(float& v) {
        u32 bits = 0;
        if (out) memcpy(&bits, &v, 4);
        u32f(bits);
        if (!out) memcpy(&v, &bits, 4);
    }
    /* Enums are stored at a fixed width rather than whatever the compiler
       chose. ItemId and MatId are append-only, so the numbers themselves are
       already a stable part of the format. */
    void itemf(ItemId& v) { u16 t = (u16)v; u16f(t); if (!out) v = (ItemId)t; }

    /* A fixed-size pool whose length is part of the build. The count is still
       written, so a peer or save with a different pool size is rejected
       explicitly instead of being read off the end. */
    bool countf(int expected) {
        u32 t = (u32)expected; u32f(t);
        if (!out && (int)t != expected) ok = false;
        return ok;
    }
};

void codecPlayer(Blob&, Player&);
void codecInventory(Blob&, Inventory&);
void codecItemStack(Blob&, ItemStack&);
void codecToolInst(Blob&, ToolInst&);
void codecEntity(Blob&, Entity&);
void codecPickup(Blob&, Pickup&);
void codecProjectile(Blob&, Projectile&);
void codecDevice(Blob&, Device&);
void codecDrone(Blob&, Drone&);
void codecSpark(Blob&, Spark&);
void codecShedSpark(Blob&, ShedSpark&);
void codecCircuitConfig(Blob&, CircuitDeviceConfig&);
void codecCircuitWire(Blob&, CircuitWire&);
void codecRoom(Blob&, Room&);
void codecTree(Blob&, Tree&);
void codecTorch(Blob&, TorchFixture&);

/* The whole shared overlay: pools, world clock and progression. Used by the
   network state packet, and available to any other transport that needs the
   same live state. Players are NOT here; they are per-slot and the caller
   decides which ones to send. */
void codecOverlay(Blob&);
