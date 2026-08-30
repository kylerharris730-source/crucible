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

/* --- the thumbnail ---------------------------------------------------------
   A save slot with a picture of where you were is a different object from a
   save slot with a filename. Ten identical rows reading "cinderlift3.sav" ask you
   to remember which is which; ten pictures answer it before you have finished
   looking at them.

   Small on purpose. 128x96 in RGB is 36 KB against a world that is tens of
   megabytes, so it costs nothing to store and -- much more importantly -- it is
   small enough that reading ten of them to draw the save screen is instant.
   That is the constraint that actually shaped this: the screen has to open
   without loading ten worlds, which is what savePeek is for. */
static const int SAVE_THUMB_W = 128;
static const int SAVE_THUMB_H = 96;
static const int SAVE_THUMB_BYTES = SAVE_THUMB_W * SAVE_THUMB_H * 3;

/* What a slot looks like from outside, without loading it. `when` is a unix
   time; `rgb` is SAVE_THUMB_BYTES of top-down RGB, all zero when the save
   predates thumbnails. */
struct SaveSlotInfo {
    bool used;          /* a readable cinderlift save is at this path */
    bool readable;      /* ...and this build can actually load it */
    i64  when;          /* unix seconds, 0 if unknown */
    u64  bytes;         /* file size */
    bool hasThumb;
    u8   rgb[SAVE_THUMB_W * SAVE_THUMB_H * 3];
    char note[96];      /* why it is not readable, when it is not */
};

/* Read a slot's header, timestamp and thumbnail WITHOUT decoding the world.
   Seeks past every other section using the length framing, so the cost is a few
   reads regardless of how large the file is -- see the note on SAVE_THUMB_W.

   Returns false only when there is no file; a file that exists but cannot be
   loaded comes back with `used` true and `readable` false, because "there is a
   save here and this build cannot read it" is something the screen has to be
   able to show rather than something to hide. */
bool savePeek(const char* path, SaveSlotInfo* out);

/* Both return false and leave a message in saveError() on failure. A failed
   LOAD leaves the world in whatever state it got to -- the caller should
   regenerate rather than carry on, and main.cpp does.

   `thumbRgb` is SAVE_THUMB_BYTES of top-down RGB, or null to write no
   thumbnail. Defaulted so every existing caller -- the tests, the network's
   join snapshot -- keeps writing exactly what it wrote before. */
bool saveWrite(const char* path, const World& w, const u8* thumbRgb = 0);
bool saveRead(const char* path, World& w);

const char* saveError();

/* Push the written bytes somewhere they will survive the process.

   On Windows this does nothing, because fclose() already did it: the file is
   on a disk. In the browser it is not. Emscripten's default filesystem is
   MEMFS, which is a JavaScript object -- saveWrite() succeeds, the save screen
   lists the slot with its size and thumbnail, and every byte of it disappears
   when the tab closes. That failure is worse than saving being unavailable,
   because the game tells the player their world is safe.

   The browser build therefore mounts IDBFS over the save directory, and IDBFS
   only reaches IndexedDB when it is explicitly told to. This is that
   instruction, and it has to be called after a successful write or the mount
   buys nothing. */
void savePersist();

/* The per-cell colour speckle, derived from the cell index rather than stored
   -- see the note in save.cpp for why, and for what happens when the thing
   deriving it is not actually a hash. Exposed so a test can measure it. */
u8 tintAt(u32 i);
