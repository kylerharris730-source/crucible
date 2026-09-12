/* ============================================================================
   wiki.cpp -- generates the reference wiki at web/wiki/, from the game's own
   tables.

   See WIKI.md for what the wiki is, its house style, and every page type with
   its intent. WIKI_STEPS.md is the order it is being built in and where
   progress is tracked.

   The short version of why this is a program rather than a folder of
   hand-written HTML: the game already holds every fact the reference half needs
   -- 116 materials, 293 items, 141 recipes, 27 creatures, 25 devices, 208
   sprites -- and a page that was TOLD a number is wrong the first time that
   number is tuned, with nothing anywhere complaining. A page that ASKS cannot
   drift. This month alone spear reach, trinket slot count and the whole
   fuel-to-coke chain moved.

   Run it through the script, which knows the link set:

       bash scripts/run_wiki.sh

   Not wired into CI on purpose -- the wiki updates when you say so, and the
   output is committed so Pages publishes it with the next deploy. Nothing here
   is Windows-specific and the link set needs no Win32 library, so this could
   move into the ubuntu Pages job later if that ever becomes wanted.
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

/* Which commit this was built from, passed in by run_wiki.sh exactly as
   build_web.sh passes CINDERLIFT_BUILD_ID to the game. It goes in the footer of
   every page, which is what makes "is the wiki stale?" a question with an
   answer: the output is committed rather than regenerated on push, so without a
   stamp the only way to tell would be to regenerate and diff. */
#ifndef WIKI_BUILD_ID
#define WIKI_BUILD_ID "development"
#endif
#ifndef WIKI_VERSION
#define WIKI_VERSION "unknown"
#endif

static const char* OUT_DIR = "web/wiki";

/* Anything that goes wrong stops the run. A generator that reports success
   having written half a site is worse than one that crashes: the output LOOKS
   like a wiki, so nobody reads it sceptically, and the missing half is
   discovered by a player instead of by us. */
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

/* --- the navigation -------------------------------------------------------

   One table, read by three things: the nav bar on every page, the hub's "Look
   up" column, and (later) the orphan check in tests/wiki.cpp. Three copies of
   this list would disagree within a week.

   `built` is the field that matters. Pages arrive over several steps, and a nav
   that links to the page we have not written yet is a site with dead links in
   it from the moment it goes live -- so a section appears in the nav only once
   its pages exist, and the flag is flipped in the step that writes them. That
   keeps WIKI_STEPS.md's "the site never regresses" true at every commit rather
   than only at the end. */
struct Section {
    const char* slug;   /* directory under /wiki/, "" for the hub */
    const char* label;  /* what the nav calls it */
    bool built;
};

static Section SECTIONS[] = {
    { "",           "Home",      true  },
    { "guide",      "Guide",     false },
    { "materials",  "Materials", false },
    { "items",      "Items",     false },
    { "recipes",    "Recipes",   false },
    { "creatures",  "Creatures", false },
    { "devices",    "Devices",   false },
};
static const int N_SECTIONS = (int)(sizeof(SECTIONS) / sizeof(SECTIONS[0]));

/* --- writing a page ------------------------------------------------------

   `depth` is how many directories below web/wiki/ the page sits, and every
   relative link is prefixed accordingly. Absolute paths would be wrong twice
   over: the site is served from a subdirectory of the Pages domain, and opening
   a generated file straight off disk to check it is the ordinary way to look at
   one. A page that only works when served is a page nobody proofreads. */
static void writeRoot(FILE* f, int depth) {
    for (int i = 0; i < depth; ++i) fputs("../", f);
}

static void escapeTo(FILE* f, const char* s) {
    if (!s) return;
    for (; *s; ++s) {
        switch (*s) {
            case '&': fputs("&amp;", f);  break;
            case '<': fputs("&lt;", f);   break;
            case '>': fputs("&gt;", f);   break;
            case '"': fputs("&quot;", f); break;
            default:  fputc(*s, f);       break;
        }
    }
}

/* `relative` is the path under web/wiki/, `depth` its directory depth, and
   `section` the slug to mark as current in the nav ("" for the hub). */
static FILE* pageOpen(const char* relative, int depth,
                      const char* title, const char* section) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", OUT_DIR, relative);
    FILE* f = fopen(path, "wb");
    if (!f) fail("cannot write", path);

    fputs("<!doctype html>\n<html lang=\"en\">\n<head>\n", f);
    fputs("<meta charset=\"utf-8\">\n", f);
    fputs("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n", f);

    fputs("<title>", f);
    escapeTo(f, title);
    /* The site's name comes second so a tabful of wiki pages is told apart by
       the half that differs. */
    fputs(" &middot; Cinderlift wiki</title>\n", f);

    fputs("<link rel=\"stylesheet\" href=\"", f);
    writeRoot(f, depth);
    fputs("wiki.css\">\n", f);

    /* The site's own icon, one directory above the wiki. */
    fputs("<link rel=\"icon\" href=\"", f);
    writeRoot(f, depth + 1);
    fputs("favicon.ico\">\n", f);

    fputs("</head>\n<body>\n", f);

    fputs("<header class=\"top\">\n", f);
    fputs("<a class=\"brand\" href=\"", f);
    writeRoot(f, depth);
    fputs("index.html\">Cinderlift <span>wiki</span></a>\n", f);
    fputs("<nav>\n", f);
    for (int i = 0; i < N_SECTIONS; ++i) {
        if (!SECTIONS[i].built) continue;
        const bool current = strcmp(SECTIONS[i].slug, section) == 0;
        fputs(current ? "<a class=\"here\" href=\"" : "<a href=\"", f);
        writeRoot(f, depth);
        if (SECTIONS[i].slug[0]) {
            fputs(SECTIONS[i].slug, f);
            fputc('/', f);
        }
        fputs("index.html\">", f);
        escapeTo(f, SECTIONS[i].label);
        fputs("</a>\n", f);
    }
    fputs("</nav>\n</header>\n", f);

    fputs("<main>\n", f);
    return f;
}

static void pageClose(FILE* f, int depth) {
    fputs("</main>\n", f);

    fputs("<footer>\n", f);
    fputs("<p>Generated from the game&rsquo;s own tables &mdash; every number on\n"
          "this page was read out of the source, not copied into it.</p>\n", f);
    fputs("<p class=\"stamp\">Built from <code>", f);
    escapeTo(f, WIKI_BUILD_ID);
    fputs("</code> &middot; Cinderlift ", f);
    escapeTo(f, WIKI_VERSION);
    fputs(" &middot; <a href=\"", f);
    writeRoot(f, depth + 1);
    fputs("index.html\">the game</a>", f);
    fputs("</p>\n</footer>\n", f);

    fputs("</body>\n</html>\n", f);
    if (fclose(f) != 0) fail("failed to close a page", NULL);
}

/* The generator must run from the repository root: OUT_DIR is relative to it
   and so is every path in the link set. Run from tools/ it would cheerfully
   create tools/web/wiki and publish nothing, so check for a file only the root
   has and turn that into an error message instead of a mystery. */
static void requireRepoRoot() {
    FILE* f = fopen("src/materials.h", "rb");
    if (!f) fail("run me from the repository root", "src/materials.h not found");
    fclose(f);
}

int main() {
    requireRepoRoot();

    /* The tables do not populate themselves, and several are built at startup
       rather than being static initialisers. playerSessionsReset() is here for
       the same reason the test harnesses call it: linking the game's code means
       linking the game's globals, and the session table is one of them. */
    initMaterials();
    initItems();
    initSprites();
    playerSessionsReset();

    /* Counted rather than quoted. These figures appear in WIKI.md and
       WIKI_STEPS.md, and printing the live values is how those documents get
       caught going out of date. */
    int stackable = 0, described = 0;
    for (int i = 1; i < ITEM_COUNT; ++i) {
        if (ITEMS[i].maxStack) ++stackable;
        if (ITEMS[i].description && ITEMS[i].description[0]) ++described;
    }

    ensureDir("web");
    ensureDir(OUT_DIR);

    FILE* f = pageOpen("index.html", 0, "Home", "");
    fputs("<h1>The Cinderlift wiki</h1>\n", f);
    fputs("<p class=\"lede\">A reference for a game about digging, heat and\n"
          "leaving. Generated from the game&rsquo;s own tables, so nothing here\n"
          "can disagree with what the game actually does.</p>\n", f);
    fputs("<p>Step 1.2 &mdash; the spine carries a template now, and no pages\n"
          "yet. What the generator can already see:</p>\n", f);
    fprintf(f,
        "<ul>\n"
        "<li>%d materials</li>\n"
        "<li>%d items, %d of them stackable, %d with written descriptions</li>\n"
        "<li>%d recipes across %d stations</li>\n"
        "<li>%d creatures</li>\n"
        "<li>%d devices</li>\n"
        "<li>%d sprites</li>\n"
        "</ul>\n",
        (int)MAT_COUNT,
        (int)ITEM_COUNT, stackable, described,
        N_RECIPES, (int)STATION_COUNT,
        (int)ENT_COUNT,
        (int)DEV_COUNT,
        (int)SPR_COUNT);
    pageClose(f, 0);

    printf("wiki: wrote %s/index.html\n", OUT_DIR);
    printf("      built from %s, version %s\n", WIKI_BUILD_ID, WIKI_VERSION);
    printf("      %d materials, %d items (%d stackable, %d described),\n",
           (int)MAT_COUNT, (int)ITEM_COUNT, stackable, described);
    printf("      %d recipes over %d stations, %d creatures, %d devices, %d sprites\n",
           N_RECIPES, (int)STATION_COUNT, (int)ENT_COUNT, (int)DEV_COUNT,
           (int)SPR_COUNT);
    return 0;
}
