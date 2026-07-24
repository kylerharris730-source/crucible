#!/bin/sh
# macOS / Linux counterpart of build.bat. Builds the portable SDL2 shell.
# Needs SDL2:  brew install sdl2        (macOS)
#              apt install libsdl2-dev  (Debian/Ubuntu)
set -e
cd "$(dirname "$0")"

if command -v sdl2-config >/dev/null 2>&1; then
    SDL_CFLAGS=$(sdl2-config --cflags)
    SDL_LIBS=$(sdl2-config --libs)
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
    SDL_CFLAGS=$(pkg-config --cflags sdl2)
    SDL_LIBS=$(pkg-config --libs sdl2)
else
    echo "SDL2 not found. Install it with:" >&2
    echo "  brew install sdl2          # macOS" >&2
    echo "  sudo apt install libsdl2-dev   # Debian/Ubuntu" >&2
    exit 1
fi

mkdir -p build
${CXX:-c++} -std=c++11 -O2 -Wall -Wextra $SDL_CFLAGS \
    src/common.cpp src/materials.cpp src/world.cpp src/render.cpp src/main_sdl.cpp \
    -o build/powder $SDL_LIBS

echo "Built build/powder"
