@echo off
setlocal enabledelayedexpansion
if not exist build mkdir build
if not exist build\obj mkdir build\obj

REM Every .cpp in src, discovered rather than listed.
REM
REM This was a hand-written list, and it failed the way hand-written lists do:
REM a new source file (craft.cpp) reached the harness runner and the Makefile
REM and not this, so the link came apart on undefined references. The bad part
REM was not the failure, it was the consequence -- g++ had already claimed the
REM output, so the working cinderlift.exe was destroyed and nothing replaced it.
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
set GIT_HEAD=
set SOURCE_DIRTY=
for /f %%i in ('git rev-parse --short^=12 HEAD 2^>nul') do set GIT_HEAD=%%i
for /f %%i in ('git status --porcelain --untracked-files^=normal -- src Makefile build.bat 2^>nul') do set SOURCE_DIRTY=1
if defined GIT_HEAD (
    if defined SOURCE_DIRTY (
        REM Two separately compiled dirty trees must never claim to be the same
        REM build merely because HEAD matches. A GUID is embedded once per build;
        REM copying that executable to the other PC still matches exactly.
        for /f %%i in ('powershell -NoProfile -Command "[guid]::NewGuid().ToString('N')"') do set BUILD_ID=!GIT_HEAD!-dirty-%%i
    ) else (
        set BUILD_ID=!GIT_HEAD!
    )
)

REM --- the displayed version -------------------------------------------------
REM Newest tag plus the number of commits since it, so a build can be checked
REM against what was pushed. scripts/version.sh is the reference implementation
REM and carries the reasoning; this is the same thing in cmd because cmd cannot
REM run it. Keep the two in step -- the only rule is: tag with the v stripped,
REM a dot, and `git rev-list --count <tag>..HEAD`.
set CL_TAG=
set CL_COUNT=
set CL_VERSION=unknown
for /f %%i in ('git describe --tags --abbrev^=0 2^>nul') do set CL_TAG=%%i
if defined CL_TAG (
    for /f %%i in ('git rev-list --count !CL_TAG!..HEAD 2^>nul') do set CL_COUNT=%%i
    if defined CL_COUNT (
        set CL_BASE=!CL_TAG:v=!
        set CL_VERSION=!CL_BASE!.!CL_COUNT!
    )
)
if defined SOURCE_DIRTY set CL_VERSION=!CL_VERSION!+dirty

REM --- version resource ------------------------------------------------------
REM Windows reads the publisher and product name for its "unknown publisher"
REM warning out of this. Without it those fields are blank, which is one of the
REM reasons an unsigned binary gets treated as suspicious -- see res\version.rc.
REM Numbers come from the newest tag so the metadata tracks releases; an export
REM with no Git present falls back to 0.0.0 and still builds.
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
windres res\version.rc -o build\obj\version_game.o ^
    -DVER_MAJOR=!VER_MA! -DVER_MINOR=!VER_MI! -DVER_PATCH=!VER_PA! -DVER_TARGET=1
if errorlevel 1 (
    echo.
    echo VERSION RESOURCE FAILED -- build\cinderlift.exe left as it was
    exit /b 1
)

REM Built to a scratch name and moved into place only on success, so a build
REM that fails leaves the last working game exactly where it was. g++ claims
REM its output file before it knows whether the link will succeed, so compiling
REM straight to cinderlift.exe means every failed build costs you the executable
REM you had -- which is how a missing source file turned into a deleted game.
REM Embed the MinGW C++ runtimes. Otherwise the EXE works on this development
REM machine but a copied release also requires libgcc_s_dw2-1.dll and
REM libstdc++-6.dll beside it.
REM -static, not only -static-libgcc/-static-libstdc++. The 64-bit MinGW used
REM by GitHub Actions builds libstdc++ against libwinpthread; leaving that last
REM runtime dynamic produced executables that worked on the runner and failed
REM on clean Windows installs with "libwinpthread-1.dll was not found".
g++ -std=c++11 -O2 -Wall -Wextra -mwindows -static -static-libgcc -static-libstdc++ -DCINDERLIFT_BUILD_ID=\"!BUILD_ID!\" -DCINDERLIFT_VERSION=\"!CL_VERSION!\" !SRC! build/obj/version_game.o ^
    -o build\cinderlift.new.exe ^
    -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

if errorlevel 1 (
    del /q build\cinderlift.new.exe 2>nul
    echo.
    echo BUILD FAILED -- build\cinderlift.exe left as it was
    exit /b 1
)

move /y build\cinderlift.new.exe build\cinderlift.exe >nul
if errorlevel 1 (
    echo.
    echo BUILT, BUT COULD NOT REPLACE build\cinderlift.exe -- is the game running?
    echo The new build is at build\cinderlift.new.exe
    exit /b 1
)
REM --- tidy the build folder -------------------------------------------------
REM A WHITELIST, and that inversion is the whole fix. This used to name the
REM shapes it knew about -- cinderlift.*.exe, *_test.exe, *_smoke.exe, *_soak*.exe,
REM *_mismatch*.exe, *.new.exe -- so every harness built under a name nobody had
REM thought of survived it. Measured, that is how the folder collected six
REM *chk.exe files, a live_grace.exe and a stray empty build\build directory:
REM not one of them matched a pattern, and no pattern list ever will, because
REM the names are invented one at a time by whoever is debugging.
REM
REM Inverted, there is nothing to keep up to date. The game and launcher are
REM the only executables that belong here; everything else with that extension
REM goes. Diagnostic and harness builds are meant to be disposable, so losing
REM one to a normal build is the correct outcome rather than an unfortunate
REM one -- each is a single command to rebuild. An executable that is currently
REM RUNNING simply refuses to be deleted, which is harmless and self-correcting.
REM
REM ONLY .exe is touched, and that restriction is load-bearing. Saves live in
REM this folder too -- cinderlift.sav plus the numbered slots -- so a cleanup
REM written against *.* would delete somebody's world. Never widen this.
for %%f in (build\*.exe) do (
    if /i not "%%~nxf"=="cinderlift.exe" if /i not "%%~nxf"=="cinderlift-launcher.exe" del /q "%%f" 2>nul
)
REM An empty build\build, left behind when a build ran with the working
REM directory already inside build\. rmdir WITHOUT /s, so it removes the
REM directory only when it is genuinely empty and fails harmlessly otherwise --
REM this is a tidy-up and has no business recursively deleting anything.
if exist build\build rmdir build\build 2>nul
echo Built build\cinderlift.exe
