CXX      := g++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra
GIT_HEAD := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
SOURCE_DIRTY := $(shell git status --porcelain --untracked-files=normal -- src Makefile build.bat 2>/dev/null)
ifeq ($(strip $(SOURCE_DIRTY)),)
BUILD_ID := $(GIT_HEAD)
else
# Safe false rejection for independently compiled dirty trees. Copying one
# executable to both machines preserves its embedded GUID and still connects.
BUILD_ID := $(GIT_HEAD)-dirty-$(shell powershell -NoProfile -Command "[guid]::NewGuid().ToString('N')")
endif
CXXFLAGS += -DCRUCIBLE_BUILD_ID=\"$(BUILD_ID)\"
# Keep distributed builds self-contained instead of requiring MinGW runtime
# DLLs to be copied alongside crucible.exe.
LDFLAGS  := -mwindows -static -static-libgcc -static-libstdc++ -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

# Discovered, not listed -- see the note in build.bat. The two scripts drifted
# apart once already and the cost was a deleted executable.
SRC := $(wildcard src/*.cpp)
HDR := $(wildcard src/*.h)
OUT := build/crucible.exe

all: $(OUT)

$(OUT): $(SRC) $(HDR)
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	$(OUT)

clean:
	@if exist build rmdir /s /q build

.PHONY: all run clean
