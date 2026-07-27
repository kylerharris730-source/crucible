CXX      := g++
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra
LDFLAGS  := -mwindows -lgdi32 -luser32 -lwinmm

SRC := src/common.cpp src/materials.cpp src/world.cpp src/render.cpp src/player.cpp src/item.cpp src/main.cpp
OUT := build/powder.exe

all: $(OUT)

$(OUT): $(SRC) src/common.h src/materials.h src/world.h src/render.h src/player.h src/item.h
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	$(OUT)

clean:
	@if exist build rmdir /s /q build

.PHONY: all run clean
