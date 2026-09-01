#!/usr/bin/env bash
# =============================================================================
# run_tests.sh -- build and run the test suite.
#
#     make test                 everything
#     make test T=melee_test    one of them
#     bash scripts/run_tests.sh [name ...]
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
FLAGS="-std=c++11 -O2 -Wall -Wextra -I src"
FLAGS="$FLAGS -DCINDERLIFT_VERSION=\"$VERSION\""
LIBS="-lws2_32"

# network.cpp is held out of the shared set: the mismatch test needs it
# compiled twice with different build ids, and it is the ONLY file that reads
# CINDERLIFT_BUILD_ID. Everything else can be shared by every binary.
COMMON_SRC=$(ls src/*.cpp | grep -v '/main\.cpp$' | grep -v '/network\.cpp$')

stale() { [ ! -f "$2" ] || [ "$1" -nt "$2" ]; }

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

pass=0; failed=""; broken=""

report() { printf '  %-6s %s\n' "$1" "$2"; }

# --- the ordinary tests ------------------------------------------------------
run_one() {
    local name="$1" src="tests/$1.cpp" log="$BINDIR/$1.log"
    if ! $CXX $FLAGS -DCINDERLIFT_BUILD_ID='"test"' "$src" $COMMON_OBJ \
              -o "$BINDIR/$name.prog" $LIBS >"$log" 2>&1; then
        broken="$broken $name"; report "BUILD" "$name"; return
    fi
    if ( cd "$BINDIR" && "./$name.prog" ) >"$log" 2>&1; then
        pass=$((pass + 1)); report "ok" "$name"
    else
        failed="$failed $name"; report "FAIL" "$name"
    fi
}

# --- the one that needs two binaries -----------------------------------------
# The host is given the client's path and launches it; the client tries to join
# and must be refused on the build id before any world data moves. Skipped
# everywhere but Windows, because it is CreateProcess and winsock throughout.
run_mismatch() {
    local name=network_mismatch log="$BINDIR/$name.log"
    case "$(uname -s 2>/dev/null || echo Windows)" in
        MINGW*|MSYS*|CYGWIN*|Windows*) ;;
        *) report "skip" "$name (Windows only)"; return ;;
    esac
    if ! $CXX $FLAGS -DCINDERLIFT_BUILD_ID='"mismatch-host-build"' \
              tests/$name.cpp $MISMATCH_OBJ -o "$BINDIR/${name}_host.prog" $LIBS \
              >"$log" 2>&1 \
    || ! $CXX $FLAGS -DCINDERLIFT_BUILD_ID='"mismatch-client-build"' \
              tests/$name.cpp $MISMATCH_CLI -o "$BINDIR/${name}_client.prog" $LIBS \
              >>"$log" 2>&1; then
        broken="$broken $name"; report "BUILD" "$name"; return
    fi
    if ( cd "$BINDIR" && "./${name}_host.prog" "${name}_client.prog" ) >"$log" 2>&1; then
        pass=$((pass + 1)); report "ok" "$name"
    else
        failed="$failed $name"; report "FAIL" "$name"
    fi
}

# --- the suite is shell as well as C++ ---------------------------------------
run_shell() {
    local name="$1" log="$BINDIR/$1.log"
    if bash "tests/$1.sh" >"$log" 2>&1; then
        pass=$((pass + 1)); report "ok" "$name"
    else
        failed="$failed $name"; report "FAIL" "$name"
    fi
}

echo "running tests"
SELECT="$*"
matches() { [ -z "$SELECT" ] && return 0; case " $SELECT " in *" $1 "*) return 0 ;; esac; return 1; }

for src in tests/*.cpp; do
    name=$(basename "$src" .cpp)
    matches "$name" || continue
    if [ "$name" = network_mismatch ]; then run_mismatch; else run_one "$name"; fi
done
for src in tests/*.sh; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .sh)
    matches "$name" || continue
    run_shell "$name"
done

echo
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
