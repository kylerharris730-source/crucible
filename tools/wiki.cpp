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
    { "guide",      "Guide",     true  },
    { "materials",  "Materials", true  },
    { "items",      "Items",     true  },
    { "recipes",    "Recipes",   true  },
    { "creatures",  "Creatures", true  },
    { "devices",    "Devices",   true  },
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

/* --- what was written ----------------------------------------------------

   Every page pageOpen creates, remembered so the sitemap can be generated from
   what actually exists rather than maintained by hand. web/sitemap.xml lists
   the one page the site used to have and says in its own comment to keep the
   URLs updated by hand -- which is fine for one page and is precisely the kind
   of promise that is broken by page four hundred.

   A reference site that cannot be found by search is a reference site nobody
   reads, so this is part of publishing rather than a nicety. */
static const int MAX_PAGES = 2048;
struct WrittenPage {
    char path[160];
    char title[96];
    char section[24];
};
static WrittenPage g_pages[MAX_PAGES];
static int  g_pageCount = 0;

static void rememberPage(const char* relative, const char* title,
                         const char* section) {
    if (g_pageCount >= MAX_PAGES)
        fail("too many pages for the sitemap", "raise MAX_PAGES");
    WrittenPage& p = g_pages[g_pageCount];
    snprintf(p.path, sizeof(p.path), "%s", relative);
    snprintf(p.title, sizeof(p.title), "%s", title ? title : "");
    snprintf(p.section, sizeof(p.section), "%s", section ? section : "");
    ++g_pageCount;
}

/* `relative` is the path under web/wiki/, `depth` its directory depth, and
   `section` the slug to mark as current in the nav ("" for the hub). */
static FILE* pageOpen(const char* relative, int depth,
                      const char* title, const char* section) {
    rememberPage(relative, title, section);
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
    /* Search sits at the end of the nav rather than in SECTIONS, because it is
       not a section: it has no index and nothing lives under it. */
    fputs(strcmp(section, "search") == 0 ? "<a class=\"here\" href=\""
                                         : "<a href=\"", f);
    writeRoot(f, depth);
    fputs("search.html\">Search</a>\n", f);

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

/* --- slugs ---------------------------------------------------------------

   "Coke Gas" becomes "coke-gas.html". Lowercase, spaces and punctuation to
   hyphens, and nothing else -- a URL that survives being pasted into chat and
   read out loud.

   Collisions are checked rather than hoped for: two materials whose names
   differ only by punctuation would silently overwrite one another's page, and
   the reader would find the wrong one with nothing anywhere complaining. */
static void slugify(char* out, size_t cap, const char* name) {
    size_t n = 0;
    bool lastHyphen = true;          /* true so a leading hyphen is skipped */
    for (const char* p = name; *p && n + 1 < cap; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum) { out[n++] = c; lastHyphen = false; }
        else if (!lastHyphen) { out[n++] = '-'; lastHyphen = true; }
    }
    while (n && out[n - 1] == '-') --n;
    out[n] = '\0';
    if (!n) fail("a name slugified to nothing", name);
}

/* --- the durability ladder ------------------------------------------------
   g_matStrength is a 0..255 scale with named rungs (see MatStrength). The
   number alone means nothing to a reader; the rung and the tool that clears it
   are the facts they came for. */
static const char* strengthWord(u8 s) {
    if (s >= STR_ABSOLUTE) return "indestructible";
    if (s >= STR_SEALED)   return "a sealed layer barrier";
    if (s >= STR_HARD)     return "very hard";
    if (s >= STR_ALLOY)    return "hard";
    if (s >= STR_METAL)    return "metal";
    if (s >= STR_ROCK)     return "rock";
    if (s >= STR_SOFT)     return "soft";
    if (s >= STR_LOOSE)    return "loose";
    if (s > STR_NOTHING)   return "barely there";
    return "nothing at all";
}

/* Can anything in the game break this, and does the answer distinguish between
   tools?

   The obvious page column here would be "the cheapest tool that clears it", and
   it was written that way first. It is WRONG, and measuring said so: every
   mining tier carries the SAME minePower (STR_HARD), deliberately and with a
   comment in item.cpp saying why -- the four tiers are a ladder of SPEED and
   REACH, and making the top one the only one that bites hard rock would
   silently re-tier every material in the game.

   So a "breaks with" column would have printed "Hand Drill or better" on every
   single page, which reads as a tier gate that does not exist and would send a
   reader hunting for the pick that finally cracks titanium. There isn't one.

   Derived rather than assumed, so if a fifth tier ever does break the tie the
   answer changes on every page at once. */
static bool minePowerIsUniform() {
    int seen = 0;
    for (int i = MAT_COUNT; i < ITEM_COUNT; ++i) {
        if (!ITEMS[i].mineRadius || !ITEMS[i].minePower) continue;
        if (!seen) seen = ITEMS[i].minePower;
        else if (ITEMS[i].minePower != seen) return false;
    }
    return true;
}

/* The weakest tool that reaches this material, for the day the ladder stops
   being uniform. ITEM_NONE when nothing can. */
static ItemId weakestToolFor(u8 strength) {
    ItemId best = ITEM_NONE;
    int bestPower = 0;
    for (int i = MAT_COUNT; i < ITEM_COUNT; ++i) {
        if (!ITEMS[i].mineRadius || !ITEMS[i].minePower) continue;
        if (ITEMS[i].minePower < (int)strength) continue;
        if (best == ITEM_NONE || ITEMS[i].minePower < bestPower) {
            best = (ItemId)i;
            bestPower = ITEMS[i].minePower;
        }
    }
    return best;
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

/* Defined with the recipe pages further down; declared here because the
   cross-links on a material page need it and materials come first. */
static void writeItemLink(FILE* f, ItemId id, const int* cellOfItem,
                          bool withIcon);

/* --- the reverse index ---------------------------------------------------

   "What is this for?" is the most-asked question on any game wiki and the one a
   forward recipe list cannot answer. RECIPES[] reads one way -- output, then
   inputs -- so a player holding a lump of tin has no way to discover that it
   makes bronze short of reading all 141 rows.

   Inverting the table is a few lines and it is what turns a pile of pages into
   a wiki. It is also flatly unmaintainable by hand: 141 recipes produce several
   hundred cross-references, every one of which moves when a recipe is retuned.

   Both directions come out of the same pass over the same table, so they cannot
   disagree with each other -- if tin is "used in" bronze, bronze is "made from"
   tin, by construction rather than by diligence. */
/* Measured, not guessed. The busiest ingredient today is Copper at 34 recipes,
   the most-made item is Coal at 2. 64 is headroom of nearly double on the one
   that matters, and the generator fails loudly rather than truncating if it is
   ever reached -- a page silently missing half its uses is worse than a build
   that stops, because nobody would notice. (The first attempt at 24 was too
   small and Iron said so immediately, which is the check working.) */
static const int MAX_REFS = 64;

struct RecipeRefs {
    int usedIn[MAX_REFS];   /* recipes consuming this id */
    int nUsedIn;
    int madeBy[MAX_REFS];   /* recipes producing it */
    int nMadeBy;
};

static RecipeRefs* g_refs = NULL;

static void buildRecipeRefs() {
    g_refs = (RecipeRefs*)calloc(ITEM_COUNT, sizeof(RecipeRefs));
    if (!g_refs) fail("out of memory", "recipe cross-reference table");

    for (int r = 0; r < N_RECIPES; ++r) {
        const Recipe& rec = RECIPES[r];

        if (rec.out != ITEM_NONE && rec.out < ITEM_COUNT) {
            RecipeRefs& to = g_refs[rec.out];
            if (to.nMadeBy >= MAX_REFS)
                fail("too many recipes make one item", ITEMS[rec.out].name);
            to.madeBy[to.nMadeBy++] = r;
        }

        for (int k = 0; k < CRAFT_MAX_IN; ++k) {
            const ItemId in = rec.in[k].item;
            if (!in || in >= ITEM_COUNT || !rec.in[k].count) continue;
            RecipeRefs& from = g_refs[in];
            /* A recipe listing the same ingredient twice would otherwise appear
               twice on its page, which reads as two different recipes. */
            bool already = false;
            for (int j = 0; j < from.nUsedIn; ++j)
                if (from.usedIn[j] == r) already = true;
            if (already) continue;
            if (from.nUsedIn >= MAX_REFS)
                fail("one item is used by too many recipes", ITEMS[in].name);
            from.usedIn[from.nUsedIn++] = r;
        }
    }
}

/* One recipe, written as a sentence: "4x Torch -- 4x Wood + 1x Coal, by hand".
   Used on both sides, so the two directions read alike. */
static void writeRecipeLine(FILE* f, int r, const int* cellOfItem,
                            bool showOutput) {
    const Recipe& rec = RECIPES[r];
    fputs("<li>", f);
    if (showOutput) {
        if (rec.outCount > 1) fprintf(f, "%d&times; ", rec.outCount);
        writeItemLink(f, rec.out, cellOfItem, true);
        fputs(" &mdash; ", f);
    }
    bool first = true;
    for (int k = 0; k < CRAFT_MAX_IN; ++k) {
        if (!rec.in[k].item || !rec.in[k].count) continue;
        if (!first) fputs(" + ", f);
        first = false;
        fprintf(f, "%d&times; ", rec.in[k].count);
        writeItemLink(f, rec.in[k].item, cellOfItem, true);
    }
    /* Where you have to be standing. The errand a greyed-out recipe sends you
       on is half the information, and it is the half a list of ingredients
       leaves out. */
    char stationSlug[128];
    slugify(stationSlug, sizeof(stationSlug), STATION_NAMES[rec.station]);
    if (rec.station == STATION_HAND) {
        fputs(" <span class=\"dim\">&mdash; <a href=\"../recipes/", f);
        fputs(stationSlug, f);
        fputs(".html\">by hand</a></span>", f);
    } else {
        fputs(" <span class=\"dim\">&mdash; at the <a href=\"../recipes/", f);
        fputs(stationSlug, f);
        fputs(".html\">", f);
        escapeTo(f, STATION_NAMES[rec.station]);
        fputs("</a></span>", f);
    }
    fputs("</li>\n", f);
}

/* The two sections that go at the bottom of every material and item page.
   Omitted when empty, like every other section -- see the note on writeMaterialPage. */
static void writeCrossLinks(FILE* f, ItemId id, const int* cellOfItem) {
    const RecipeRefs& refs = g_refs[id];

    if (refs.nMadeBy) {
        fputs("<h2>How to get it</h2>\n<ul>\n", f);
        for (int i = 0; i < refs.nMadeBy; ++i)
            writeRecipeLine(f, refs.madeBy[i], cellOfItem, false);
        fputs("</ul>\n", f);
    }

    if (refs.nUsedIn) {
        fputs("<h2>What it is for</h2>\n<ul>\n", f);
        for (int i = 0; i < refs.nUsedIn; ++i)
            writeRecipeLine(f, refs.usedIn[i], cellOfItem, true);
        fputs("</ul>\n", f);
    }

    /* What kills things and drops this. The other half of "where do I get one",
       and the half no recipe table holds. */
    {
        bool anyDrop = false;
        for (int t = 1; t < ENT_COUNT; ++t) {
            const EntityDef& e = ENT_DEFS[t];
            const bool ordinary = (e.dropItem == id && e.dropMax > 0);
            const bool rare     = (e.rareDrop == id && e.rareOneIn > 0);
            if (!ordinary && !rare) continue;
            if (!anyDrop) {
                fputs("<h2>Dropped by</h2>\n<ul>\n", f);
                anyDrop = true;
            }
            char cslug[128];
            slugify(cslug, sizeof(cslug), e.name);
            fprintf(f, "<li><a href=\"../creatures/%s.html\">", cslug);
            escapeTo(f, e.name);
            fputs("</a>", f);
            if (rare && !ordinary)
                fprintf(f, " <span class=\"dim\">&mdash; rarely, about 1 kill "
                           "in %d</span>", e.rareOneIn);
            fputs("</li>\n", f);
        }
        if (anyDrop) fputs("</ul>\n", f);
    }
}

/* --- a material's own page -----------------------------------------------

   WIKI.md fixes the section order so the page is skimmable by POSITION: a
   reader who has looked at one material page knows where the heat block is on
   every other one. Sections with nothing in them are OMITTED, never shown
   empty -- "Heat: none" on ninety pages teaches a reader to stop looking at the
   heat section, which costs them the ten pages where it mattered.

   "Used in", "Found" and the rest of the cross-links are stage 3. */

/* One step of a transition chain: "ignites at 135 °C → Fuel Fire".

   MAT_EMPTY is the case worth naming. It is a perfectly ordinary target -- fire
   burns out to nothing, and several materials simply vanish -- but it has no
   page, because air is not a substance anyone looks up. Linking it produced
   eight dead links to empty.html, which the link sweep caught and reading the
   code did not. It is written as words instead, which is also what it means. */
static void writeTransition(FILE* f, const char* when, u8 stored, u8 becomes,
                            const char* comparison) {
    if (!stored || becomes >= MAT_COUNT) return;
    const int c = (int)stored - TEMP_OFFSET;
    fprintf(f, "<div class=\"chain\"><span>%s %s %d&nbsp;&deg;C</span>"
               "<span class=\"arrow\">&rarr;</span>", when, comparison, c);
    if (becomes == MAT_EMPTY) {
        fputs("<span>nothing &mdash; it is gone</span></div>\n", f);
        return;
    }
    char slug[128];
    slugify(slug, sizeof(slug), MATS[becomes].name);
    fprintf(f, "<a href=\"%s.html\">", slug);
    escapeTo(f, MATS[becomes].name);
    fputs("</a></div>\n", f);
}

/* A reaction with a partner rather than a temperature: "touching Steam →
   becomes Coal Wax", "mixed with Tin Melt → makes Bronze Melt". Both halves
   have to be real or there is no reaction to describe. */
static void writeReaction(FILE* f, const char* verb, u8 partner,
                          const char* becomesWord, u8 result) {
    if (!partner || partner >= MAT_COUNT) return;
    if (!result || result >= MAT_COUNT) return;
    char ps[128], rs[128];
    slugify(ps, sizeof(ps), MATS[partner].name);
    slugify(rs, sizeof(rs), MATS[result].name);
    fprintf(f, "<div class=\"chain\"><span>%s <a href=\"%s.html\">", verb, ps);
    escapeTo(f, MATS[partner].name);
    fprintf(f, "</a></span><span class=\"arrow\">&rarr;</span>"
               "<span>%s <a href=\"%s.html\">", becomesWord, rs);
    escapeTo(f, MATS[result].name);
    fputs("</a></span></div>\n", f);
}

/* --- the rules that are not in any table ---------------------------------

   Almost everything on this site is derived, and where it is not, WIKI.md
   requires the exception to be a LIST rather than a loosened rule -- the same
   shape tests/item_descriptions.cpp already uses for the handful of materials
   allowed a description.

   This is that list. Each entry is a relationship between two materials that
   the game really implements but that lives in world.cpp as a special case
   rather than in a column, so no amount of reading the tables will find it.

   The fuel-to-coke chain is the whole reason this exists, and it is not a
   marginal case: it is the most consequential process in the game -- coke
   ember at 215 C is the only thing that smelts titanium and tungsten -- and
   before this the Fuel page linked only to Fuel Fire. A reader could walk
   Coke to Coke Ember by clicking and could never get from Fuel to Coke at
   all, because that step is the retort rule and a retort is not an item, not
   a recipe, and not a row.

   Keep this SHORT. A long list here means the generator is missing a column.
   The test asserts every id in it is real, so a renumbering cannot rot it
   quietly. */
struct CodeRule {
    u8 from;
    u8 to;
    const char* how;
};

static const CodeRule CODE_RULES[] = {
    { MAT_FUEL, MAT_COKE,
      "sealed away from air and flame and heated past 125&nbsp;&deg;C, two "
      "cells of fuel become one of coke and one of coke gas" },
    { MAT_FUEL, MAT_COKE_GAS,
      "the other half of coking, and it needs somewhere to go &mdash; vent it "
      "through gas sieve" },
    { MAT_COKE, MAT_FUEL,
      "coke is made by coking fuel in a sealed vessel, not by a recipe" },
    { MAT_COKE_GAS, MAT_FUEL,
      "given off when fuel is coked" },
};
static const int N_CODE_RULES =
    (int)(sizeof(CODE_RULES) / sizeof(CODE_RULES[0]));

static void writeCodeRules(FILE* f, int id) {
    bool any = false;
    for (int i = 0; i < N_CODE_RULES; ++i) {
        if (CODE_RULES[i].from != id) continue;
        if (!any) {
            fputs("<h2>Also becomes</h2>\n<ul>\n", f);
            any = true;
        }
        char slug[128];
        slugify(slug, sizeof(slug), MATS[CODE_RULES[i].to].name);
        fprintf(f, "<li><a href=\"%s.html\">", slug);
        escapeTo(f, MATS[CODE_RULES[i].to].name);
        fprintf(f, "</a> <span class=\"dim\">&mdash; %s</span></li>\n",
                CODE_RULES[i].how);
    }
    if (any) fputs("</ul>\n", f);
}

static void writeMaterialPage(int id, const int* cellOfItem) {
    const MatInfo& m = MATS[id];
    char slug[128], file[192];
    slugify(slug, sizeof(slug), m.name);
    snprintf(file, sizeof(file), "materials/%s.html", slug);

    FILE* f = pageOpen(file, 1, m.name, "materials");

    fputs("<h1>", f);
    if (cellOfItem[id] >= 0) fprintf(f, "<span class=\"icon i%d\"></span> ", id);
    escapeTo(f, m.name);
    fputs("</h1>\n", f);

    const char* kind = (m.kind <= KIND_GAS) ? KIND_LABELS[m.kind] : "?";
    fputs("<p class=\"lede\">", f);
    escapeTo(f, kind);
    /* Materials share the item id space, so a material can carry the authored
       description ITEMS[] holds for it. Quoted verbatim -- it is the text the
       player reads in game, and two wordings of one thing is how a wiki starts
       feeling untrustworthy. */
    if (ITEMS[id].description && ITEMS[id].description[0]) {
        fputs(" &mdash; ", f);
        escapeTo(f, ITEMS[id].description);
    }
    fputs("</p>\n", f);

    /* --- behaviour ---------------------------------------------------- */
    fputs("<h2>Behaviour</h2>\n<dl class=\"stats\">\n", f);
    fprintf(f, "<dt>Density</dt><dd>%d", (int)m.density);
    if (m.kind == KIND_GAS) fputs(" &mdash; rises through anything denser", f);
    fputs("</dd>\n", f);

    if (m.kind == KIND_POWDER)
        fprintf(f, "<dt>Piling</dt><dd>slides %d/255 when dry, %d/255 when "
                   "wet &mdash; higher flows more freely</dd>\n",
                (int)m.slideDry, (int)m.slideWet);
    if (m.kind == KIND_LIQUID) {
        fprintf(f, "<dt>Spread</dt><dd>up to %d cells sideways per frame</dd>\n",
                (int)m.dispersion);
        if (m.jitter)
            fprintf(f, "<dt>Viscosity</dt><dd>refuses to flow sideways %d "
                       "frames in 255</dd>\n", (int)m.jitter);
    }
    if (m.kind == KIND_GAS) {
        fprintf(f, "<dt>Spread</dt><dd>up to %d cells sideways per frame</dd>\n",
                (int)m.dispersion);
        if (m.jitter)
            fprintf(f, "<dt>Drift</dt><dd>wanders sideways %d times in 255 "
                       "instead of rising</dd>\n", (int)m.jitter);
    }
    if (m.capacity)
        fprintf(f, "<dt>Absorbs water</dt><dd>up to %d, wicking %d</dd>\n",
                (int)m.capacity, (int)m.wick);
    if (g_matLight[id])
        fprintf(f, "<dt>Gives light</dt><dd>%d</dd>\n", (int)g_matLight[id]);
    if (g_matPassable[id])
        fputs("<dt>Walk through</dt><dd>yes</dd>\n", f);
    if (g_matClimb[id])
        fputs("<dt>Climbable</dt><dd>yes</dd>\n", f);
    if (g_matPlatform[id])
        fputs("<dt>Platform</dt><dd>stand on it, jump up through it</dd>\n", f);
    if (g_matConducts[id])
        fputs("<dt>Conducts sparks</dt><dd>yes</dd>\n", f);
    if (g_matIsPlant[id])
        fputs("<dt>Grown</dt><dd>yes &mdash; a sickle cuts it</dd>\n", f);
    if (g_matIsSeed[id])
        fputs("<dt>Seed</dt><dd>germinates where it settles</dd>\n", f);
    if (g_matDecay[id])
        fprintf(f, "<dt>Decays</dt><dd>about 1 chance in %d per frame</dd>\n",
                (int)g_matDecay[id]);
    /* A fluid's strength is not hardness -- it is what a shot spends crossing
       it, which is why depth stops a bolt in a lake. Stated here because it is
       behaviour, and deliberately NOT under a Mining heading. */
    if ((m.kind == KIND_LIQUID || m.kind == KIND_GAS) &&
        g_matStrength[id] > STR_NOTHING)
        fputs("<dt>Stops shots</dt><dd>a shot spends pierce crossing it, so "
              "depth stops one</dd>\n", f);
    fputs("</dl>\n", f);

    /* --- heat ----------------------------------------------------------
       The most valuable block on the page, and the one nobody could maintain
       by hand: it is four tables and a subtraction per material. */
    const bool anyHeat = m.igniteTemp || m.boilTemp || m.coolTemp ||
                         g_matIgnitesOnContact[id] || g_matVentsFire[id] ||
                         m.quenchedBy || m.spawnTemp;
    if (anyHeat || m.heatCond) {
        fputs("<h2>Heat</h2>\n<dl class=\"stats\">\n", f);
        fprintf(f, "<dt>Conducts heat</dt><dd>%d of 255", (int)m.heatCond);
        if (m.heatCond >= 200)      fputs(" &mdash; readily", f);
        else if (m.heatCond <= 20)  fputs(" &mdash; barely; it insulates", f);
        fputs("</dd>\n", f);
        if (m.heatMassShift)
            fprintf(f, "<dt>Thermal mass</dt><dd>holds %d&times; the heat, so "
                       "it stays hot long after it stops being heated</dd>\n",
                    1 << m.heatMassShift);
        if (m.spawnTemp)
            fprintf(f, "<dt>Placed at</dt><dd>%d&nbsp;&deg;C</dd>\n",
                    (int)m.spawnTemp - TEMP_OFFSET);
        if (m.quenchedBy < MAT_COUNT && m.quenchedBy) {
            char qs[128];
            slugify(qs, sizeof(qs), MATS[m.quenchedBy].name);
            fputs("<dt>Destroyed by</dt><dd>touching <a href=\"", f);
            fputs(qs, f); fputs(".html\">", f);
            escapeTo(f, MATS[m.quenchedBy].name);
            fputs("</a></dd>\n", f);
        }
        if (g_matIgnitesOnContact[id])
            fputs("<dt>Sets fire to</dt><dd>whatever it touches</dd>\n", f);
        if (g_matVentsFire[id])
            fputs("<dt>Vents fire</dt><dd>flame passes through it</dd>\n", f);
        fputs("</dl>\n", f);

        writeTransition(f, "ignites at", m.igniteTemp, m.burnsTo, "");
        writeTransition(f, "at", m.boilTemp, m.boilsTo, "or above");
        writeTransition(f, "below", m.coolTemp, m.coolsTo, "");

        /* Reactions that are not about temperature but belong in the same
           chain, because a reader following "what does this turn into" does not
           care which table the answer came out of. All three are derivable:
           alloying, wetting and dissolving each have their own pair of tables. */
        writeReaction(f, "touching", g_matWetBy[id], "becomes", g_matWetInto[id]);
        writeReaction(f, "mixed with", g_matAlloyWith[id], "makes",
                      g_matAlloysTo[id]);
        if (g_matDissolvedBy[id] && g_matDissolvedBy[id] < MAT_COUNT) {
            char ds[128];
            slugify(ds, sizeof(ds), MATS[g_matDissolvedBy[id]].name);
            fputs("<div class=\"chain\"><span>dissolved by</span>"
                  "<span class=\"arrow\">&rarr;</span><a href=\"", f);
            fputs(ds, f);
            fputs(".html\">", f);
            escapeTo(f, MATS[g_matDissolvedBy[id]].name);
            fputs("</a></div>\n", f);
        }
        if (g_matDecay[id] && g_matDecaysTo[id] && g_matDecaysTo[id] < MAT_COUNT) {
            char ds[128];
            slugify(ds, sizeof(ds), MATS[g_matDecaysTo[id]].name);
            fprintf(f, "<div class=\"chain\"><span>decays</span>"
                       "<span class=\"arrow\">&rarr;</span>"
                       "<a href=\"%s.html\">", ds);
            escapeTo(f, MATS[g_matDecaysTo[id]].name);
            fputs("</a></div>\n", f);
        }
    }

    /* --- mining ---------------------------------------------------------
       Only for things you actually dig. A liquid or a gas carries a strength
       too, but it means "a shot spends pierce crossing this", not "bring a
       better pick" -- and a Mining heading over water would be a section
       answering a question nobody asked. */
    const u8 strength = g_matStrength[id];
    if (strength > STR_NOTHING &&
        (m.kind == KIND_STATIC || m.kind == KIND_POWDER)) {
        fputs("<h2>Mining</h2>\n<dl class=\"stats\">\n", f);
        fprintf(f, "<dt>Hardness</dt><dd>%s (%d)</dd>\n",
                strengthWord(strength), (int)strength);
        const ItemId tool = weakestToolFor(strength);
        if (tool == ITEM_NONE) {
            fputs("<dt>Breaks with</dt><dd>nothing in the game &mdash; this is "
                  "not something you dig through</dd>\n", f);
        } else if (minePowerIsUniform()) {
            /* Every tier bites equally hard, so naming one would invent a gate.
               Say what is actually true and why it is worth knowing. */
            fputs("<dt>Breaks with</dt><dd>any mining tool &mdash; the tiers "
                  "differ in speed and reach, not in what they can bite</dd>\n", f);
        } else {
            fputs("<dt>Breaks with</dt><dd>", f);
            escapeTo(f, ITEMS[tool].name);
            fputs(" or better</dd>\n", f);
        }
        if (g_matDropsAs[id] && g_matDropsAs[id] != id &&
            g_matDropsAs[id] < MAT_COUNT) {
            char ds[128];
            slugify(ds, sizeof(ds), MATS[g_matDropsAs[id]].name);
            fprintf(f, "<dt>Drops</dt><dd><a href=\"%s.html\">", ds);
            escapeTo(f, MATS[g_matDropsAs[id]].name);
            fputs("</a></dd>\n", f);
        }
        if (g_matSmeltYield[id])
            fprintf(f, "<dt>Smelts into</dt><dd>%d per cell</dd>\n",
                    (int)g_matSmeltYield[id]);
        fputs("</dl>\n", f);
    }

    writeCodeRules(f, id);
    writeCrossLinks(f, (ItemId)id, cellOfItem);

    fputs("<p class=\"n\"><a href=\"index.html\">&larr; all materials</a></p>\n", f);
    pageClose(f, 1);
}

static void writeMaterialIndex(const int* cellOfItem) {
    ensureDir("web/wiki/materials");

    /* Every material's own page, and a check that no two of them want the same
       filename. Two names differing only in punctuation would silently
       overwrite each other and leave a reader on the wrong page with nothing
       complaining -- the class of failure this whole generator exists to make
       impossible. */
    {
        static char slugs[MAT_COUNT][128];
        for (int i = 1; i < MAT_COUNT; ++i) {
            slugify(slugs[i], sizeof(slugs[i]), MATS[i].name);
            for (int j = 1; j < i; ++j) {
                if (strcmp(slugs[i], slugs[j]) == 0) {
                    char detail[256];
                    snprintf(detail, sizeof(detail), "%s and %s both want %s.html",
                             MATS[j].name, MATS[i].name, slugs[i]);
                    fail("two materials share a page filename", detail);
                }
            }
            writeMaterialPage(i, cellOfItem);
        }
    }

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
        {
            char slug[128];
            slugify(slug, sizeof(slug), m.name);
            fprintf(f, "<a href=\"%s.html\">", slug);
            escapeTo(f, m.name);
            fputs("</a>", f);
        }
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

/* --- items ---------------------------------------------------------------

   The Items section covers what is NOT a material. Ids below MAT_COUNT are
   materials -- a stack of stone IS ITEM(MAT_STONE) -- and they already have
   their own section with heat, mining and behaviour on every page. Listing
   them a second time here would give a reader two pages about stone that agree
   today and could disagree tomorrow, which is the failure this whole generator
   exists to rule out. The index says so rather than quietly being short. */

static const char* ITEM_KIND_LABELS[] = {
    "Material", "Tool", "Throwable", "Module", "Drone module", "Accessory",
    "Worn", "Mining", "Seed", "Device", "Egg", "Igniter", "Food", "Melee",
    "Component", "Spark"
};
/* If a kind is added and this list is not, every item of the new kind would be
   labelled by whatever happened to be at that index -- silently, and plausibly.
   The compiler checks instead. */
static_assert((int)(sizeof(ITEM_KIND_LABELS) / sizeof(ITEM_KIND_LABELS[0]))
              == (int)ITEMK_SPARK + 1,
              "ITEM_KIND_LABELS is out of step with enum ItemKind");

static const char* EQUIP_LABELS[] = {
    "Feet", "Back", "Trinket", "Trinket", "Head", "Body",
    "Trinket", "Trinket", "Trinket", "Trinket"
};

static const char* itemKindLabel(u8 kind) {
    const int n = (int)(sizeof(ITEM_KIND_LABELS) / sizeof(ITEM_KIND_LABELS[0]));
    return (kind < n) ? ITEM_KIND_LABELS[kind] : "?";
}

/* A worn item's slot, in the words the equipment screen uses. The six trinket
   slots are interchangeable, so they are all just "Trinket" -- naming one
   would suggest a distinction the game does not make. */
static const char* equipLabel(u8 slot) {
    const int n = (int)(sizeof(EQUIP_LABELS) / sizeof(EQUIP_LABELS[0]));
    return (slot < n && slot < EQ_COUNT) ? EQUIP_LABELS[slot] : NULL;
}

static void writeItemPage(int id, const int* cellOfItem) {
    const ItemDef& it = ITEMS[id];
    char slug[128], file[192];
    slugify(slug, sizeof(slug), it.name);
    snprintf(file, sizeof(file), "items/%s.html", slug);

    FILE* f = pageOpen(file, 1, it.name, "items");

    fputs("<h1>", f);
    if (cellOfItem[id] >= 0) fprintf(f, "<span class=\"icon i%d\"></span> ", id);
    escapeTo(f, it.name);
    fputs("</h1>\n", f);

    /* The authored description, VERBATIM. It is the text the player reads in
       game; a paraphrase here is a second wording of one object, which is how a
       wiki starts feeling untrustworthy. */
    fputs("<p class=\"lede\">", f);
    escapeTo(f, itemKindLabel(it.kind));
    if (it.description && it.description[0]) {
        fputs(" &mdash; ", f);
        escapeTo(f, it.description);
    }
    fputs("</p>\n", f);

    fputs("<h2>What it does</h2>\n<dl class=\"stats\">\n", f);

    if (it.maxStack > 1)
        fprintf(f, "<dt>Stacks to</dt><dd>%u</dd>\n", (unsigned)it.maxStack);

    if (it.kind == ITEMK_MINING) {
        fprintf(f, "<dt>Radius</dt><dd>%d cells</dd>\n", (int)it.mineRadius);
        fprintf(f, "<dt>Bite</dt><dd>%u cells per swing</dd>\n",
                (unsigned)it.mineBite);
        if (it.mineCooldown)
            fprintf(f, "<dt>Cooldown</dt><dd>%d frames (%.2f s)</dd>\n",
                    (int)it.mineCooldown, (double)it.mineCooldown / 60.0);
        if (it.minePlantsOnly)
            fputs("<dt>Cuts</dt><dd>only what grew &mdash; it will not touch "
                  "rock</dd>\n", f);
    }

    if (it.kind == ITEMK_TOOL) {
        fprintf(f, "<dt>Module slots</dt><dd>%d</dd>\n", (int)it.toolSlots);
        fprintf(f, "<dt>Base delay</dt><dd>%d frames between shots</dd>\n",
                (int)it.baseDelay);
        if (it.energyCapacity)
            fprintf(f, "<dt>Charge</dt><dd>holds %u, recovers %d per frame</dd>\n",
                    (unsigned)it.energyCapacity, (int)it.energyRecharge);
        fputs("<dt>Damage</dt><dd>none of its own &mdash; what a tool does "
              "lives on the modules you put in it</dd>\n", f);
    }

    if (it.damage)
        fprintf(f, "<dt>Damage</dt><dd>%d</dd>\n", it.damage);
    if (it.pierce)
        fprintf(f, "<dt>Pierce</dt><dd>%d cells before it is spent</dd>\n",
                (int)it.pierce);
    if (it.blast)
        fprintf(f, "<dt>Blast</dt><dd>%d cells</dd>\n", (int)it.blast);
    if (it.power)
        fprintf(f, "<dt>Breaks</dt><dd>up to %s terrain</dd>\n",
                strengthWord(it.power));
    if (it.shotHoming > 0.0f)
        fputs("<dt>Homing</dt><dd>steers toward what it is aimed at</dd>\n", f);
    if (it.energyCost)
        fprintf(f, "<dt>Charge per shot</dt><dd>%u</dd>\n",
                (unsigned)it.energyCost);
    if (it.addDelay)
        fprintf(f, "<dt>Delay</dt><dd>%+d frames</dd>\n", (int)it.addDelay);

    if (it.heal)
        fprintf(f, "<dt>Restores</dt><dd>%d health</dd>\n", (int)it.heal);

    /* Worn gear. The largest-never-summed rule is counter-intuitive and costs
       players real material when they stack two cheap trinkets expecting them
       to add up, so it is said on every page that has a bonus rather than
       tucked into a tutorial nobody has read yet. */
    const char* slotName = (it.kind == ITEMK_WORN || it.kind == ITEMK_ACCESSORY)
                         ? equipLabel(it.equipSlot) : NULL;
    if (slotName) {
        fputs("<dt>Worn on</dt><dd>", f);
        escapeTo(f, slotName);
        fputs("</dd>\n", f);
    }
    bool anyBonus = false;
    if (it.reachBonus) {
        fprintf(f, "<dt>Reach</dt><dd>+%d cells</dd>\n", (int)it.reachBonus);
        anyBonus = true;
    }
    if (it.speedPct) {
        fprintf(f, "<dt>Ground speed</dt><dd>+%d%%</dd>\n", (int)it.speedPct);
        anyBonus = true;
    }
    if (it.heatResist)
        fprintf(f, "<dt>Heat protection</dt><dd>%d&nbsp;&deg;C</dd>\n",
                (int)it.heatResist);
    if (it.coldResist)
        fprintf(f, "<dt>Cold protection</dt><dd>%d&nbsp;&deg;C</dd>\n",
                (int)it.coldResist);
    if (it.armour)
        fprintf(f, "<dt>Armour</dt><dd>%d off every hit &mdash; helmet and suit "
                   "DO add together</dd>\n", (int)it.armour);
    if (it.fly.thrust > 0.0f)
        fputs("<dt>Flight</dt><dd>yes</dd>\n", f);

    if (it.kind == ITEMK_EGG && it.summons < ENT_COUNT) {
        fputs("<dt>Releases</dt><dd>", f);
        escapeTo(f, ENT_DEFS[it.summons].name);
        fputs("</dd>\n", f);
    }
    if (it.kind == ITEMK_DEVICE && it.deviceType < DEV_COUNT)
        fputs("<dt>Places</dt><dd>a machine &mdash; see Devices</dd>\n", f);

    fputs("</dl>\n", f);

    /* Said on every page carrying a bonus rather than left to a tutorial
       nobody has read yet: the rule is counter-intuitive and costs real
       material when a player stacks two cheap pieces expecting them to add.
       Worded without naming a slot -- boots are not trinkets, and the first
       version of this sentence told a boots page it was about trinkets. */
    if (anyBonus)
        fputs("<p class=\"n\">Reach and speed bonuses are <strong>not</strong> "
              "added up: the one that counts is the largest single bonus you "
              "are wearing. Two cheap pieces never beat one good one.</p>\n", f);

    writeCrossLinks(f, (ItemId)id, cellOfItem);

    fputs("<p class=\"n\"><a href=\"index.html\">&larr; all items</a></p>\n", f);
    pageClose(f, 1);
}

static void writeItems(const int* cellOfItem) {
    ensureDir("web/wiki/items");

    static char slugs[ITEM_COUNT][128];
    int listed = 0;
    for (int i = MAT_COUNT; i < ITEM_COUNT; ++i) {
        if (!ITEMS[i].maxStack) continue;       /* not a thing you can hold */
        slugify(slugs[i], sizeof(slugs[i]), ITEMS[i].name);
        for (int j = MAT_COUNT; j < i; ++j) {
            if (!ITEMS[j].maxStack) continue;
            if (strcmp(slugs[i], slugs[j]) == 0) {
                char detail[256];
                snprintf(detail, sizeof(detail), "%s and %s both want %s.html",
                         ITEMS[j].name, ITEMS[i].name, slugs[i]);
                fail("two items share a page filename", detail);
            }
        }
        writeItemPage(i, cellOfItem);
        ++listed;
    }

    FILE* f = pageOpen("items/index.html", 1, "Items", "items");
    fputs("<h1>Items</h1>\n", f);
    fprintf(f, "<p class=\"lede\">The %d things you can carry that are not raw\n"
               "world substances. Dug-up materials have their own section &mdash;\n"
               "see <a href=\"../materials/index.html\">Materials</a>.</p>\n",
            listed);

    fputs("<div class=\"filterbar\">\n", f);
    fputs("<input type=\"search\" placeholder=\"Filter by name or kind…\" "
          "aria-label=\"Filter items\">\n", f);
    /* One chip per kind that anything actually has, so the bar never offers a
       filter that returns nothing. */
    for (int k = 0; k <= (int)ITEMK_SPARK; ++k) {
        if (k == ITEMK_MATERIAL) continue;
        int n = 0;
        for (int i = MAT_COUNT; i < ITEM_COUNT; ++i)
            if (ITEMS[i].maxStack && ITEMS[i].kind == k) ++n;
        if (!n) continue;
        fprintf(f, "<button class=\"chip\" data-kind=\"%s\">%s</button>\n",
                ITEM_KIND_LABELS[k], ITEM_KIND_LABELS[k]);
    }
    fprintf(f, "<span class=\"count\">%d of %d</span>\n", listed, listed);
    fputs("</div>\n", f);

    fputs("<div class=\"tablewrap\">\n<table class=\"index\">\n", f);
    fputs("<thead><tr>\n"
          "<th class=\"sortable\">Item</th>\n"
          "<th class=\"sortable\">Kind</th>\n"
          "<th class=\"sortable num\">Stack</th>\n"
          "<th class=\"sortable num\">Damage</th>\n"
          "<th class=\"sortable\">Worn on</th>\n"
          "<th>What it is</th>\n"
          "</tr></thead>\n<tbody>\n", f);

    for (int i = MAT_COUNT; i < ITEM_COUNT; ++i) {
        const ItemDef& it = ITEMS[i];
        if (!it.maxStack) continue;
        const char* kind = itemKindLabel(it.kind);

        fputs("<tr data-kind=\"", f);
        escapeTo(f, kind);
        fputs("\" data-search=\"", f);
        escapeTo(f, it.name);
        fputc(' ', f);
        escapeTo(f, kind);
        fputs("\">\n", f);

        fputs("<td>", f);
        if (cellOfItem[i] >= 0)
            fprintf(f, "<span class=\"icon i%d\"></span>", i);
        fprintf(f, "<a href=\"%s.html\">", slugs[i]);
        escapeTo(f, it.name);
        fputs("</a></td>\n", f);

        fputs("<td class=\"dim\">", f);
        escapeTo(f, kind);
        fputs("</td>\n", f);

        if (it.maxStack > 1) fprintf(f, "<td class=\"num\">%u</td>\n",
                                     (unsigned)it.maxStack);
        else                 fputs("<td class=\"num\" data-sort=\"\"></td>\n", f);

        if (it.damage) fprintf(f, "<td class=\"num\">%d</td>\n", it.damage);
        else           fputs("<td class=\"num\" data-sort=\"\"></td>\n", f);

        const char* slotName =
            (it.kind == ITEMK_WORN || it.kind == ITEMK_ACCESSORY)
            ? equipLabel(it.equipSlot) : NULL;
        fputs("<td class=\"dim\">", f);
        if (slotName) escapeTo(f, slotName);
        fputs("</td>\n", f);

        /* The authored sentence, trimmed to keep the row one line. The full
           text is on the item's own page, verbatim. */
        fputs("<td class=\"dim\">", f);
        if (it.description && it.description[0]) {
            char brief[96];
            snprintf(brief, sizeof(brief), "%s", it.description);
            char* stop = strchr(brief, '.');
            if (stop) *stop = '\0';
            escapeTo(f, brief);
        }
        fputs("</td>\n</tr>\n", f);
    }

    fputs("</tbody>\n</table>\n</div>\n", f);
    pageClose(f, 1);
}

/* --- recipes -------------------------------------------------------------

   One page per station, and the rows in the game's own RECIPES[] order so the
   page and the in-game panel agree. A wiki that sorts them "better" than the
   game does is a wiki you cannot read alongside the game.

   Quantities are printed exactly as the table holds them and never rounded or
   tidied. A recipe asking for seven of something looks like a typo and is not
   one. */

/* Items and materials live in different directories, so a link has to know
   which. The id says: below MAT_COUNT is a material. */
static void writeItemLink(FILE* f, ItemId id, const int* cellOfItem,
                          bool withIcon) {
    if (id == ITEM_NONE || id >= ITEM_COUNT) { fputs("&mdash;", f); return; }
    const char* name = ITEMS[id].name;
    char slug[128];
    slugify(slug, sizeof(slug), name);
    if (withIcon && cellOfItem[id] >= 0)
        fprintf(f, "<span class=\"icon i%d\"></span>", id);
    fprintf(f, "<a href=\"../%s/%s.html\">",
            id < MAT_COUNT ? "materials" : "items", slug);
    escapeTo(f, name);
    fputs("</a>", f);
}

static void writeRecipes(const int* cellOfItem) {
    ensureDir("web/wiki/recipes");

    /* Every recipe must appear on exactly one station page. Counted rather
       than assumed: a station id outside the table would silently drop its
       recipes off the site altogether, and a reader cannot notice the absence
       of something they never knew existed. */
    int placed = 0;

    for (int st = 0; st < STATION_COUNT; ++st) {
        int n = 0;
        for (int r = 0; r < N_RECIPES; ++r) if (RECIPES[r].station == st) ++n;

        char file[192], slug[128];
        slugify(slug, sizeof(slug), STATION_NAMES[st]);
        snprintf(file, sizeof(file), "recipes/%s.html", slug);

        FILE* f = pageOpen(file, 1, STATION_NAMES[st], "recipes");
        fputs("<h1>", f);
        escapeTo(f, STATION_NAMES[st]);
        fputs("</h1>\n", f);

        if (st == STATION_HAND)
            fprintf(f, "<p class=\"lede\">The %d things you can make with\n"
                       "nothing but your hands. This is the whole of what is open\n"
                       "to you before you build anything.</p>\n", n);
        else
            fprintf(f, "<p class=\"lede\">%d recipes. Stand near a %s to make\n"
                       "any of them.</p>\n", n, STATION_NAMES[st]);

        fputs("<div class=\"tablewrap\">\n<table>\n", f);
        fputs("<thead><tr><th>Makes</th><th>From</th></tr></thead>\n<tbody>\n", f);

        for (int r = 0; r < N_RECIPES; ++r) {
            const Recipe& rec = RECIPES[r];
            if (rec.station != st) continue;
            ++placed;

            fputs("<tr><td>", f);
            if (rec.outCount > 1) fprintf(f, "%d&times; ", rec.outCount);
            writeItemLink(f, rec.out, cellOfItem, true);
            fputs("</td><td>", f);
            bool first = true;
            for (int k = 0; k < CRAFT_MAX_IN; ++k) {
                if (!rec.in[k].item || !rec.in[k].count) continue;
                if (!first) fputs(" + ", f);
                first = false;
                fprintf(f, "%d&times; ", rec.in[k].count);
                writeItemLink(f, rec.in[k].item, cellOfItem, true);
            }
            /* A recipe with no inputs would be free material out of nowhere. */
            if (first) fputs("<span class=\"dim\">nothing</span>", f);
            fputs("</td></tr>\n", f);
        }

        fputs("</tbody>\n</table>\n</div>\n", f);
        fputs("<p class=\"n\"><a href=\"index.html\">&larr; all stations</a></p>\n", f);
        pageClose(f, 1);
    }

    if (placed != N_RECIPES) {
        char detail[160];
        snprintf(detail, sizeof(detail), "%d of %d recipes reached a page",
                 placed, N_RECIPES);
        fail("some recipes belong to no station page", detail);
    }

    FILE* f = pageOpen("recipes/index.html", 1, "Recipes", "recipes");
    fputs("<h1>Recipes</h1>\n", f);
    fprintf(f, "<p class=\"lede\">%d recipes across %d stations. Each station is\n"
               "a thing you build and put down, and it unlocks everything on its\n"
               "page.</p>\n", N_RECIPES, (int)STATION_COUNT);
    fputs("<div class=\"tablewrap\">\n<table>\n", f);
    fputs("<thead><tr><th>Station</th><th class=\"num\">Recipes</th>"
          "</tr></thead>\n<tbody>\n", f);
    for (int st = 0; st < STATION_COUNT; ++st) {
        int n = 0;
        for (int r = 0; r < N_RECIPES; ++r) if (RECIPES[r].station == st) ++n;
        char slug[128];
        slugify(slug, sizeof(slug), STATION_NAMES[st]);
        fprintf(f, "<tr><td><a href=\"%s.html\">", slug);
        escapeTo(f, STATION_NAMES[st]);
        fprintf(f, "</a></td><td class=\"num\">%d</td></tr>\n", n);
    }
    fputs("</tbody>\n</table>\n</div>\n", f);
    pageClose(f, 1);
}

/* --- creatures -----------------------------------------------------------

   Grouped by depth, because "what is this and what do I do" is nearly always
   asked by someone who has just met it at a particular depth, and the answer to
   "am I too deep" is the most useful thing the section can tell them.

   Bosses sit behind a clearly marked heading rather than being hidden or being
   sprung on someone reading about rock mites. WIKI.md asks for spoilers MARKED,
   not avoided.

   One thing this section does NOT have is art. Creature sprites are not in the
   item sheet -- they are their own canvases at six different sizes, from 22x36
   up to the Effigy's 96x112 -- and packing a second variable-cell sheet is a
   piece of work in its own right rather than a line of this step. Recorded as
   a gap in WIKI_STEPS.md instead of quietly shipped as though a creature page
   was always meant to be text. */

static void writeLayers(FILE* f, u8 mask) {
    bool first = true;
    for (int bit = 0; bit < 3; ++bit) {
        if (!(mask & (1u << bit))) continue;
        if (!first) fputs(", ", f);
        first = false;
        fprintf(f, "layer %d", bit + 1);
    }
    if (first) fputs("nowhere it spawns on its own", f);
}

static void writeCreaturePage(int type, const int* cellOfItem) {
    const EntityDef& e = ENT_DEFS[type];
    char slug[128], file[192];
    slugify(slug, sizeof(slug), e.name);
    snprintf(file, sizeof(file), "creatures/%s.html", slug);

    FILE* f = pageOpen(file, 1, e.name, "creatures");
    fputs("<h1>", f);
    escapeTo(f, e.name);
    fputs("</h1>\n", f);

    fputs("<p class=\"lede\">", f);
    if (e.isBoss) fputs("A boss. ", f);
    fputs("Found in ", f);
    writeLayers(f, e.layerMask);
    if (e.surfaceAtNight) fputs(", and on the surface after dark", f);
    fputs(".</p>\n", f);

    fputs("<h2>In a fight</h2>\n<dl class=\"stats\">\n", f);
    fprintf(f, "<dt>Health</dt><dd>%d</dd>\n", e.hp);
    if (e.touchDamage) {
        fprintf(f, "<dt>Contact damage</dt><dd>%d", e.touchDamage);
        if (e.touchCooldown)
            fprintf(f, ", at most once every %d frames (%.1f s)",
                    e.touchCooldown, (double)e.touchCooldown / 60.0);
        fputs("</dd>\n", f);
    }
    if (e.shotEvery) {
        fprintf(f, "<dt>Shoots</dt><dd>%d damage every %d frames (%.1f s)</dd>\n",
                e.shotDamage, e.shotEvery, (double)e.shotEvery / 60.0);
        if (e.standOff > 0.0f)
            fprintf(f, "<dt>Keeps its distance</dt><dd>about %d cells</dd>\n",
                    (int)e.standOff);
    }
    fprintf(f, "<dt>Speed</dt><dd>%.2f cells per frame%s</dd>\n",
            (double)e.speed, e.flies ? ", and it flies" : "");
    fprintf(f, "<dt>Size</dt><dd>%d&times;%d cells</dd>\n", e.w, e.h);
    if (e.heatTolerance)
        fprintf(f, "<dt>Survives heat to</dt><dd>%d&nbsp;&deg;C</dd>\n",
                (int)e.heatTolerance);
    if (e.indestructible)
        fputs("<dt>Invulnerable</dt><dd>yes</dd>\n", f);
    if (e.tame)
        fputs("<dt>Hostile</dt><dd>no &mdash; it will not attack you</dd>\n", f);
    fputs("</dl>\n", f);

    const bool anyDrop = (e.dropItem != ITEM_NONE && e.dropMax > 0) ||
                         (e.rareDrop != ITEM_NONE && e.rareOneIn > 0);
    if (anyDrop) {
        fputs("<h2>Drops</h2>\n<dl class=\"stats\">\n", f);
        if (e.dropItem != ITEM_NONE && e.dropMax > 0) {
            fputs("<dt>Always</dt><dd>", f);
            if (e.dropMin == e.dropMax) fprintf(f, "%d&times; ", e.dropMax);
            else fprintf(f, "%d&ndash;%d&times; ", e.dropMin, e.dropMax);
            writeItemLink(f, e.dropItem, cellOfItem, true);
            fputs("</dd>\n", f);
        }
        if (e.rareDrop != ITEM_NONE && e.rareOneIn > 0) {
            fputs("<dt>Rarely</dt><dd>", f);
            writeItemLink(f, e.rareDrop, cellOfItem, true);
            fprintf(f, " &mdash; about 1 kill in %d</dd>\n", e.rareOneIn);
        }
        fputs("</dl>\n", f);
    }

    fputs("<p class=\"n\"><a href=\"index.html\">&larr; all creatures</a></p>\n", f);
    pageClose(f, 1);
}

static void writeCreatures(const int* cellOfItem) {
    ensureDir("web/wiki/creatures");

    static char slugs[64][128];
    for (int t = 1; t < ENT_COUNT; ++t) {
        slugify(slugs[t], sizeof(slugs[t]), ENT_DEFS[t].name);
        for (int j = 1; j < t; ++j)
            if (strcmp(slugs[t], slugs[j]) == 0)
                fail("two creatures share a page filename", ENT_DEFS[t].name);
        writeCreaturePage(t, cellOfItem);
    }

    FILE* f = pageOpen("creatures/index.html", 1, "Creatures", "creatures");
    fputs("<h1>Creatures</h1>\n", f);
    fprintf(f, "<p class=\"lede\">%d of them, by how deep you have to go to meet\n"
               "one. Bosses are listed separately at the bottom.</p>\n",
            (int)ENT_COUNT - 1);

    /* Ordinary creatures first, by layer; bosses last and clearly fenced. */
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) fputs("<div class=\"spoiler\">\n", f);
        fputs(pass ? "<h2>Bosses &mdash; spoilers</h2>\n"
                   : "<h2>What lives down there</h2>\n", f);
        if (pass == 1)
            fputs("<p>Each is summoned deliberately, so none of these can "
                  "blunder into you in a tunnel.</p>\n", f);

        fputs("<div class=\"tablewrap\">\n<table class=\"index\">\n", f);
        fputs("<thead><tr>\n"
              "<th class=\"sortable\">Creature</th>\n"
              "<th class=\"sortable\">Found</th>\n"
              "<th class=\"sortable num\">Health</th>\n"
              "<th class=\"sortable num\">Contact</th>\n"
              "<th class=\"sortable num\">Shot</th>\n"
              "<th>Drops</th>\n"
              "</tr></thead>\n<tbody>\n", f);

        for (int t = 1; t < ENT_COUNT; ++t) {
            const EntityDef& e = ENT_DEFS[t];
            if ((bool)e.isBoss != (pass == 1)) continue;

            fputs("<tr data-search=\"", f);
            escapeTo(f, e.name);
            fputs("\">\n<td><a href=\"", f);
            fputs(slugs[t], f);
            fputs(".html\">", f);
            escapeTo(f, e.name);
            fputs("</a></td>\n<td class=\"dim\">", f);
            writeLayers(f, e.layerMask);
            fputs("</td>\n", f);
            fprintf(f, "<td class=\"num\">%d</td>\n", e.hp);
            if (e.touchDamage) fprintf(f, "<td class=\"num\">%d</td>\n", e.touchDamage);
            else fputs("<td class=\"num\" data-sort=\"\"></td>\n", f);
            if (e.shotEvery) fprintf(f, "<td class=\"num\">%d</td>\n", e.shotDamage);
            else fputs("<td class=\"num\" data-sort=\"\"></td>\n", f);
            fputs("<td class=\"dim\">", f);
            if (e.dropItem != ITEM_NONE && e.dropMax > 0)
                writeItemLink(f, e.dropItem, cellOfItem, false);
            fputs("</td>\n</tr>\n", f);
        }
        fputs("</tbody>\n</table>\n</div>\n", f);
        if (pass == 1) fputs("</div>\n", f);
    }
    pageClose(f, 1);
}

/* --- devices -------------------------------------------------------------

   Devices get their own section for one reason above all others, and WIKI.md
   names it: LIMITS, stated as numbers. The Heat Lamp caps at 100 C. That single
   fact cost a play session -- a retort needs 125 C, the lamp is the obvious
   thing to point at it, and it can never get there -- and it has been sitting in
   DEVS[] the whole time, one column away from being printed.

   So every device prints its adjustable range. A machine whose number stops
   somewhere is a machine that will disappoint somebody at exactly that point. */
static void writeDevices() {
    ensureDir("web/wiki/devices");

    static char slugs[64][128];
    for (int d = 1; d < DEV_COUNT; ++d) {
        const DeviceInfo& dev = DEVS[d];
        slugify(slugs[d], sizeof(slugs[d]), dev.name);
        for (int j = 1; j < d; ++j)
            if (strcmp(slugs[d], slugs[j]) == 0)
                fail("two devices share a page filename", dev.name);

        char file[192];
        snprintf(file, sizeof(file), "devices/%s.html", slugs[d]);
        FILE* f = pageOpen(file, 1, dev.name, "devices");
        fputs("<h1>", f);
        escapeTo(f, dev.name);
        fputs("</h1>\n", f);

        fprintf(f, "<p class=\"lede\">A machine, %d&times;%d cells.</p>\n",
                devTypeW((DeviceType)d), devTypeH((DeviceType)d));

        fputs("<h2>Its setting</h2>\n<dl class=\"stats\">\n", f);
        if (dev.vMin == dev.vMax) {
            /* The panel hides its -/+ for these, so the page says why rather
               than leaving a reader looking for a control that is not there. */
            fputs("<dt>Adjustable</dt><dd>no &mdash; this one has nothing to "
                  "tune</dd>\n", f);
        } else {
            fputs("<dt>", f);
            escapeTo(f, dev.valueLabel ? dev.valueLabel : "Setting");
            fputs("</dt><dd>", f);
            fprintf(f, "%d to %d", (int)dev.vMin, (int)dev.vMax);
            if (dev.valueUnit && dev.valueUnit[0]) {
                fputc(' ', f);
                escapeTo(f, dev.valueUnit);
            }
            fprintf(f, ", in steps of %d", (int)dev.vStep);
            fputs("</dd>\n", f);
            fprintf(f, "<dt>Starts at</dt><dd>%d", (int)dev.vDefault);
            if (dev.valueUnit && dev.valueUnit[0]) {
                fputc(' ', f);
                escapeTo(f, dev.valueUnit);
            }
            fputs("</dd>\n", f);
        }
        if (dev.aimable)
            fputs("<dt>Aimable</dt><dd>yes &mdash; it acts on the cells just "
                  "outside one edge, and you choose which</dd>\n", f);
        fputs("</dl>\n", f);

        /* The point of the whole page type. Said in words, not left for the
           reader to infer from a range they may not have read carefully. */
        if (dev.vMin != dev.vMax) {
            fputs("<p class=\"n\">It cannot go past ", f);
            fprintf(f, "<strong>%d", (int)dev.vMax);
            if (dev.valueUnit && dev.valueUnit[0]) {
                fputc(' ', f);
                escapeTo(f, dev.valueUnit);
            }
            fputs("</strong>. That is the limit of the machine, not of your "
                  "settings.</p>\n", f);
        }

        fputs("<p class=\"n\"><a href=\"index.html\">&larr; all devices</a></p>\n", f);
        pageClose(f, 1);
    }

    FILE* f = pageOpen("devices/index.html", 1, "Devices", "devices");
    fputs("<h1>Devices</h1>\n", f);
    fprintf(f, "<p class=\"lede\">%d machines you can place, with what each one\n"
               "can be set to &mdash; and, more usefully, where each one stops.</p>\n",
            (int)DEV_COUNT - 1);
    fputs("<div class=\"tablewrap\">\n<table class=\"index\">\n", f);
    fputs("<thead><tr>\n"
          "<th class=\"sortable\">Device</th>\n"
          "<th class=\"sortable\">Size</th>\n"
          "<th class=\"sortable\">Setting</th>\n"
          "<th class=\"sortable num\">Lowest</th>\n"
          "<th class=\"sortable num\">Highest</th>\n"
          "</tr></thead>\n<tbody>\n", f);
    for (int d = 1; d < DEV_COUNT; ++d) {
        const DeviceInfo& dev = DEVS[d];
        fputs("<tr data-search=\"", f);
        escapeTo(f, dev.name);
        fputs("\">\n<td><a href=\"", f);
        fputs(slugs[d], f);
        fputs(".html\">", f);
        escapeTo(f, dev.name);
        fputs("</a></td>\n", f);
        fprintf(f, "<td class=\"dim\">%d&times;%d</td>\n",
                devTypeW((DeviceType)d), devTypeH((DeviceType)d));
        fputs("<td class=\"dim\">", f);
        if (dev.vMin != dev.vMax && dev.valueLabel) escapeTo(f, dev.valueLabel);
        fputs("</td>\n", f);
        if (dev.vMin != dev.vMax) {
            fprintf(f, "<td class=\"num\">%d</td>\n<td class=\"num\">%d</td>\n",
                    (int)dev.vMin, (int)dev.vMax);
        } else {
            fputs("<td class=\"num\" data-sort=\"\"></td>\n"
                  "<td class=\"num\" data-sort=\"\"></td>\n", f);
        }
        fputs("</tr>\n", f);
    }
    fputs("</tbody>\n</table>\n</div>\n", f);
    pageClose(f, 1);
}

/* --- the prose half ------------------------------------------------------

   A Markdown subset, in C++, so the authored pages come out of the same
   template as the generated ones -- one header, one nav, one stylesheet. The
   alternative was a Python dependency for half the site, which is a second
   toolchain to keep working for a few hundred lines of string handling.

   Subset, not Markdown. Headings, paragraphs, lists, tables, fenced code,
   links, bold, italic and rules: what the existing documents actually use.
   Anything past that is a feature request, not a bug.

   WIKI.md had prose living in web/wiki/_src/. This table replaces that, and it
   is the better shape: CIRCUITS.md and LOGISTICS.md already exist at the
   repository root, are already read there by contributors, and copying them
   under web/wiki/ would create a second copy to keep in step -- the exact
   failure the whole generated-not-written argument is against. So sources are
   named wherever they already live, and new prose written FOR the wiki goes in
   web/wiki/_src/ without either of them being a special case. */
struct ProsePage {
    const char* source;   /* path from the repository root */
    const char* out;      /* path under web/wiki/ */
    const char* title;
    /* Tutorials are numbered and ordered; concept pages are not. The order is
       a DEPENDENCY order, which is what makes WIKI.md's rule -- "a tutorial may
       not mention anything the reader cannot yet have" -- checkable rather than
       aspirational. */
    bool tutorial;
    const char* blurb;    /* one line, for the guide index and the hub */
};

static const ProsePage PROSE[] = {
    { "web/wiki/_src/first-ten-minutes.md", "guide/first-ten-minutes.html",
      "Your first ten minutes", true,
      "Move, dig, and make the four things that matter." },
    { "web/wiki/_src/the-dark.md", "guide/the-dark.html",
      "The dark is not scenery", true,
      "Light is what stops things spawning on you." },
    { "web/wiki/_src/making-fire.md", "guide/making-fire.html",
      "Making fire", true,
      "Nothing else in the early game is hot enough." },
    { "web/wiki/_src/digging.md", "guide/digging.html",
      "Digging properly", true,
      "The mining ladder, and what it does and does not buy you." },
    { "web/wiki/_src/crafting-ladder.md", "guide/crafting-ladder.html",
      "The crafting ladder", true,
      "Hand to bench to anvil to furnace, and what each opens." },
    { "web/wiki/_src/smelting.md", "guide/smelting.html",
      "Ore into bars", true,
      "What each metal needs, against what each fuel reaches." },
    { "web/wiki/_src/coke-retort.md", "guide/coke-retort.html",
      "Coke, and the sealed retort", true,
      "The one process the game never tells you about." },
    { "web/wiki/_src/going-down.md", "guide/going-down.html",
      "Going down", true,
      "Three layers, the seals between them, and when to try." },
    { "web/wiki/_src/fighting.md", "guide/fighting.html",
      "Fighting back", true,
      "Damage lives on the module, not on the tool." },
    { "web/wiki/_src/gearing-up.md", "guide/gearing-up.html",
      "Gearing up", true,
      "Slots, trinkets, and why two cheap ones never beat one good one." },
    { "web/wiki/_src/farming.md", "guide/farming.html",
      "Farming and eating", true,
      "Seeds, soil, water, and what wheat is finally for." },
    { "web/wiki/_src/bees.md", "guide/bees.html",
      "Bees and wax", true,
      "Three wild hives, and beeswax is not wax." },
    { "CIRCUITS.md", "guide/circuits.html",
      "Circuits", true,
      "Wires that carry information rather than sparks." },
    { "LOGISTICS.md", "guide/logistics.html",
      "Item logistics", true,
      "Pipes, chests, spouts and drains." },
    { "web/wiki/_src/leaving.md", "guide/leaving.html",
      "Leaving", true,
      "The end of the game. Spoilers." },
    { "web/wiki/_src/heat.md", "guide/heat.html",
      "How heat behaves", false,
      "Conduction, thermal mass, and why the wall matters." },
};
static const int N_PROSE = (int)(sizeof(PROSE) / sizeof(PROSE[0]));

/* A markdown link to another repository document -- LOGISTICS.md points at
   CIRCUITS.md -- is a dead link once published, because the .md is not on the
   site. Rewrite it to the page that document became, and if it became no page,
   drop the link and keep the words rather than publishing a 404.

   Returns NULL when the target is not a local .md file, meaning "leave it
   alone": an ordinary http link or an already-correct relative one. */
static const char* prosePageFor(const char* url, size_t len) {
    if (len < 4) return NULL;
    if (strncmp(url + len - 3, ".md", 3) != 0) return NULL;
    for (int i = 0; i < N_PROSE; ++i) {
        const char* src = PROSE[i].source;
        /* Match on the basename, so "CIRCUITS.md" finds the entry whose source
           is "CIRCUITS.md" and "_src/bees.md" finds web/wiki/_src/bees.md. */
        const char* base = strrchr(src, '/');
        base = base ? base + 1 : src;
        const size_t blen = strlen(base);
        if (blen != len) continue;
        if (strncmp(base, url, len) == 0) {
            const char* slug = strrchr(PROSE[i].out, '/');
            return slug ? slug + 1 : PROSE[i].out;
        }
    }
    return "";   /* a .md we do not publish: keep the text, drop the link */
}

/* Inline markup. Order matters: code spans are taken first, because the whole
   point of `**` inside a code span is that it is not emphasis. */
static void renderInline(FILE* f, const char* s, const char* end) {
    while (s < end) {
        if (*s == '`') {
            const char* close = s + 1;
            while (close < end && *close != '`') ++close;
            if (close < end) {
                fputs("<code>", f);
                for (const char* p = s + 1; p < close; ++p) {
                    switch (*p) {
                        case '&': fputs("&amp;", f); break;
                        case '<': fputs("&lt;", f);  break;
                        case '>': fputs("&gt;", f);  break;
                        default:  fputc(*p, f);      break;
                    }
                }
                fputs("</code>", f);
                s = close + 1;
                continue;
            }
        }
        if (*s == '[') {
            const char* close = s + 1;
            while (close < end && *close != ']') ++close;
            if (close + 1 < end && close[1] == '(') {
                const char* urlEnd = close + 2;
                while (urlEnd < end && *urlEnd != ')') ++urlEnd;
                if (urlEnd < end) {
                    const char* url = close + 2;
                    const size_t ulen = (size_t)(urlEnd - url);
                    const char* rewritten = prosePageFor(url, ulen);
                    if (rewritten && !rewritten[0]) {
                        /* Points at a document the wiki does not publish. Keep
                           the words, drop the link: a reader losing a link is a
                           much smaller harm than a reader hitting a 404. */
                        renderInline(f, s + 1, close);
                    } else {
                        fputs("<a href=\"", f);
                        if (rewritten) fputs(rewritten, f);
                        else for (const char* p = url; p < urlEnd; ++p) fputc(*p, f);
                        fputs("\">", f);
                        /* "See [CIRCUITS.md](CIRCUITS.md)" reads fine in a repo
                           and reads as a stray filename on a published page. If
                           the author wrote the filename as the link text, show
                           the page's title instead -- unambiguous, because it
                           only fires when the two are the same string. */
                        const char* title = NULL;
                        if (rewritten && (size_t)(close - (s + 1)) == ulen &&
                            strncmp(s + 1, url, ulen) == 0) {
                            for (int pi = 0; pi < N_PROSE; ++pi) {
                                const char* slug = strrchr(PROSE[pi].out, '/');
                                slug = slug ? slug + 1 : PROSE[pi].out;
                                if (strcmp(slug, rewritten) == 0) {
                                    title = PROSE[pi].title;
                                    break;
                                }
                            }
                        }
                        if (title) escapeTo(f, title);
                        else       renderInline(f, s + 1, close);
                        fputs("</a>", f);
                    }
                    s = urlEnd + 1;
                    continue;
                }
            }
        }
        if (s + 1 < end && s[0] == '*' && s[1] == '*') {
            const char* close = s + 2;
            while (close + 1 < end && !(close[0] == '*' && close[1] == '*')) ++close;
            if (close + 1 < end) {
                fputs("<strong>", f);
                renderInline(f, s + 2, close);
                fputs("</strong>", f);
                s = close + 2;
                continue;
            }
        }
        if (*s == '*') {
            const char* close = s + 1;
            while (close < end && *close != '*') ++close;
            if (close < end) {
                fputs("<em>", f);
                renderInline(f, s + 1, close);
                fputs("</em>", f);
                s = close + 1;
                continue;
            }
        }
        switch (*s) {
            case '&': fputs("&amp;", f); break;
            case '<': fputs("&lt;", f);  break;
            case '>': fputs("&gt;", f);  break;
            default:  fputc(*s, f);      break;
        }
        ++s;
    }
}

static void renderInlineLine(FILE* f, const char* line) {
    renderInline(f, line, line + strlen(line));
}

/* One table row, split on unescaped pipes. `header` decides th against td. */
static void renderTableRow(FILE* f, char* line, bool header) {
    fputs("<tr>", f);
    char* p = line;
    if (*p == '|') ++p;
    while (*p) {
        char* cell = p;
        while (*p && *p != '|') ++p;
        char* cellEnd = p;
        if (*p) ++p;
        while (cell < cellEnd && (*cell == ' ' || *cell == '\t')) ++cell;
        while (cellEnd > cell && (cellEnd[-1] == ' ' || cellEnd[-1] == '\t')) --cellEnd;
        /* A trailing pipe leaves one empty cell, which is formatting rather
           than a column. */
        if (cell == cellEnd && !*p) break;
        fputs(header ? "<th>" : "<td>", f);
        renderInline(f, cell, cellEnd);
        fputs(header ? "</th>" : "</td>", f);
    }
    fputs("</tr>\n", f);
}

static bool isTableRule(const char* line) {
    /* |---|:--:|---| and friends: the row that says "the one above was a
       header". Nothing but pipes, dashes, colons and spaces, and at least one
       dash so a bare "| |" is not mistaken for one. */
    bool dash = false;
    for (const char* p = line; *p; ++p) {
        if (*p == '-') dash = true;
        else if (*p != '|' && *p != ':' && *p != ' ' && *p != '\t') return false;
    }
    return dash;
}

static char* readWholeFile(const char* path, long* lengthOut) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    const size_t got = fread(buf, 1, (size_t)len, f);
    buf[got] = '\0';
    fclose(f);
    if (lengthOut) *lengthOut = (long)got;
    return buf;
}

/* --- hard-wrapped continuation lines --------------------------------------

   These documents are hard-wrapped at 79 columns, so a single sentence is
   several source lines. Those are not line breaks in the output: honouring them
   would ragged every paragraph on the site to the width of somebody's editor.

   PARAGRAPHS AND LIST ITEMS BOTH. That is the whole reason this is a function
   rather than a loop inside the paragraph case, and it was found by looking at
   the rendered page rather than by reading the code: a wrapped bullet emitted
   its first line as the bullet and dropped the rest into a paragraph AFTER the
   list, so "Circuit wires have no cell footprint and cost no material. They are
   a visible" was a bullet and "wiring overlay, not an alternative kind of
   copper." was loose text below it. Read as plainly broken, and the
   construct is everywhere in these files.

   Advances `p` past every line it absorbs. */
static void consumeWrapped(FILE* f, char*& p) {
    for (;;) {
        if (!*p) return;
        char* peekNl = strchr(p, '\n');
        char save = '\0';
        if (peekNl) { save = *peekNl; *peekNl = '\0'; }

        char* body = p;
        while (*body == ' ') ++body;
        const size_t len = strlen(body);
        if (len && body[len - 1] == '\r') body[len - 1] = '\0';

        /* Anything that starts a block of its own ends the run. */
        bool ol = false;
        {
            char* d = body;
            while (*d >= '0' && *d <= '9') ++d;
            ol = (d != body) && d[0] == '.' && d[1] == ' ';
        }
        const bool stop =
            *body == '\0' || *body == '#' || *body == '|' ||
            strncmp(body, "```", 3) == 0 ||
            strncmp(body, "---", 3) == 0 ||
            ((body[0] == '-' || body[0] == '*') && body[1] == ' ') || ol;

        if (stop) { if (peekNl) *peekNl = save; return; }

        fputc(' ', f);
        renderInlineLine(f, body);
        if (peekNl) { *peekNl = save; p = peekNl + 1; }
        else        { p = body + strlen(body); }
    }
}

/* Renders `text` into an already-open page. Destroys `text` in the process --
   it is split in place. */
static void renderMarkdown(FILE* f, char* text) {
    enum { NONE, UL, OL, CODE, TABLE } block = NONE;
    char* p = text;
    bool tableHeaderDone = false;

    while (*p) {
        char* line = p;
        char* nl = strchr(p, '\n');
        if (nl) { *nl = '\0'; p = nl + 1; } else { p = line + strlen(line); }
        /* CRLF sources are ordinary on this checkout. */
        const size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[len - 1] = '\0';

        /* Inside a fence everything is literal until the closing fence. */
        if (block == CODE) {
            if (strncmp(line, "```", 3) == 0) {
                fputs("</code></pre>\n", f);
                block = NONE;
            } else {
                for (char* q = line; *q; ++q) {
                    switch (*q) {
                        case '&': fputs("&amp;", f); break;
                        case '<': fputs("&lt;", f);  break;
                        case '>': fputs("&gt;", f);  break;
                        default:  fputc(*q, f);      break;
                    }
                }
                fputc('\n', f);
            }
            continue;
        }

        char* body = line;
        while (*body == ' ') ++body;

        const bool blank = (*body == '\0');
        const bool isUl = (body[0] == '-' || body[0] == '*') && body[1] == ' ';
        bool isOl = false;
        {
            char* d = body;
            while (*d >= '0' && *d <= '9') ++d;
            isOl = (d != body) && (d[0] == '.') && (d[1] == ' ');
        }
        const bool isTable = (body[0] == '|');

        /* Close whatever block the new line is not a continuation of. */
        if (block == UL && !isUl)       { fputs("</ul>\n", f); block = NONE; }
        if (block == OL && !isOl)       { fputs("</ol>\n", f); block = NONE; }
        if (block == TABLE && !isTable) { fputs("</tbody></table></div>\n", f);
                                          block = NONE; }

        if (blank) continue;

        if (strncmp(body, "```", 3) == 0) {
            fputs("<pre><code>", f);
            block = CODE;
            continue;
        }

        if (isTable) {
            if (isTableRule(body)) {
                /* The rule closes the header and opens the body. */
                if (block == TABLE && !tableHeaderDone) {
                    fputs("</thead><tbody>\n", f);
                    tableHeaderDone = true;
                }
                continue;
            }
            if (block != TABLE) {
                fputs("<div class=\"tablewrap\"><table><thead>\n", f);
                block = TABLE;
                tableHeaderDone = false;
            }
            renderTableRow(f, body, !tableHeaderDone);
            continue;
        }

        if (body[0] == '#') {
            int level = 0;
            while (body[level] == '#') ++level;
            if (level <= 6 && body[level] == ' ') {
                /* The document's own H1 is dropped: the page already has one
                   from its title, and two is a page that looks broken. */
                if (level == 1) continue;
                fprintf(f, "<h%d>", level);
                renderInlineLine(f, body + level + 1);
                fprintf(f, "</h%d>\n", level);
                continue;
            }
        }

        if (strncmp(body, "---", 3) == 0 && body[strspn(body, "- ")] == '\0') {
            fputs("<hr>\n", f);
            continue;
        }

        if (isUl) {
            if (block != UL) { fputs("<ul>\n", f); block = UL; }
            fputs("<li>", f);
            renderInlineLine(f, body + 2);
            consumeWrapped(f, p);
            fputs("</li>\n", f);
            continue;
        }
        if (isOl) {
            if (block != OL) { fputs("<ol>\n", f); block = OL; }
            char* d = body;
            while (*d != ' ') ++d;
            fputs("<li>", f);
            renderInlineLine(f, d + 1);
            consumeWrapped(f, p);
            fputs("</li>\n", f);
            continue;
        }

        fputs("<p>", f);
        renderInlineLine(f, body);
        consumeWrapped(f, p);
        fputs("</p>\n", f);
    }

    if (block == UL)    fputs("</ul>\n", f);
    if (block == OL)    fputs("</ol>\n", f);
    if (block == CODE)  fputs("</code></pre>\n", f);
    if (block == TABLE) fputs("</tbody></table></div>\n", f);
}

/* Every tutorial carries the same five sections, in the same order. WIKI.md
   fixes the shape so a reader who has followed one knows where the failure list
   is on every other -- and that list is the point of the page, because it is the
   half a player cannot work out alone.

   Checked here rather than trusted, so a tutorial written without its failure
   list fails the build instead of shipping as half a page. */
static const char* TUTORIAL_SECTIONS[] = {
    "## You will need",
    "## Steps",
    "## When it works",
    "## When it does not",
    "## Next"
};
static const int N_TUTORIAL_SECTIONS =
    (int)(sizeof(TUTORIAL_SECTIONS) / sizeof(TUTORIAL_SECTIONS[0]));

static void checkTutorialShape(const char* source, const char* text) {
    /* The two documents that predate the wiki are concept pages in tutorial
       clothing: they teach a system rather than get one job done, and forcing
       "You will need" onto Circuits would be shape for its own sake. Named
       explicitly rather than exempted by a rule, so the exception stays a
       decision instead of becoming a habit. */
    if (strcmp(source, "CIRCUITS.md") == 0) return;
    if (strcmp(source, "LOGISTICS.md") == 0) return;

    for (int i = 0; i < N_TUTORIAL_SECTIONS; ++i) {
        if (strstr(text, TUTORIAL_SECTIONS[i])) continue;
        char detail[256];
        snprintf(detail, sizeof(detail), "%s has no \"%s\" section",
                 source, TUTORIAL_SECTIONS[i] + 3);
        fail("a tutorial is missing one of its five sections", detail);
    }
}

static void writeProse() {
    ensureDir("web/wiki/guide");

    int tutorialNo = 0;
    for (int i = 0; i < N_PROSE; ++i) {
        long len = 0;
        char* text = readWholeFile(PROSE[i].source, &len);
        /* A missing source is a broken build, not a page quietly left out. */
        if (!text) fail("cannot read prose source", PROSE[i].source);
        if (len < 200) fail("prose source is suspiciously short", PROSE[i].source);
        if (PROSE[i].tutorial) {
            ++tutorialNo;
            checkTutorialShape(PROSE[i].source, text);
        }

        FILE* f = pageOpen(PROSE[i].out, 1, PROSE[i].title, "guide");
        fputs("<h1>", f);
        escapeTo(f, PROSE[i].title);
        fputs("</h1>\n", f);
        if (PROSE[i].blurb) {
            fputs("<p class=\"lede\">", f);
            escapeTo(f, PROSE[i].blurb);
            fputs("</p>\n", f);
        }
        renderMarkdown(f, text);
        fputs("<p class=\"n\"><a href=\"index.html\">&larr; all guides</a></p>\n", f);
        pageClose(f, 1);
        free(text);
    }

    FILE* f = pageOpen("guide/index.html", 1, "Guide", "guide");
    fputs("<h1>Guide</h1>\n", f);
    fputs("<p class=\"lede\">The written half. Everything else on this site is\n"
          "generated from the game&rsquo;s own tables; these pages explain how the\n"
          "systems behave and how to get things done, which no table holds.</p>\n", f);

    fputs("<h2>In order</h2>\n", f);
    fputs("<p>Each one assumes only the ones above it, so read them in this\n"
          "order the first time.</p>\n<ol>\n", f);
    for (int i = 0; i < N_PROSE; ++i) {
        if (!PROSE[i].tutorial) continue;
        const char* slug = strrchr(PROSE[i].out, '/');
        fprintf(f, "<li><a href=\"%s\">", slug ? slug + 1 : PROSE[i].out);
        escapeTo(f, PROSE[i].title);
        fputs("</a>", f);
        if (PROSE[i].blurb) {
            fputs(" <span class=\"dim\">&mdash; ", f);
            escapeTo(f, PROSE[i].blurb);
            fputs("</span>", f);
        }
        fputs("</li>\n", f);
    }
    fputs("</ol>\n", f);

    bool anyConcept = false;
    for (int i = 0; i < N_PROSE; ++i) {
        if (PROSE[i].tutorial) continue;
        if (!anyConcept) {
            fputs("<h2>How it works underneath</h2>\n", f);
            fputs("<p>Not tasks, but the behaviour the tasks rest on.</p>\n<ul>\n", f);
            anyConcept = true;
        }
        const char* slug = strrchr(PROSE[i].out, '/');
        fprintf(f, "<li><a href=\"%s\">", slug ? slug + 1 : PROSE[i].out);
        escapeTo(f, PROSE[i].title);
        fputs("</a>", f);
        if (PROSE[i].blurb) {
            fputs(" <span class=\"dim\">&mdash; ", f);
            escapeTo(f, PROSE[i].blurb);
            fputs("</span>", f);
        }
        fputs("</li>\n", f);
    }
    if (anyConcept) fputs("</ul>\n", f);

    pageClose(f, 1);
}

/* The wiki's own sitemap, listing every page the generator wrote. robots.txt
   names it alongside the site's, which is what the protocol allows and is
   simpler than merging two generated-and-hand-written halves into one file.

   Kept in step with web/index.html by hand, like the palette: one URL changing
   once is not worth a build step. */
static const char* SITE_URL = "https://cinderlift.com/";

/* --- search ---------------------------------------------------------------

   A generated index of every page, and a page that filters it in the browser.

   This is the one place the wiki has a second copy of anything, which WIKI.md
   is otherwise firm about avoiding -- so it is worth being precise about why it
   is not the thing that rule forbids. The rule is against a copy that can
   DISAGREE: a hand-written page quoting a number the tables have since changed.
   This index is emitted in the same pass, from the same list of pages the site
   was built from, so it cannot describe a page that does not exist or miss one
   that does. It is a projection, not a duplicate.

   Written as JavaScript rather than JSON so the search page needs no fetch:
   a file:// copy of the wiki searches as readily as the served one, which is
   the same reason every link on this site is relative. */
static void writeSearchIndex() {
    char path[512];
    snprintf(path, sizeof(path), "%s/searchdata.js", OUT_DIR);
    FILE* f = fopen(path, "wb");
    if (!f) fail("cannot write", path);

    fputs("/* GENERATED by tools/wiki.cpp from the pages it wrote. */\n"
          "window.WIKI_PAGES = [\n", f);
    for (int i = 0; i < g_pageCount; ++i) {
        const WrittenPage& p = g_pages[i];
        /* Index pages are how you get to the things, not things themselves;
           searching for "iron" should not return "Materials". */
        const char* leaf = strrchr(p.path, '/');
        leaf = leaf ? leaf + 1 : p.path;
        if (strcmp(leaf, "index.html") == 0) continue;

        fputs("[\"", f);
        for (const char* c = p.title; *c; ++c) {
            if (*c == '"' || *c == '\\') fputc('\\', f);
            fputc(*c, f);
        }
        fputs("\",\"", f);
        fputs(p.path, f);
        fputs("\",\"", f);
        fputs(p.section, f);
        fputs("\"],\n", f);
    }
    fputs("];\n", f);
    if (fclose(f) != 0) fail("failed to close", path);
}

static void writeSearchPage() {
    FILE* f = pageOpen("search.html", 0, "Search", "search");
    fputs("<h1>Search</h1>\n", f);
    fputs("<p class=\"lede\">Every page on the wiki, by name.</p>\n", f);

    fputs("<div class=\"filterbar\">\n", f);
    fputs("<input id=\"q\" type=\"search\" autofocus "
          "placeholder=\"Type a name…\" aria-label=\"Search the wiki\">\n", f);
    fputs("<span class=\"count\" id=\"n\"></span>\n", f);
    fputs("</div>\n", f);
    fputs("<ul id=\"results\"></ul>\n", f);

    /* With JavaScript off the box above does nothing, so the page must still
       be a way in rather than a dead end. */
    fputs("<noscript><p>Search needs JavaScript. The indexes work without "
          "it:</p></noscript>\n", f);
    fputs("<h2>Or browse</h2>\n<ul>\n", f);
    for (int i = 0; i < N_SECTIONS; ++i) {
        if (!SECTIONS[i].built || !SECTIONS[i].slug[0]) continue;
        fprintf(f, "<li><a href=\"%s/index.html\">", SECTIONS[i].slug);
        escapeTo(f, SECTIONS[i].label);
        fputs("</a></li>\n", f);
    }
    fputs("</ul>\n", f);

    fputs("<script src=\"searchdata.js\"></script>\n", f);
    fputs("<script src=\"search.js\"></script>\n", f);
    pageClose(f, 0);
}

static void writeSitemap() {
    char path[512];
    snprintf(path, sizeof(path), "%s/sitemap.xml", OUT_DIR);
    FILE* f = fopen(path, "wb");
    if (!f) fail("cannot write", path);

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", f);
    fputs("<!-- GENERATED by tools/wiki.cpp from the pages it actually wrote.\n"
          "     Do not edit: a hand-maintained list of several hundred URLs is\n"
          "     a list that is wrong. -->\n", f);
    fputs("<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n", f);
    for (int i = 0; i < g_pageCount; ++i) {
        /* index.html is dropped from the URL: a directory and its index are
           one page, and listing both invites a search engine to treat them as
           duplicates of each other. */
        char url[256];
        snprintf(url, sizeof(url), "%s", g_pages[i].path);
        char* tail = strstr(url, "index.html");
        if (tail && (tail == url || tail[-1] == '/')) *tail = '\0';
        fprintf(f, "  <url>\n    <loc>%swiki/%s</loc>\n"
                   "    <changefreq>weekly</changefreq>\n  </url>\n",
                SITE_URL, url);
    }
    fputs("</urlset>\n", f);
    if (fclose(f) != 0) fail("failed to close", path);
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
    int stackable = 0, described = 0, nonMaterialItems = 0;
    for (int i = MAT_COUNT; i < ITEM_COUNT; ++i)
        if (ITEMS[i].maxStack) ++nonMaterialItems;
    for (int i = 1; i < ITEM_COUNT; ++i) {
        if (ITEMS[i].maxStack) ++stackable;
        if (ITEMS[i].description && ITEMS[i].description[0]) ++described;
    }

    ensureDir("web");
    ensureDir(OUT_DIR);

    static int cellOfItem[ITEM_COUNT];
    int sheetW = 0, sheetH = 0, iconCount = 0;
    writeIcons(cellOfItem, sheetW, sheetH, iconCount);

    /* Before any page is written: both directions of the recipe table, so a
       page can say what a thing is for as readily as what it is made of. */
    buildRecipeRefs();

    writeMaterialIndex(cellOfItem);
    writeItems(cellOfItem);
    writeRecipes(cellOfItem);
    writeCreatures(cellOfItem);
    writeDevices();
    writeProse();

    /* --- the hub ---------------------------------------------------------

       Two columns rather than one list, because two different readers arrive
       here for opposite reasons: one wants to be taught something and one wants
       to look one thing up. A single list serves neither.

       The reference column carries live COUNTS -- "115 materials", not
       "Materials" -- because the count is what tells a reader the table is
       complete rather than a selection somebody curated.

       No content of its own. The moment the hub starts explaining something it
       has become a page that can contradict another page. */
    FILE* f = pageOpen("index.html", 0, "Home", "");
    fputs("<h1>The Cinderlift wiki</h1>\n", f);
    fputs("<p class=\"lede\">A reference for a game about digging, heat and\n"
          "leaving. Generated from the game&rsquo;s own tables, so nothing here\n"
          "can disagree with what the game actually does.</p>\n", f);

    fputs("<div class=\"doors\">\n", f);

    fputs("<section class=\"door\">\n<h2>Learn</h2>\n", f);
    fputs("<p>How the systems behave, which no table holds.</p>\n<ul>\n", f);
    for (int i = 0; i < N_PROSE; ++i) {
        fprintf(f, "<li><a href=\"%s\">", PROSE[i].out);
        escapeTo(f, PROSE[i].title);
        fputs("</a></li>\n", f);
    }
    fputs("</ul>\n", f);
    fputs("<p class=\"n\">Read them in order the first time &mdash; each one\n"
          "assumes only the ones above it.</p>\n", f);
    fputs("</section>\n", f);

    fputs("<section class=\"door\">\n<h2>Look up</h2>\n", f);
    fputs("<p>Every fact the game holds about itself.</p>\n<ul>\n", f);
    /* Driven by the same table the nav is, so the hub cannot offer a door the
       nav has not got -- or, worse, one that is not there yet. */
    for (int i = 0; i < N_SECTIONS; ++i) {
        if (!SECTIONS[i].built || !SECTIONS[i].slug[0]) continue;
        if (strcmp(SECTIONS[i].slug, "guide") == 0) continue;   /* the other door */
        fprintf(f, "<li><a href=\"%s/index.html\">", SECTIONS[i].slug);
        escapeTo(f, SECTIONS[i].label);
        fputs("</a>", f);
        if (strcmp(SECTIONS[i].slug, "materials") == 0)
            fprintf(f, " <span class=\"n\">%d</span>", (int)MAT_COUNT - 1);
        if (strcmp(SECTIONS[i].slug, "items") == 0)
            fprintf(f, " <span class=\"n\">%d</span>", nonMaterialItems);
        if (strcmp(SECTIONS[i].slug, "recipes") == 0)
            fprintf(f, " <span class=\"n\">%d</span>", N_RECIPES);
        if (strcmp(SECTIONS[i].slug, "creatures") == 0)
            fprintf(f, " <span class=\"n\">%d</span>", (int)ENT_COUNT - 1);
        if (strcmp(SECTIONS[i].slug, "devices") == 0)
            fprintf(f, " <span class=\"n\">%d</span>", (int)DEV_COUNT - 1);
        fputs("</li>\n", f);
    }
    fputs("</ul>\n", f);
    fputs("<p class=\"n\">Every table the game holds is a page here, and every\n"
          "page says what its subject is made from and what it is for.</p>\n", f);
    fputs("</section>\n", f);

    fputs("</div>\n", f);
    pageClose(f, 0);

    writeSearchIndex();
    writeSearchPage();
    writeSitemap();

    printf("wiki: wrote %s/index.html\n", OUT_DIR);
    printf("      %d pages, listed in sitemap.xml\n", g_pageCount);
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
