@echo off
setlocal
if not exist build mkdir build

g++ -std=c++11 -O2 -Wall -Wextra -mwindows ^
    src\common.cpp src\materials.cpp src\world.cpp src\render.cpp src\player.cpp src\item.cpp src\sprite.cpp src\worldgen.cpp src\light.cpp src\room.cpp src\door.cpp src\device.cpp src\projectile.cpp src\main.cpp ^
    -o build\crucible.exe ^
    -lgdi32 -luser32 -lwinmm -lmsimg32

if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)
echo Built build\crucible.exe
