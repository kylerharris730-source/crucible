@echo off
setlocal
if not exist build mkdir build

g++ -std=c++11 -O2 -Wall -Wextra -mwindows ^
    src\common.cpp src\materials.cpp src\world.cpp src\render.cpp src\player.cpp src\main.cpp ^
    -o build\powder.exe ^
    -lgdi32 -luser32 -lwinmm

if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)
echo Built build\powder.exe
