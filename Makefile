CXX      := g++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra
LDFLAGS  := -mwindows -lgdi32 -luser32 -lwinmm -lmsimg32

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
