CXX      := g++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra
LDFLAGS  := -mwindows -lgdi32 -luser32 -lwinmm -lmsimg32

SRC := src/common.cpp src/materials.cpp src/world.cpp src/render.cpp src/player.cpp src/item.cpp src/sprite.cpp src/worldgen.cpp src/light.cpp src/room.cpp src/projectile.cpp src/main.cpp
OUT := build/crucible.exe

all: $(OUT)

$(OUT): $(SRC) src/common.h src/materials.h src/world.h src/render.h src/player.h src/item.h src/sprite.h src/worldgen.h src/light.h src/room.h src/projectile.h
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	$(OUT)

clean:
	@if exist build rmdir /s /q build

.PHONY: all run clean
