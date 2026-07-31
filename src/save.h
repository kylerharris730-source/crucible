#pragma once
#include "world.h"

/* --- saving ----------------------------------------------------------------

   A save has to survive the game changing under it, because the game changes
   several times a day. Two things break saves, they break them in completely
   different ways, and only one of them is expensive to fix.

   --- what actually breaks a save ---
   MATERIAL IDS. Every cell stores a u8 index into MatId, and materials get
   inserted in the MIDDLE of that enum all the time -- MAT_ROPE and MAT_PLATFORM
   went in ahead of MAT_DEVICE, MAT_OAK_SEED ahead of those. A save written
   before any of that would decode every cell in the world as the wrong thing:
   stone reading as ceramic, iron as clay. Silent, total, and it looks like
   corruption rather than like a version problem.

   So ids are never trusted. The file carries a table of saved id -> material
   NAME, and loading remaps by name. After that, inserting, reordering and
   renumbering materials are all free forever. It is about forty lines and it is
   the single highest-value thing in this file.

   FORMAT DRIFT is the other one, and it is answered by framing rather than by
   versioning: every section carries a tag and a length, the loader skips tags
   it does not know, and anything absent stays at its default. Adding a whole
   subsystem therefore does not invalidate old saves -- they load without it.

   --- what is deliberately NOT here ---
   Migration. There is no code anywhere that transforms old data to new
   meaning, and there should never be. That is where the effort in a save system
   really goes and almost none of it is ever worth it: "fall damage used to
   start at 26 cells" is not something a loader can fix. Additive change is
   free, genuinely breaking change refuses the file with a clear message, and
   nothing in between pretends.

   --- size ---
   The world alone is 12.6 M cells. Stored raw that is 50 MB of Cell plus 12.6
   of temperature and 12.6 of background, which is not a thing to write on every
   save. Each plane is run-length encoded separately instead of the struct being
   encoded as a whole, which matters enormously: interleaved, one random byte
   per cell defeats the compression of the three beside it.

   See saveReport() for where the bytes actually go -- it is meant to be watched
   as the game grows. */

/* Bumped only when the FRAMING changes -- the header, the section format, the
   way the material table is written. Adding a section does not touch it, and
   neither does adding a material. If this ever has to move, old saves are
   refused rather than guessed at. */
static const u32 SAVE_VERSION = 1;

struct SaveStat {
    const char* name;
    u64 bytes;
};

/* Where the last save or load put its bytes, section by section, biggest
   first. Valid until the next call. */
int  saveStatCount();
const SaveStat* saveStats();
u64  saveTotalBytes();

/* Both return false and leave a message in saveError() on failure. A failed
   LOAD leaves the world in whatever state it got to -- the caller should
   regenerate rather than carry on, and main.cpp does. */
bool saveWrite(const char* path, const World& w);
bool saveRead(const char* path, World& w);

const char* saveError();
