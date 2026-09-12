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
    { "materials",  "Materials", true  },
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

    /* The generated half of the styling: the icon sheet's size, which depends
       on how many items there are, and one class per item. */
    fputs("<link rel=\"stylesheet\" href=\"", f);
    writeRoot(f, depth);
    fputs("icons.css\">\n", f);

    /* The site's own icon, one directory above the wiki. */
    fputs("<link rel=\"icon\" href=\"", f);
    writeRoot(f, depth + 1);
    fputs("favicon.ico\">\n", f);

    /* Sorting and filtering. Deferred so it never blocks rendering: every table
       ships complete in the HTML, so the script is an enhancement and the page
       is a full reference without it. */
    fputs("<script src=\"", f);
    writeRoot(f, depth);
    fputs("wiki.js\" defer></script>\n", f);

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

/* --- the icon sheet ------------------------------------------------------

   Every stackable item's art, laid out in one grid, written as a PAM and turned
   into a PNG by scripts/ppm_to_png.py -- the same no-image-library-in-the-build
   pipeline tools/cover.cpp already uses.

   dropArt() is what makes this a loop rather than a project. It hands back a
   14x14 canvas for EVERY stackable item, materials and non-materials alike: for
   anything with a sprite of its own it IS the inventory icon's canvas, and for
   a raw material it is a box-resampled copy of renderMaterialIcon's 21x21. One
   call, no special cases -- and tests/dropped_items.cpp already proves all 290
   canvases are non-blank and shaped rather than solid squares. So the art on
   the wiki is the art in the game's own hands, by construction.

   ALPHA, not a matte. Art stores 0 for "nothing here", and flattening that onto
   a background colour makes every icon a rectangle of that colour -- the exact
   complaint that started the dropped-item work: "i dont want them to all be big
   squares i want the sprite". A matte would look right on a panel and wrong on
   every row highlight and hover state it is ever drawn over.

   One sheet rather than 290 files: one request instead of 290, and CSS
   background-position picks the cell.

   These offsets are DERIVED, which is why icons.css is generated while wiki.css
   is hand-written. The split is the rule from WIKI.md, not an inconsistency. */
static const int ICON_COLS = 16;

/* Doubled everywhere, because .icon renders 14x14 art at 28x28. Scaling by an
   exact integer keeps every cell a crisp square block, in a game whose whole
   look is that the cells are visible; 1.7x would be mush. */
static const int ICON_ZOOM = 2;

static void writeIcons(int* cellOfItem, int& sheetW, int& sheetH, int& count) {
    int n = 0;
    for (int i = ITEM_NONE + 1; i < ITEM_COUNT; ++i)
        cellOfItem[i] = ITEMS[i].maxStack ? n++ : -1;
    count = n;
    if (n == 0) fail("no stackable items", "the item table looks empty");

    const int cols = ICON_COLS;
    const int rows = (n + cols - 1) / cols;
    sheetW = cols * SPR_W;
    sheetH = rows * SPR_H;

    unsigned char* rgba = (unsigned char*)calloc((size_t)sheetW * sheetH, 4);
    if (!rgba) fail("out of memory", "icon sheet");

    int blank = 0;
    const char* firstBlank = NULL;
    for (int i = ITEM_NONE + 1; i < ITEM_COUNT; ++i) {
        const int cell = cellOfItem[i];
        if (cell < 0) continue;
        const u32* art = dropArt((u16)i);
        if (!art) {
            if (!firstBlank) firstBlank = ITEMS[i].name;
            ++blank;
            continue;
        }
        const int ox = (cell % cols) * SPR_W;
        const int oy = (cell / cols) * SPR_H;
        int drawn = 0;
        for (int y = 0; y < SPR_H; ++y) {
            for (int x = 0; x < SPR_W; ++x) {
                const u32 px = art[y * SPR_W + x];
                if (!px) continue;      /* 0 is transparent, and stays that way */
                ++drawn;
                unsigned char* p =
                    rgba + (((size_t)(oy + y) * sheetW) + (ox + x)) * 4;
                p[0] = (unsigned char)((px >> 16) & 0xFF);
                p[1] = (unsigned char)((px >> 8) & 0xFF);
                p[2] = (unsigned char)(px & 0xFF);
                p[3] = 255;
            }
        }
        if (!drawn) {
            if (!firstBlank) firstBlank = ITEMS[i].name;
            ++blank;
        }
    }
    /* An invisible icon is the one failure a reader cannot work around, because
       they cannot see there is anything to work around. tests/dropped_items.cpp
       guards the same property in game; this refuses to publish. */
    if (blank) {
        char detail[256];
        snprintf(detail, sizeof(detail),
                 "%d item(s) have no visible art, starting with %s",
                 blank, firstBlank ? firstBlank : "?");
        fail("refusing to write a sheet with blank cells", detail);
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/icons.pam", OUT_DIR);
    FILE* f = fopen(path, "wb");
    if (!f) fail("cannot write", path);
    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\n"
               "TUPLTYPE RGB_ALPHA\nENDHDR\n", sheetW, sheetH);
    if (fwrite(rgba, 4, (size_t)sheetW * sheetH, f) != (size_t)sheetW * sheetH)
        fail("short write", path);
    if (fclose(f) != 0) fail("failed to close", path);
    free(rgba);

    /* icons.css: the base rule -- which needs the sheet's pixel size and so
       cannot live in the hand-written stylesheet -- and one class per item. */
    snprintf(path, sizeof(path), "%s/icons.css", OUT_DIR);
    f = fopen(path, "wb");
    if (!f) fail("cannot write", path);
    fputs("/* GENERATED by tools/wiki.cpp -- do not edit.\n"
          "   One class per item, addressing a cell of icons.png.\n"
          "   wiki.css is the hand-written half; this half is derived. */\n", f);
    fprintf(f, ".icon {\n"
               "    background-image: url(\"icons.png\");\n"
               "    background-size: %dpx %dpx;\n"
               "}\n", sheetW * ICON_ZOOM, sheetH * ICON_ZOOM);
    for (int i = ITEM_NONE + 1; i < ITEM_COUNT; ++i) {
        const int cell = cellOfItem[i];
        if (cell < 0) continue;
        fprintf(f, ".i%d { background-position: -%dpx -%dpx; }\n", i,
                (cell % cols) * SPR_W * ICON_ZOOM,
                (cell / cols) * SPR_H * ICON_ZOOM);
    }
    if (fclose(f) != 0) fail("failed to close", path);
}

/* --- the material index --------------------------------------------------

   WIKI.md settles the shape: ONE table with every row in it, sortable by any
   column, because sorting the whole set is the single thing a table is good for
   and pagination destroys it. 116 rows is nothing for a browser.

   Only columns a reader might sort or compare BY belong here. Detail goes on
   the detail page in 2.1; an index that shows everything is a detail page with
   bad typography. */

static const char* KIND_LABELS[] = {
    "Empty", "Solid", "Powder", "Liquid", "Gas"
};

/* Temperatures in MATS[] are STORED units, offset by TEMP_OFFSET -- see the
   encoding note at the top of materials.h. Printing the raw byte would put
   "175" on the page for something that ignites at 135 C, which is the house
   rule in WIKI.md ("numbers carry units, always") being broken by exactly one
   subtraction. 0 is the disabled sentinel in every temperature column, and it
   means "never", not "at -40 C".

   A cell for a threshold a material does not have is left EMPTY rather than
   filled with a dash or a zero: the sort treats absent as absent, so sorting by
   ignition point answers "what burns, coldest first" instead of burying the
   answer under ninety materials that do not burn at all. */
static void writeTemp(FILE* f, u8 stored) {
    if (!stored) {                 /* the "no such threshold" sentinel */
        fputs("<td class=\"num\" data-sort=\"\"></td>\n", f);
        return;
    }
    const int c = (int)stored - TEMP_OFFSET;
    fprintf(f, "<td class=\"num\" data-sort=\"%d\">%d&nbsp;&deg;C</td>\n", c, c);
}

static void writeMaterialIndex(const int* cellOfItem) {
    ensureDir("web/wiki/materials");

    FILE* f = pageOpen("materials/index.html", 1, "Materials", "materials");
    fputs("<h1>Materials</h1>\n", f);
    fprintf(f, "<p class=\"lede\">Every one of the %d substances in the world,\n"
               "with the numbers that decide how each behaves. Click a column to\n"
               "sort by it.</p>\n", (int)MAT_COUNT - 1);

    fputs("<div class=\"filterbar\">\n", f);
    fputs("<input type=\"search\" placeholder=\"Filter by name or kind…\" "
          "aria-label=\"Filter materials\">\n", f);
    /* Chips for the four real kinds. KIND_EMPTY is not one of them: air is not
       a material a reader looks up. */
    for (int k = KIND_STATIC; k <= KIND_GAS; ++k)
        fprintf(f, "<button class=\"chip\" data-kind=\"%s\">%s</button>\n",
                KIND_LABELS[k], KIND_LABELS[k]);
    fprintf(f, "<span class=\"count\">%d of %d</span>\n",
            (int)MAT_COUNT - 1, (int)MAT_COUNT - 1);
    fputs("</div>\n", f);

    fputs("<div class=\"tablewrap\">\n<table class=\"index\">\n", f);
    fputs("<thead><tr>\n"
          "<th class=\"sortable\">Material</th>\n"
          "<th class=\"sortable\">Kind</th>\n"
          "<th class=\"sortable num\" title=\"Heavier sinks through lighter\">"
          "Density</th>\n"
          "<th class=\"sortable num\" title=\"How readily heat crosses into it, "
          "0 to 255\">Conducts</th>\n"
          "<th class=\"sortable num\" title=\"At or above this it catches fire\">"
          "Ignites</th>\n"
          /* Not "Boils" and "Freezes". One column serves every material, and
             the same field that boils water melts stone into lava and cooks
             sand into glass; the cold one freezes water and condenses steam.
             Naming either after the case that happens to be commonest puts a
             plainly wrong word on two thirds of the rows. */
          "<th class=\"sortable num\" title=\"At or above this it becomes "
          "something else -- boiling, melting or cooking\">Melts or boils</th>\n"
          "<th class=\"sortable num\" title=\"Below this it becomes something "
          "else -- freezing, setting or condensing\">Freezes or sets</th>\n"
          "</tr></thead>\n<tbody>\n", f);

    for (int i = 1; i < MAT_COUNT; ++i) {
        const MatInfo& m = MATS[i];
        const char* kind = (m.kind <= KIND_GAS) ? KIND_LABELS[m.kind] : "?";

        fputs("<tr data-kind=\"", f);
        escapeTo(f, kind);
        fputs("\" data-search=\"", f);
        escapeTo(f, m.name);
        fputc(' ', f);
        escapeTo(f, kind);
        fputs("\">\n", f);

        /* Materials share the item id space -- a stack of stone IS ITEM(MAT_STONE)
           -- so a material's icon is simply its own id's cell. Anything the game
           will not let you carry has no cell, and gets no icon rather than a
           broken one. */
        fputs("<td>", f);
        if (cellOfItem[i] >= 0)
            fprintf(f, "<span class=\"icon i%d\"></span>", i);
        escapeTo(f, m.name);
        fputs("</td>\n", f);

        fputs("<td class=\"dim\">", f);
        escapeTo(f, kind);
        fputs("</td>\n", f);

        fprintf(f, "<td class=\"num\">%d</td>\n", (int)m.density);
        fprintf(f, "<td class=\"num\">%d</td>\n", (int)m.heatCond);
        writeTemp(f, m.igniteTemp);
        writeTemp(f, m.boilTemp);
        writeTemp(f, m.coolTemp);
        fputs("</tr>\n", f);
    }

    fputs("</tbody>\n</table>\n</div>\n", f);

    fputs("<p class=\"lede\">Density is relative: a heavier material sinks\n"
          "through a lighter one, and a gas rises through anything denser than\n"
          "itself. Conductivity is how readily heat crosses INTO a material,\n"
          "which is why an iron wall cooks what is behind it and a refractory\n"
          "one does not.</p>\n", f);

    pageClose(f, 1);
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

    static int cellOfItem[ITEM_COUNT];
    int sheetW = 0, sheetH = 0, iconCount = 0;
    writeIcons(cellOfItem, sheetW, sheetH, iconCount);

    writeMaterialIndex(cellOfItem);

    FILE* f = pageOpen("index.html", 0, "Home", "");
    fputs("<h1>The Cinderlift wiki</h1>\n", f);
    fputs("<p class=\"lede\">A reference for a game about digging, heat and\n"
          "leaving. Generated from the game&rsquo;s own tables, so nothing here\n"
          "can disagree with what the game actually does.</p>\n", f);
    fputs("<p>Step 1.4 &mdash; the spine, a template, and every item&rsquo;s own\n"
          "art. What the generator can already see:</p>\n", f);
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

    /* A strip of real icons, so the sheet is verifiable by looking at the page
       rather than by opening the PNG and counting. It goes away when the
       material index lands in 1.5 and has thousands of them. */
    fputs("<h2>Every icon is the game&rsquo;s own art</h2>\n", f);
    fputs("<p>Drawn by the same code that draws them in your hands:</p>\n", f);
    fputs("<p>\n", f);
    for (int i = ITEM_NONE + 1, shown = 0; i < ITEM_COUNT && shown < 48; ++i) {
        if (cellOfItem[i] < 0) continue;
        fprintf(f, "<span class=\"icon i%d\" title=\"", i);
        escapeTo(f, ITEMS[i].name);
        fputs("\"></span>\n", f);
        ++shown;
    }
    fputs("</p>\n", f);
    pageClose(f, 0);

    printf("wiki: wrote %s/index.html\n", OUT_DIR);
    printf("      icons.pam %dx%d, %d cells in %d columns\n",
           sheetW, sheetH, iconCount, ICON_COLS);
    printf("      built from %s, version %s\n", WIKI_BUILD_ID, WIKI_VERSION);
    printf("      %d materials, %d items (%d stackable, %d described),\n",
           (int)MAT_COUNT, (int)ITEM_COUNT, stackable, described);
    printf("      %d recipes over %d stations, %d creatures, %d devices, %d sprites\n",
           N_RECIPES, (int)STATION_COUNT, (int)ENT_COUNT, (int)DEV_COUNT,
           (int)SPR_COUNT);
    return 0;
}
