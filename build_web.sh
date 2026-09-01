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
# Git Bash rewrites any argument that looks like an absolute POSIX path into
# a Windows one, so -DCINDERLIFT_SAVE_PATH="/saves/..." reached the compiler
# as "C:/Program Files/Git/saves/...". The game then tried to save to a
# directory that does not exist in the tab's filesystem and every browser save
# failed -- silently, because the path looked plausible in the error message.
# This turns that rewriting off for the two defines that carry MEMFS paths.
export MSYS2_ARG_CONV_EXCL='-DCINDERLIFT_SAVE_PATH;-DCINDERLIFT_LEGACY_SAVE_PATH'

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
# network.cpp is IN now. It used to be held back for src/web/network_stub.cpp,
# which refused to host or join because a tab cannot open a TCP socket. The
# socket calls have since been put behind a seam and web/netshim.cpp fills it
# with a WebRTC data channel, so the browser runs the same protocol as Windows
# rather than a second copy of it.
SRC=$(ls src/*.cpp)
SRC="$SRC $(ls src/web/*.cpp)"

BUILD_ID=$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)

# Shown in the menu and on the page, so a deploy can be told apart from the
# one before it. Prints "unknown" when tags are missing, which in CI means
# actions/checkout was not given fetch-depth: 0.
CL_VERSION=$(bash scripts/version.sh 2>/dev/null || echo unknown)

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
#
# -l idbfs.js and the save-path defines are what make saving mean anything
# here. Emscripten's default filesystem is MEMFS -- a JavaScript object that
# dies with the tab -- so without them the save screen works perfectly and
# loses every world on reload, which is worse than not offering saves at all.
# IDBFS is backed by IndexedDB, and index.html mounts it over /saves; the
# game is pointed there by overriding the save path rather than by any
# web-specific branch inside the game, which is the same trick that keeps
# this port a shim rather than a fork.
#
# The runtime methods are exported because index.html's preRun runs in the
# PAGE scope, not the module scope, so it reaches them through Module.* --
# and -O3 would otherwise drop names that nothing in the C++ refers to.
# INITIAL_MEMORY is sized for the world planes, which are static globals:
# cells is 4 bytes x 4096 x 9216 = 151 MB, with temp and bg another 37.7 MB
# each. That is ~226 MB before anything else, so the default 16 MB heap cannot
# even link. Growth is allowed on top for the rest.
"${EMXX_CMD[@]}" \
    -std=c++11 -O3 \
    -I src \
    -DCINDERLIFT_BUILD_ID="\"$BUILD_ID\"" \
    -DCINDERLIFT_VERSION="\"$CL_VERSION\"" \
    -DCINDERLIFT_SAVE_PATH="\"/saves/cinderlift.sav\"" \
    -DCINDERLIFT_LEGACY_SAVE_PATH="\"/saves/crucible.sav\"" \
    ${EXTRA_FLAGS:-} \
    $SRC \
    -s USE_SDL=2 \
    -l idbfs.js \
    -s EXPORTED_RUNTIME_METHODS=FS,IDBFS,addRunDependency,removeRunDependency,stringToUTF8,UTF8ToString,ccall,cwrap \
    -s ASYNCIFY=1 \
    -s ASYNCIFY_STACK_SIZE=65536 \
    -s INITIAL_MEMORY=671088640 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MAXIMUM_MEMORY=2147483648 \
    -s STACK_SIZE=5242880 \
    -s EXIT_RUNTIME=0 \
    -s MODULARIZE=0 \
    -s ENVIRONMENT=web \
    -o web/cinderlift.js

echo "Built web/cinderlift.js + web/cinderlift.wasm"
ls -lh web/cinderlift.wasm
echo
# Said here because opening web/index.html by double-clicking it is the
# obvious thing to try and it cannot work: a file:// page is not allowed to
# fetch WebAssembly, so the page renders and then hangs with no error.
echo "Serve it -- do NOT just open web/index.html, a file:// page cannot load wasm:"
echo "    python -m http.server 8099 -d web"
echo "    then open http://localhost:8099"
