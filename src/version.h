/* --- the build's own version number ----------------------------------------

   CINDERLIFT_VERSION is "MAJOR.MINOR.PATCH.N": the most recent release tag,
   then the number of commits made since it. scripts/version.sh computes it and
   every build script compiles it in; the reasoning for deriving it from git
   rather than storing a counter is written out there and not repeated here.

   It exists to be READ BY A HUMAN who wants to know whether the build in front
   of them is the one they just pushed. The menu shows it and so does the web
   page.

   Not to be confused with CINDERLIFT_BUILD_ID, which is the git hash and is
   part of the multiplayer handshake -- two players are refused a connection
   when theirs differ, because a protocol mismatch corrupts a world rather than
   failing cleanly. This is deliberately NOT wired into that check. It is a
   label, and giving a label the power to refuse connections would mean an
   untagged checkout ("unknown") could not play with anything, including itself
   built twice. Keep it decorative on purpose. */
#ifndef CINDERLIFT_VERSION_H
#define CINDERLIFT_VERSION_H

/* Building without the flag -- someone compiling a single file by hand, or a
   tarball with no git -- is not an error, it just cannot know. Say so rather
   than guess; see the note in scripts/version.sh about why a wrong version is
   worse than an absent one. */
#ifndef CINDERLIFT_VERSION
#define CINDERLIFT_VERSION "unknown"
#endif

#endif
