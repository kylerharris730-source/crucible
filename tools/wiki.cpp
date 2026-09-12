/* ============================================================================
   wiki.cpp -- generates the reference wiki at web/wiki/, from the game's own
   tables.

   See WIKI.md for what the wiki is and WIKI_STEPS.md for the order it is being
   built in. The short version of why this is a program rather than a folder of
   hand-written HTML: the game already holds every fact the reference half needs
   -- 116 materials, 293 items, 141 recipes, 27 creatures, 25 devices, 208
   sprites -- and a page that was TOLD a number is wrong the first time that
   number is tuned, with nothing anywhere complaining. A page that ASKS cannot
   drift. This month alone spear reach, trinket slot count and the whole
   fuel-to-coke chain moved.

   Run it through the script, which knows the link set:

       bash scripts/run_wiki.sh

   Not wired into CI on purpose -- the wiki updates when you say so, and the
   output is committed so that Pages publishes it with the next deploy. There is
   nothing Windows-specific here and nothing in the link set needs a Win32
   library, so this could move into the ubuntu Pages job later if that ever
   becomes wanted.

   --- step 1.1 -------------------------------------------------------------
   This is deliberately almost empty. It writes one placeholder page and stops.

   That is the whole point of doing it first: the riskiest assumption in the
   plan is not any page's content, it is that a tool can link the game's data
   tables at all and land a file where Pages will publish it. The link set
   snowballs -- asking item.cpp for one name drags in sprites, devices,
   entities and the world -- so "just read the tables" is not a small program,
   and finding that out after writing five page types would be expensive.
   ========================================================================== */
#include "materials.h"
#include "item.h"
#include "craft.h"
#include "entity.h"
#include "device.h"
#include "sprite.h"
#include "multiplayer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define WIKI_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define WIKI_MKDIR(p) mkdir((p), 0755)
#endif

static const char* OUT_DIR = "web/wiki";

/* Anything that goes wrong here stops the run. A wiki generator that reports
   success having written half a site is worse than one that crashes: the
   output LOOKS like a wiki, so nobody reads it sceptically, and the missing
   half is discovered by a player instead of by us. */
static void fail(const char* what, const char* detail) {
    fprintf(stderr, "wiki: %s", what);
    if (detail && detail[0]) fprintf(stderr, ": %s", detail);
    fprintf(stderr, "\n");
    exit(1);
}

/* EEXIST is success -- the directory is tracked in git, so on every run after
   the first it is already there. */
static void ensureDir(const char* path) {
    if (WIKI_MKDIR(path) != 0 && errno != EEXIST)
        fail("cannot create directory", path);
}

static FILE* openOut(const char* relative) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", OUT_DIR, relative);
    FILE* f = fopen(path, "wb");
    if (!f) fail("cannot write", path);
    return f;
}

/* Written here rather than trusted to the caller: the generator must be run
   from the repository root, because OUT_DIR is relative to it and so is every
   path in the link set. Run from tools/ it would cheerfully create
   tools/web/wiki and publish nothing. Checking for a file only the root has
   turns that into an error message instead of a mystery. */
static void requireRepoRoot() {
    FILE* f = fopen("src/materials.h", "rb");
    if (!f) fail("run me from the repository root", "src/materials.h not found");
    fclose(f);
}

int main() {
    requireRepoRoot();

    /* The tables do not populate themselves, and several of them are built at
       startup rather than being static initialisers. playerSessionsReset() is
       here for the same reason the test harnesses call it: linking the game's
       code means linking the game's globals, and the multiplayer session table
       is one of them. */
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    /* Counted rather than quoted. These numbers appear in WIKI.md and
       WIKI_STEPS.md, and printing the live values is how the documents get
       caught being out of date. */
    int stackable = 0, described = 0;
    for (int i = 1; i < ITEM_COUNT; ++i) {
        if (ITEMS[i].maxStack) ++stackable;
        if (ITEMS[i].description && ITEMS[i].description[0]) ++described;
    }

    ensureDir("web");
    ensureDir(OUT_DIR);

    FILE* f = openOut("index.html");
    fprintf(f,
        "<!doctype html>\n"
        "<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
        "<title>Cinderlift wiki</title>\n"
        "</head><body>\n"
        "<h1>Cinderlift wiki</h1>\n"
        "<p>Generated from the game's own tables. Nothing here is hand-written,\n"
        "so nothing here can disagree with the game.</p>\n"
        "<p>This is step 1.1 &mdash; the spine, with no pages on it yet. What the\n"
        "generator can already see:</p>\n"
        "<ul>\n"
        "<li>%d materials</li>\n"
        "<li>%d items, %d of them stackable, %d with written descriptions</li>\n"
        "<li>%d recipes across %d stations</li>\n"
        "<li>%d creatures</li>\n"
        "<li>%d devices</li>\n"
        "<li>%d sprites</li>\n"
        "</ul>\n"
        "</body></html>\n",
        (int)MAT_COUNT,
        (int)ITEM_COUNT, stackable, described,
        N_RECIPES, (int)STATION_COUNT,
        (int)ENT_COUNT,
        (int)DEV_COUNT,
        (int)SPR_COUNT);
    if (fclose(f) != 0) fail("failed to close", "index.html");

    printf("wiki: wrote %s/index.html\n", OUT_DIR);
    printf("      %d materials, %d items (%d stackable, %d described),\n",
           (int)MAT_COUNT, (int)ITEM_COUNT, stackable, described);
    printf("      %d recipes over %d stations, %d creatures, %d devices, %d sprites\n",
           N_RECIPES, (int)STATION_COUNT, (int)ENT_COUNT, (int)DEV_COUNT,
           (int)SPR_COUNT);
    return 0;
}
