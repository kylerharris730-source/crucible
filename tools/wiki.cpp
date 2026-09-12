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
};

static const ProsePage PROSE[] = {
    { "CIRCUITS.md", "guide/circuits.html", "Circuits" },
};
static const int N_PROSE = (int)(sizeof(PROSE) / sizeof(PROSE[0]));

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
                    fputs("<a href=\"", f);
                    for (const char* p = close + 2; p < urlEnd; ++p) fputc(*p, f);
                    fputs("\">", f);
                    renderInline(f, s + 1, close);
                    fputs("</a>", f);
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

static void writeProse() {
    ensureDir("web/wiki/guide");

    for (int i = 0; i < N_PROSE; ++i) {
        long len = 0;
        char* text = readWholeFile(PROSE[i].source, &len);
        /* A missing source is a broken build, not a page quietly left out. */
        if (!text) fail("cannot read prose source", PROSE[i].source);
        if (len < 200) fail("prose source is suspiciously short", PROSE[i].source);

        FILE* f = pageOpen(PROSE[i].out, 1, PROSE[i].title, "guide");
        fprintf(f, "<h1>%s</h1>\n", PROSE[i].title);
        renderMarkdown(f, text);
        pageClose(f, 1);
        free(text);
    }

    FILE* f = pageOpen("guide/index.html", 1, "Guide", "guide");
    fputs("<h1>Guide</h1>\n", f);
    fputs("<p class=\"lede\">The written half. Everything else on this site is\n"
          "generated from the game&rsquo;s tables; these pages explain how the\n"
          "systems behave, which no table holds.</p>\n", f);
    fputs("<ul>\n", f);
    for (int i = 0; i < N_PROSE; ++i) {
        const char* slug = strrchr(PROSE[i].out, '/');
        fprintf(f, "<li><a href=\"%s\">", slug ? slug + 1 : PROSE[i].out);
        escapeTo(f, PROSE[i].title);
        fputs("</a></li>\n", f);
    }
    fputs("</ul>\n", f);
    fputs("<p>The tutorials land in stage 4 &mdash; see <code>WIKI_STEPS.md</code>.</p>\n", f);
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
    fputs("<p class=\"n\">The tutorials are being written &mdash; stage 4 of\n"
          "<code>WIKI_STEPS.md</code>.</p>\n", f);
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
        fputs("</li>\n", f);
    }
    fputs("</ul>\n", f);
    fprintf(f, "<p class=\"n\">Still to come: %d items, %d recipes over %d\n"
               "stations, %d creatures and %d devices.</p>\n",
            stackable, N_RECIPES, (int)STATION_COUNT,
            (int)ENT_COUNT, (int)DEV_COUNT);
    fputs("</section>\n", f);

    fputs("</div>\n", f);
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
