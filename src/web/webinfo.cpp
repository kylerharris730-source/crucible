/* --- what the page is allowed to ask the build about it ---------------------

   One function, and it exists so the website can show the same version number
   the in-game menu shows, taken from the same place. The alternative was
   build_web.sh writing the version into index.html, which would mean the page
   in the repository is never the page that ships, and a tracked file that is
   modified by every build.

   Reading it out of the wasm keeps one source of truth: if the menu and the
   page ever disagree, something is loading a stale wasm, and that is exactly
   the situation this number is here to reveal. */
#ifndef _WIN32

#include "../version.h"
#include <emscripten.h>

extern "C" {

/* Returns a pointer into static storage. The caller is JavaScript going
   through ccall with a 'string' return, which copies before this could matter,
   and the string is a compile-time literal that outlives everything anyway. */
EMSCRIPTEN_KEEPALIVE const char* webVersion(void) {
    return CINDERLIFT_VERSION;
}

}

#endif
