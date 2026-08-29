@echo off
setlocal
if not exist build mkdir build

REM The launcher has no runtime files and is safe to distribute by itself. Use
REM full static runtime linking: GitHub's 64-bit MinGW otherwise leaves a hidden
REM libwinpthread-1.dll dependency even with static-libgcc/static-libstdc++.
REM That dependency exists on the CI runner but not on an ordinary Windows PC.
g++ -std=c++11 -O2 -Wall -Wextra -mwindows -static -static-libgcc -static-libstdc++ ^
    launcher\main.cpp -o build\cinderlift-launcher.new.exe ^
    -lwininet -ladvapi32 -lshell32 -lgdi32 -luser32
if errorlevel 1 (
    del /q build\cinderlift-launcher.new.exe 2>nul
    echo LAUNCHER BUILD FAILED -- existing launcher left untouched
    exit /b 1
)
move /y build\cinderlift-launcher.new.exe build\cinderlift-launcher.exe >nul
if errorlevel 1 (
    echo BUILT, BUT COULD NOT REPLACE build\cinderlift-launcher.exe
    exit /b 1
)
echo Built build\cinderlift-launcher.exe
