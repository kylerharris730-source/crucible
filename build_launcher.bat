@echo off
setlocal enabledelayedexpansion
if not exist build mkdir build
if not exist build\obj mkdir build\obj

REM --- version resource ------------------------------------------------------
REM This one matters more than the game's. The launcher is what was reported
REM rejected as malware, and it is the more suspicious of the two by nature:
REM it downloads an executable, writes it under %LOCALAPPDATA%, replaces its
REM own binary and runs what it fetched. That is a legitimate updater and also
REM exactly the shape of a dropper, so the only thing separating them, absent
REM a signature, is whether the file says who and what it is. See res\version.rc.
set VER_TAG=
for /f %%i in ('git describe --tags --abbrev^=0 2^>nul') do set VER_TAG=%%i
set VER_MA=0
set VER_MI=0
set VER_PA=0
if defined VER_TAG (
    set VER_NUM=!VER_TAG:v=!
    for /f "tokens=1,2,3 delims=." %%a in ("!VER_NUM!") do (
        set VER_MA=%%a
        set VER_MI=%%b
        set VER_PA=%%c
    )
)
if "!VER_MA!"=="" set VER_MA=0
if "!VER_MI!"=="" set VER_MI=0
if "!VER_PA!"=="" set VER_PA=0
windres res\version.rc -o build/obj/version_launcher.o ^
    -DVER_MAJOR=!VER_MA! -DVER_MINOR=!VER_MI! -DVER_PATCH=!VER_PA! -DVER_TARGET=2
if errorlevel 1 (
    echo VERSION RESOURCE FAILED -- existing launcher left untouched
    exit /b 1
)

REM The launcher has no runtime files and is safe to distribute by itself. Use
REM full static runtime linking: GitHub's 64-bit MinGW otherwise leaves a hidden
REM libwinpthread-1.dll dependency even with static-libgcc/static-libstdc++.
REM That dependency exists on the CI runner but not on an ordinary Windows PC.
g++ -std=c++11 -O2 -Wall -Wextra -mwindows -static -static-libgcc -static-libstdc++ ^
    launcher\main.cpp build/obj/version_launcher.o -o build\cinderlift-launcher.new.exe ^
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
