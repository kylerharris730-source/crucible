CXX      := g++
# -O3, matching build_web.sh -- the native build was the odd one out. Measured
# on the lava-into-water case in tools/steamprof.cpp: sim 10.95 -> 10.46 ms and
# lighting 2.10 -> 1.79 ms, with the end state bit-identical to the -O2 build.
# -march is deliberately NOT raised with it: it buys another 0.5 ms in the sim
# and gives it straight back in the renderer, and -mfpmath=sse changes worldgen
# float rounding, so the same seed stops making the same world.
CXXFLAGS := -std=c++11 -O3 -Wall -Wextra
GIT_HEAD := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
SOURCE_DIRTY := $(shell git status --porcelain --untracked-files=normal -- src Makefile build.bat 2>/dev/null)
ifeq ($(strip $(SOURCE_DIRTY)),)
BUILD_ID := $(GIT_HEAD)
else
# Safe false rejection for independently compiled dirty trees. Copying one
# executable to both machines preserves its embedded GUID and still connects.
BUILD_ID := $(GIT_HEAD)-dirty-$(shell powershell -NoProfile -Command "[guid]::NewGuid().ToString('N')")
endif
CXXFLAGS += -DCINDERLIFT_BUILD_ID=\"$(BUILD_ID)\"

# The human-readable version: newest tag plus commits since it. Derived,
# never stored -- scripts/version.sh explains why at length. Shelling out to
# the one script rather than reimplementing it here is deliberate: build.bat
# has to reimplement it (cmd cannot source bash) and that is already one copy
# too many.
CL_VERSION := $(shell bash scripts/version.sh 2>/dev/null || echo unknown)
CXXFLAGS += -DCINDERLIFT_VERSION=\"$(CL_VERSION)\"
# Keep distributed builds self-contained instead of requiring MinGW runtime
# DLLs to be copied alongside cinderlift.exe.
LDFLAGS  := -mwindows -static -static-libgcc -static-libstdc++ -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

# Discovered, not listed -- see the note in build.bat. The two scripts drifted
# apart once already and the cost was a deleted executable.
SRC := $(wildcard src/*.cpp)
HDR := $(wildcard src/*.h)
OUT := build/cinderlift.exe

# Version resource, kept in step with build.bat rather than left to drift --
# an executable built here should identify itself to Windows exactly as the
# released one does, or the SmartScreen behaviour differs between the binary
# you test and the binary you ship. See res/version.rc.
VER_TAG := $(shell git describe --tags --abbrev=0 2>/dev/null)
VER_NUM := $(patsubst v%,%,$(VER_TAG))
VER_MA  := $(or $(word 1,$(subst ., ,$(VER_NUM))),0)
VER_MI  := $(or $(word 2,$(subst ., ,$(VER_NUM))),0)
VER_PA  := $(or $(word 3,$(subst ., ,$(VER_NUM))),0)
OBJDIR  := build/obj
RES     := $(OBJDIR)/version_game.o

all: $(OUT)

$(RES): res/version.rc
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	windres res/version.rc -o $(RES) \
	    -DVER_MAJOR=$(VER_MA) -DVER_MINOR=$(VER_MI) -DVER_PATCH=$(VER_PA) -DVER_TARGET=1

$(OUT): $(SRC) $(HDR) $(RES)
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) $(SRC) $(RES) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	$(OUT)

clean:
	@if exist $(OUT) del /q $(OUT)
	@if exist build\cinderlift.new.exe del /q build\cinderlift.new.exe
	@if exist $(OBJDIR) rmdir /s /q $(OBJDIR)

# Saves deliberately live beside a directly launched development executable.
# Never recursively remove build here: `make clean` must clean compiler output,
# not erase the user's worlds or the separately built launcher.

# --- tests -----------------------------------------------------------------
# Every test is a standalone main() linking all of src/ except main.cpp. There
# was no way to run them for a long time, and the cost was real: a sweep once
# reported 37 failures that were really one mistyped script name.
#
#     mingw32-make test                 all of them
#     mingw32-make test T=melee_test    just one
#
# The work lives in scripts/run_tests.sh rather than in pattern rules here,
# because network_mismatch needs the SAME source compiled twice with different
# CINDERLIFT_BUILD_ID values and linked into two binaries that then talk to
# each other -- which is not a thing one make rule can say.
T ?=
test:
	@bash scripts/run_tests.sh $(T)

.PHONY: all run clean test
