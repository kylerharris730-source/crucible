#pragma once
#include "world.h"

/* --- the explored map ------------------------------------------------------

   What the player has SEEN, kept separately from what exists. Terraria's map is
   the model: you reveal it by walking, it persists, and it is the only thing in
   the game that answers "where have I been".

   --- resolution, and why not per cell ---

   The world is 4096 x 9216 = 37.7M cells. A per-cell map would be a second copy
   of the world at one byte each -- 37 MB of save, and an image no screen can
   show without scaling it down anyway.

   So one map pixel per 8x8 cells: 512 x 1152 entries, 576 KB. That is the
   resolution where the map is still legible about the things you navigate by --
   a 30-cell cave passage is four pixels tall, a 14-cell machine is two, an ore
   vein is a visible speck -- and where the whole world fits on one screen at
   1:1 with room to spare horizontally.

   Chunks (32x32) were the other candidate and are too coarse: at 128 x 288 the
   entire underground is a smudge and a tunnel is less than a pixel.

   --- one array, not two ---

   0 means UNEXPLORED, which is why materials cannot use it -- and MAT_EMPTY is
   0, so "explored but empty" needs somewhere else to live. It gets MAP_AIR,
   above every possible MatId. A separate explored bitmap was the alternative
   and it is two arrays that must agree about the same fact. */

static const int MAP_SHIFT = 3;                  /* 8x8 cells per map pixel */
static const int MAP_CELL  = 1 << MAP_SHIFT;
static const int MAP_W     = SIM_W >> MAP_SHIFT;
static const int MAP_H     = SIM_H >> MAP_SHIFT;

/* Explored, and nothing there. Distinct from 0 (never seen) because "I have
   been down that tunnel and it is empty" is the single most useful thing a map
   can tell you, and it is exactly what a material id cannot express. */
static const u8 MAP_UNSEEN = 0;
static const u8 MAP_AIR    = 255;

extern u8 g_map[MAP_W * MAP_H];

void mapClear();

/* Reveal what the camera can see. Called every frame; internally it refreshes
   only a slice of the visible area per call, because rescanning the whole view
   at 64 cells per map pixel is 196k reads a frame for a picture that changes
   when you walk. See the note in mapReveal. */
void mapReveal(const World& w, int camX, int camY);

/* Colour for one map pixel, already resolved through the material LUT.
   Returns false for unexplored, so the caller can draw its own "fog" rather
   than being handed a colour that means "nothing". */
bool mapColour(int mx, int my, u32* out);

/* How much of the world has been revealed, as a count of map pixels. Cheap
   enough to call for a readout; O(MAP_W * MAP_H). */
int  mapSeenCount();
