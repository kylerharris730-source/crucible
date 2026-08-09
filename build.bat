@echo off
setlocal enabledelayedexpansion
if not exist build mkdir build

REM Every .cpp in src, discovered rather than listed.
REM
REM This was a hand-written list, and it failed the way hand-written lists do:
REM a new source file (craft.cpp) reached the harness runner and the Makefile
REM and not this, so the link came apart on undefined references. The bad part
REM was not the failure, it was the consequence -- g++ had already claimed the
REM output, so the working crucible.exe was destroyed and nothing replaced it.
REM A build script that can delete your game and produce nothing is worse than
REM one that merely fails.
REM
REM Discovered, adding a file to src\ IS adding it to the build, and the two
REM cannot drift apart again.
set SRC=
for %%f in (src\*.cpp) do set SRC=!SRC! %%f

REM Direct-IP peers reject different source revisions before exchanging a
REM world. Embedding HEAD makes that check automatic for normal builds; an
REM exported source tree without Git gets an explicit unknown id and can still
REM build, but should only connect to a copy of that same executable.
set BUILD_ID=unknown
for /f %%i in ('git rev-parse --short=12 HEAD 2^>nul') do set BUILD_ID=%%i

REM Built to a scratch name and moved into place only on success, so a build
REM that fails leaves the last working game exactly where it was. g++ claims
REM its output file before it knows whether the link will succeed, so compiling
REM straight to crucible.exe means every failed build costs you the executable
REM you had -- which is how a missing source file turned into a deleted game.
g++ -std=c++11 -O2 -Wall -Wextra -mwindows -DCRUCIBLE_BUILD_ID=\"!BUILD_ID!\" !SRC! ^
    -o build\crucible.new.exe ^
    -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

if errorlevel 1 (
    del /q build\crucible.new.exe 2>nul
    echo.
    echo BUILD FAILED -- build\crucible.exe left as it was
    exit /b 1
)

move /y build\crucible.new.exe build\crucible.exe >nul
if errorlevel 1 (
    echo.
    echo BUILT, BUT COULD NOT REPLACE build\crucible.exe -- is the game running?
    echo The new build is at build\crucible.new.exe
    exit /b 1
)
REM Direct diagnostic builds use either the `crucible.<purpose>.exe` or
REM `<name>_test.exe` convention. Failed replacement builds can also leave a
REM `<name>.new.exe`. None are saves or the main executable, so a successful
REM normal build is the safe moment to clear that scoped clutter.
for %%f in (build\crucible.*.exe) do del /q "%%f" 2>nul
for %%f in (build\*_test.exe build\*_smoke.exe build\*.new.exe) do del /q "%%f" 2>nul
echo Built build\crucible.exe
