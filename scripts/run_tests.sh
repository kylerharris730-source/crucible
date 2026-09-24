#!/usr/bin/env bash
# =============================================================================
# run_tests.sh -- build and run the test suite.
#
#     make test                 everything
#     make test-quick           everything but the slow few (see QUICK_SKIP)
#     make test T=melee_test    one of them
#     bash scripts/run_tests.sh [--quick] [-j N] [name ...]
#
# Tests build and run in parallel, one per core (-j N or JOBS=N to change it;
# -j 1 is the old serial run). A handful that cannot share the machine run
# afterwards, one at a time -- see SERIAL. Output lines arrive in the order
# tests finish; the summary at the bottom is what to read.
#
# Until this existed there was no way to run the tests at all. Each one is a
# standalone main() that links every src/*.cpp except main.cpp, and the only
# way to run the suite was to reconstruct that command by hand. That is not a
# theoretical inconvenience: a sweep once reported 37 failures that were really
# one mistyped script name, because "the binary did not run" and "the test
# failed" looked identical from the outside. Hence the summary at the bottom,
# which distinguishes them.
#
# Objects are shared between tests and rebuilt only when their source is newer,
# so the second run costs a link each rather than 27 compiles.
# =============================================================================
set -u

cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
OBJDIR=build/tobj
BINDIR=build/tbin
mkdir -p "$OBJDIR" "$BINDIR"

VERSION=$(bash scripts/version.sh 2>/dev/null || echo unknown)
# -O3 to match the shipped build, for the reason this file already gives
# about stale objects: a suite compiled differently from the game is measuring
# something the game does not do.
FLAGS="-std=c++11 -O3 -Wall -Wextra -I src"
FLAGS="$FLAGS -DCINDERLIFT_VERSION=\"$VERSION\""
LIBS="-lws2_32 -lwinmm"

# network.cpp is held out of the shared set: the mismatch test needs it
# compiled twice with different build ids, and it is the ONLY file that reads
# CINDERLIFT_BUILD_ID. Everything else can be shared by every binary.
COMMON_SRC=$(ls src/*.cpp | grep -v '/main\.cpp$' | grep -v '/network\.cpp$')

# Newest header in the tree. Objects older than this are rebuilt.
#
# Without it, editing a HEADER rebuilt nothing: staleness was judged by
# comparing each .cpp against its own .o, so a constant changed in world.h was
# silently compiled out of a stale object and every test measured the old
# value. That is worse than a build error, because the suite goes green and
# the numbers you are reading are from code that no longer exists -- it cost a
# tuning session before it was spotted.
#
# A blunt "any header changed, rebuild everything" rather than real dependency
# tracking: 27 objects is a few seconds, and the alternative is a makedepend
# that can itself be wrong.
NEWEST_HDR=$(ls -t src/*.h src/web/*.h 2>/dev/null | head -1)

stale() {
    [ ! -f "$2" ] && return 0
    [ "$1" -nt "$2" ] && return 0
    [ -n "$NEWEST_HDR" ] && [ "$NEWEST_HDR" -nt "$2" ] && return 0
    return 1
}

compile() {   # compile <src> <obj> <build-id>
    stale "$1" "$2" || return 0
    # shellcheck disable=SC2086
    $CXX $FLAGS -DCINDERLIFT_BUILD_ID="\"$3\"" -c "$1" -o "$2" 2>>"$OBJDIR/errors.log"
}

printf 'building objects (version %s)\n' "$VERSION"
: > "$OBJDIR/errors.log"
pids=""
for src in $COMMON_SRC; do
    obj="$OBJDIR/$(basename "$src" .cpp).o"
    compile "$src" "$obj" "test" & pids="$pids $!"
done
for p in $pids; do wait "$p" || true; done

# The three network objects: one ordinary, two that disagree about their build.
compile src/network.cpp "$OBJDIR/network.o"        "test"
compile src/network.cpp "$OBJDIR/network_host.o"   "mismatch-host-build"
compile src/network.cpp "$OBJDIR/network_client.o" "mismatch-client-build"

if [ -s "$OBJDIR/errors.log" ] && grep -q 'error' "$OBJDIR/errors.log"; then
    echo "--- object build failed ---" >&2
    grep -m 20 -A2 'error' "$OBJDIR/errors.log" >&2
    exit 1
fi

COMMON_OBJ=$(ls "$OBJDIR"/*.o | grep -v 'network_host\.o$' | grep -v 'network_client\.o$' | tr '\n' ' ')
MISMATCH_OBJ=$(ls "$OBJDIR"/*.o | grep -v '/network\.o$' | grep -v 'network_client\.o$' | tr '\n' ' ')
MISMATCH_CLI=$(ls "$OBJDIR"/*.o | grep -v '/network\.o$' | grep -v 'network_host\.o$' | tr '\n' ' ')

# --- options -----------------------------------------------------------------
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
QUICK=0
while [ $# -gt 0 ]; do
    case "$1" in
        --quick) QUICK=1; shift ;;
        -j)      JOBS="$2"; shift 2 ;;
        -j*)     JOBS="${1#-j}"; shift ;;
        *)       break ;;
    esac
done
[ "$JOBS" -ge 1 ] 2>/dev/null || JOBS=1

# Run AFTER the parallel batch, one at a time. The network tests bind fixed
# ports and wait on the wall clock, so two at once fight over the port and a
# loaded machine eats into their timeouts. nav_cost asserts a CPU-time budget
# for a routing rebuild, which a machine running eleven other tests would blow.
SERIAL=" lan_address network_smoke network_four network_mismatch nav_cost "

# What --quick leaves out: the slowest sim tests and the multi-second network
# ones. Measured serially on a 12-thread machine, the full suite's run time was
# 287 s and these nine were 173 s of it -- bee_routes alone is 75 s and
# hive_bees 34. Everything else is under 7 s, so in parallel the rest finishes
# in about the time of its slowest test. Run the full suite before committing;
# quick is for the edit loop.
QUICK_SKIP=" bee_routes hive_bees drone_combat heat_lamp deep_layer deep_roster
             network_smoke network_four network_mismatch "

report() { printf '  %-6s %-24s %6s\n' "$1" "$2" "$3"; }
now_ms() { echo $(( $(date +%s%N) / 1000000 )); }

# Each test leaves "<status> <ms>" in its .result file, and the summary is
# built from those once everything has finished. Counting in shell variables,
# as this used to, does not survive running tests as background jobs: each
# job is a subshell, and its increments die with it.
finish() {   # finish <name> <ok|FAIL|BUILD|skip> <start-ms>
    local ms=$(( $(now_ms) - $3 ))
    printf '%s %s\n' "$2" "$ms" > "$BINDIR/$1.result"
    report "$2" "$1" "$(awk -v m="$ms" 'BEGIN { printf "%.1fs", m / 1000 }')"
}

# --- the ordinary tests ------------------------------------------------------
run_one() {
    local name="$1" src="tests/$1.cpp" log="$BINDIR/$1.log" t0; t0=$(now_ms)
    if ! $CXX $FLAGS -DCINDERLIFT_BUILD_ID='"test"' "$src" $COMMON_OBJ \
              -o "$BINDIR/$name.prog" $LIBS >"$log" 2>&1; then
        finish "$name" BUILD "$t0"; return
    fi
    if ( cd "$BINDIR" && "./$name.prog" ) >"$log" 2>&1; then
        finish "$name" ok "$t0"
    else
        finish "$name" FAIL "$t0"
    fi
}

# --- the one that needs two binaries -----------------------------------------
# The host is given the client's path and launches it; the client tries to join
# and must be refused on the build id before any world data moves. Skipped
# everywhere but Windows, because it is CreateProcess and winsock throughout.
run_mismatch() {
    local name=network_mismatch log="$BINDIR/network_mismatch.log" t0; t0=$(now_ms)
    case "$(uname -s 2>/dev/null || echo Windows)" in
        MINGW*|MSYS*|CYGWIN*|Windows*) ;;
        *) finish "$name" skip "$t0"; return ;;
    esac
    if ! $CXX $FLAGS -DCINDERLIFT_BUILD_ID='"mismatch-host-build"' \
              tests/$name.cpp $MISMATCH_OBJ -o "$BINDIR/${name}_host.prog" $LIBS \
              >"$log" 2>&1 \
    || ! $CXX $FLAGS -DCINDERLIFT_BUILD_ID='"mismatch-client-build"' \
              tests/$name.cpp $MISMATCH_CLI -o "$BINDIR/${name}_client.prog" $LIBS \
              >>"$log" 2>&1; then
        finish "$name" BUILD "$t0"; return
    fi
    if ( cd "$BINDIR" && "./${name}_host.prog" "${name}_client.prog" ) >"$log" 2>&1; then
        finish "$name" ok "$t0"
    else
        finish "$name" FAIL "$t0"
    fi
}

# --- the suite is shell as well as C++ ---------------------------------------
run_shell() {
    local name="$1" log="$BINDIR/$1.log" t0; t0=$(now_ms)
    if bash "tests/$1.sh" >"$log" 2>&1; then
        finish "$name" ok "$t0"
    else
        finish "$name" FAIL "$t0"
    fi
}

run_any() {
    case "$1" in
        network_mismatch) run_mismatch ;;
        *) if [ -e "tests/$1.cpp" ]; then run_one "$1"; else run_shell "$1"; fi ;;
    esac
}

SELECT="$*"
matches() { [ -z "$SELECT" ] && return 0; case " $SELECT " in *" $1 "*) return 0 ;; esac; return 1; }
listed()  { case " $(echo $2) " in *" $1 "*) return 0 ;; esac; return 1; }   # echo folds the lists' line breaks

# Which tests, in which group. Named explicitly (SELECT) always runs, quick or
# not: asking for a test by name is asking for it.
ALL=""; PAR=""; SER=""; SKIPPED=""
for src in tests/*.cpp tests/*.sh; do
    [ -e "$src" ] || continue
    name=$(basename "$src"); name=${name%.*}
    matches "$name" || continue
    if [ "$QUICK" = 1 ] && [ -z "$SELECT" ] && listed "$name" "$QUICK_SKIP"; then
        SKIPPED="$SKIPPED $name"; continue
    fi
    ALL="$ALL $name"
    rm -f "$BINDIR/$name.result"
    if listed "$name" "$SERIAL"; then SER="$SER $name"; else PAR="$PAR $name"; fi
done

T_START=$(now_ms)
echo "running tests ($JOBS at a time$([ "$QUICK" = 1 ] && echo ', quick'))"
for name in $PAR; do
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
    run_any "$name" &
done
wait
[ -n "$SER" ] && echo "  -- one at a time:$SER"
for name in $SER; do run_any "$name"; done

pass=0; failed=""; broken=""
for name in $ALL; do
    status=BUILD
    [ -f "$BINDIR/$name.result" ] && read -r status _ < "$BINDIR/$name.result"
    case "$status" in
        ok|skip) pass=$((pass + 1)) ;;
        FAIL)    failed="$failed $name" ;;
        *)       broken="$broken $name" ;;
    esac
done

echo
WALL=$(( $(now_ms) - T_START ))
printf 'wall %s; slowest:' "$(awk -v m="$WALL" 'BEGIN { printf "%.1fs", m / 1000 }')"
for name in $ALL; do
    [ -f "$BINDIR/$name.result" ] && { read -r _ ms < "$BINDIR/$name.result"; echo "$ms $name"; }
done | sort -rn | head -5 | awk '{ printf " %s %.1fs", $2, $1 / 1000 }'
echo
[ -n "$SKIPPED" ] && printf 'quick: skipped%s\n' "$SKIPPED"
nfail=$(printf '%s' "$failed" | wc -w)
nbroken=$(printf '%s' "$broken" | wc -w)
printf '%d passed, %d failed, %d did not build\n' "$pass" "$nfail" "$nbroken"
[ -n "$failed" ] && printf 'failed:%s\n' "$failed"
# Separated from failures on purpose: a test that did not COMPILE is a broken
# build, not a broken behaviour, and the two want different reactions.
[ -n "$broken" ] && printf 'did not build:%s  (see %s/<name>.log)\n' "$broken" "$BINDIR"
[ -z "$failed" ] && [ -z "$broken" ] && echo "PASS"
[ -n "$failed" ] || [ -n "$broken" ] && exit 1
exit 0
