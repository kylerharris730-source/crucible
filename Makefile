# Two shells, one simulation.
#
#   BACKEND=win32  src/main.cpp      Win32 + GDI, no external dependencies
#   BACKEND=sdl    src/main_sdl.cpp  SDL2, portable (macOS, Linux, Windows)
#
# Windows defaults to win32 so that build keeps working with nothing installed
# but a compiler. Everything else defaults to sdl. Override either way:
#
#   make BACKEND=sdl        (SDL build on Windows)
#   make                    (SDL build on macOS/Linux)
#
# Each backend has its own output name, so switching between them on one
# machine cannot leave you running a stale binary from the other.

CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra

# Make's built-in default for CXX is not usable on every toolchain -- this
# MinGW bakes in an absolute Unix-style path from the machine it was built on,
# which does not exist and, because of the forward slashes, sends make looking
# for a shell to run it. Replace it only when it is still the built-in default,
# so CXX from the environment or the command line is still honoured.
ifeq ($(origin CXX),default)
  CXX := g++
endif

CORE := src/common.cpp src/materials.cpp src/world.cpp src/render.cpp
HDRS := src/common.h src/materials.h src/world.h src/render.h src/panel.h

ifeq ($(OS),Windows_NT)
  BACKEND ?= win32
  EXT     := .exe
  MKDIR    = @if not exist build mkdir build
  RMDIR    = @if exist build rmdir /s /q build
else
  BACKEND ?= sdl
  EXT     :=
  MKDIR    = @mkdir -p build
  RMDIR    = @rm -rf build
endif

ifeq ($(BACKEND),win32)
  SRC  := $(CORE) src/main.cpp
  DEPS := $(HDRS)
  NAME := powder
  LDFLAGS := -mwindows -lgdi32 -luser32 -lwinmm
else
  SRC  := $(CORE) src/main_sdl.cpp
  DEPS := $(HDRS) src/font8.h
  # On Windows the SDL build is the non-default one, so it gets its own name;
  # everywhere else it is *the* build and is just "powder".
  ifeq ($(OS),Windows_NT)
    NAME := powder-sdl
    # No sdl2-config on a bare MinGW install. Point at an unpacked SDL2 dev
    # package: make BACKEND=sdl SDL_CFLAGS=-IC:/SDL2/include SDL_LIBS=-LC:/SDL2/lib
    SDL_CFLAGS ?=
    SDL_LIBS   ?=
    LDFLAGS := $(SDL_LIBS) -lmingw32 -lSDL2main -lSDL2 -mwindows
  else
    NAME := powder
    # sdl2-config ships with Homebrew's sdl2 and with most distro packages;
    # pkg-config is the fallback for the ones that drop it.
    SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
    SDL_LIBS   ?= $(shell sdl2-config --libs   2>/dev/null || pkg-config --libs   sdl2 2>/dev/null)
    LDFLAGS := $(SDL_LIBS)
    # Fail with something readable rather than a wall of "SDL.h: No such file".
    ifneq ($(MAKECMDGOALS),clean)
      ifeq ($(strip $(SDL_LIBS)),)
        $(error SDL2 not found. Install it with: brew install sdl2 (macOS), or sudo apt install libsdl2-dev (Debian/Ubuntu))
      endif
    endif
  endif
  CXXFLAGS += $(SDL_CFLAGS)
endif

OUT := build/$(NAME)$(EXT)

ifeq ($(OS),Windows_NT)
  RUN = build\$(NAME)$(EXT)
else
  RUN = ./$(OUT)
endif

all: $(OUT)

$(OUT): $(SRC) $(DEPS)
	$(MKDIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	$(RUN)

clean:
	$(RMDIR)

.PHONY: all run clean
