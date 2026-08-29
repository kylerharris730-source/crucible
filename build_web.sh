#!/usr/bin/env bash
# =============================================================================
# build_web.sh -- the browser build.
#
# Discovered, not listed, for the same reason build.bat is: a source file added
# to src/ must reach every build or the link comes apart on undefined
# references. The one file held back is network.cpp, which is winsock from top
# to bottom; src/web/network_stub.cpp stands in for it.
#
# Run from the repository root with emsdk active:
#     source ~/emsdk/emsdk_env.sh && ./build_web.sh
# =============================================================================
set -euo pipefail

cd "$(dirname "$0")"

# em++ rather than emcc, because this is C++ and em++ is the driver that links
# the C++ runtime.
#
# Resolved rather than assumed, and the fallbacks are not paranoia -- each one
# is a shape this actually takes. On Linux/CI `em++` is on PATH and runs. On
# this Windows checkout the extensionless `em++` beside the SDK is a POSIX
# wrapper that Git Bash finds first and cannot execute ("Permission denied"),
# and the em++.exe that emsdk is documented to provide was not present at all
# after install -- leaving em++.py, which runs perfectly under the interpreter
# emsdk bundles for exactly this purpose. Hence: PATH first, then the SDK's own
# Python driving the .py. EMXX overrides the lot.
EMXX_CMD=()
if [ -n "${EMXX:-}" ]; then
    EMXX_CMD=("$EMXX")
elif command -v em++ >/dev/null 2>&1 && [ -x "$(command -v em++)" ]; then
    EMXX_CMD=(em++)
else
    for root in "${EMSDK:-}" "${HOME:-}/emsdk" "/c/Users/${USERNAME:-${USER:-}}/emsdk"; do
        [ -n "$root" ] || continue
        driver="$root/upstream/emscripten/em++.py"
        [ -f "$driver" ] || continue
        python=$(ls -d "$root"/python/*/python.exe 2>/dev/null | head -1 || true)
        [ -n "$python" ] || python=$(command -v python3 || command -v python || true)
        [ -n "$python" ] || continue
        EMXX_CMD=("$python" "$driver")
        break
    done
fi
if [ ${#EMXX_CMD[@]} -eq 0 ]; then
    echo "em++ not found -- run: source ~/emsdk/emsdk_env.sh" >&2
    exit 1
fi

# Every game source except the winsock one, plus the shim.
SRC=$(ls src/*.cpp | grep -v '/network\.cpp$')
SRC="$SRC $(ls src/web/*.cpp)"

BUILD_ID=$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)

mkdir -p web

# --- why these flags ---------------------------------------------------------
# ASYNCIFY is the one that makes this port possible at all. main.cpp keeps its
# Win32 `while (g_running)` frame loop, which never returns -- and a function
# that never returns is a hung tab. ASYNCIFY rewrites the call graph so the
# Sleep() inside that loop can yield to the browser and resume where it left
# off, which is what lets the loop stay exactly as Windows has it. It costs
# code size and some speed; the alternative was restructuring the game's frame
# loop and maintaining two of them.
#
# INITIAL_MEMORY is sized for the world planes, which are static globals:
# cells is 4 bytes x 4096 x 9216 = 151 MB, with temp and bg another 37.7 MB
# each. That is ~226 MB before anything else, so the default 16 MB heap cannot
# even link. Growth is allowed on top for the rest.
"${EMXX_CMD[@]}" \
    -std=c++11 -O3 \
    -I src \
    -DCRUCIBLE_BUILD_ID="\"$BUILD_ID\"" \
    ${EXTRA_FLAGS:-} \
    $SRC \
    -s USE_SDL=2 \
    -s ASYNCIFY=1 \
    -s ASYNCIFY_STACK_SIZE=65536 \
    -s INITIAL_MEMORY=671088640 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MAXIMUM_MEMORY=2147483648 \
    -s STACK_SIZE=5242880 \
    -s EXIT_RUNTIME=0 \
    -s MODULARIZE=0 \
    -s ENVIRONMENT=web \
    -o web/crucible.js

echo "Built web/crucible.js + web/crucible.wasm"
ls -lh web/crucible.wasm
