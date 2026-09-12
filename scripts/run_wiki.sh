#!/usr/bin/env bash
# =============================================================================
# run_wiki.sh -- build tools/wiki.cpp and regenerate web/wiki/.
#
#     bash scripts/run_wiki.sh          build if stale, then generate
#     bash scripts/run_wiki.sh --clean  throw the objects away first
#
# Run this when you want the wiki updated. Nothing in CI does it: the output is
# committed, and the Pages workflow publishes it with the next deploy because it
# already uploads the whole of web/.
#
# The link set is the TEST HARNESS set -- every src/*.cpp except main.cpp and
# network.cpp -- and that is not an arbitrary choice. Reading one item's name
# reaches item.cpp, which reaches sprites, devices, entities and the world, so
# there is no small subset that compiles. The harness set is the smallest set
# that is already known to link, and it was measured to need NO Windows
# libraries at all: network.cpp is the only winsock file and it is already out.
# So this should build on Linux as readily as here, which is what keeps the
# option of moving it into the ubuntu Pages job open.
#
# Objects live in their own directory rather than sharing build/tobj with the
# tests. They are compiled with different flags, and a shared object directory
# where the flags disagree is how you end up linking code that was built for a
# different configuration -- see the note in run_tests.sh about stale objects
# going green while measuring values that no longer exist.
# =============================================================================
set -u

cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
OBJDIR=build/wikiobj
BIN=build/wiki.exe

if [ "${1:-}" = "--clean" ]; then
    rm -rf "$OBJDIR" "$BIN"
    shift || true
fi

mkdir -p "$OBJDIR"

# -O1: this is a batch tool that runs for a moment and writes files. -O3 buys
# nothing measurable here and costs real time on 29 translation units, which
# matters because the whole point of the object cache is a fast edit loop.
# -Wall -Wextra stays, because the generator indexes tables by id and an
# off-by-one there is a wrong page rather than a crash.
FLAGS="-std=c++11 -O1 -Wall -Wextra -I src"
LIBS=""

COMMON_SRC=$(ls src/*.cpp | grep -v '/main\.cpp$' | grep -v '/network\.cpp$')

# Same blunt rule run_tests.sh uses, and for the same reason: judging staleness
# per-cpp means a constant changed in a HEADER is silently compiled out of a
# stale object, and the tool then reports numbers from code that no longer
# exists. Any header newer than an object rebuilds it.
NEWEST_HDR=$(ls -t src/*.h src/web/*.h 2>/dev/null | head -1)

stale() {
    [ ! -f "$2" ] && return 0
    [ "$1" -nt "$2" ] && return 0
    [ -n "$NEWEST_HDR" ] && [ "$NEWEST_HDR" -nt "$2" ] && return 0
    return 1
}

OBJS=""
compiled=0
for src in $COMMON_SRC; do
    obj="$OBJDIR/$(basename "${src%.cpp}").o"
    OBJS="$OBJS $obj"
    if stale "$src" "$obj"; then
        printf '  compiling %s\n' "$(basename "$src")"
        if ! $CXX $FLAGS -c "$src" -o "$obj"; then
            echo "run_wiki: failed to compile $src" >&2
            exit 1
        fi
        compiled=$((compiled + 1))
    fi
done
[ "$compiled" -gt 0 ] && echo "  compiled $compiled file(s)"

echo "  linking $BIN"
if ! $CXX $FLAGS tools/wiki.cpp $OBJS -o "$BIN" $LIBS; then
    echo "run_wiki: failed to link the generator" >&2
    exit 1
fi

# Run from the repository root, because every path the generator writes is
# relative to it -- and the generator checks, rather than trusting this.
echo "  generating"
if ! "./$BIN"; then
    echo "run_wiki: the generator failed -- web/wiki/ may be incomplete" >&2
    exit 1
fi

echo
echo "Wiki written to web/wiki/. It is committed output, so:"
echo "    git add web/wiki && git commit"
echo "Pages serves max-age=600, so a deployed change can take ten minutes to show."
