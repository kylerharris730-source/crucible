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

REM Built to a scratch name and moved into place only on success, so a build
REM that fails leaves the last working game exactly where it was. g++ claims
REM its output file before it knows whether the link will succeed, so compiling
REM straight to crucible.exe means every failed build costs you the executable
REM you had -- which is how a missing source file turned into a deleted game.
REM Embed the MinGW C++ runtimes. Otherwise the EXE works on this development
REM machine but a copied release also requires libgcc_s_dw2-1.dll and
REM libstdc++-6.dll beside it.
g++ -std=c++11 -O2 -Wall -Wextra -mwindows -static-libgcc -static-libstdc++ -DCRUCIBLE_BUILD_ID=\"!BUILD_ID!\" !SRC! ^
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
REM --- tidy the build folder -------------------------------------------------
REM A WHITELIST, and that inversion is the whole fix. This used to name the
REM shapes it knew about -- crucible.*.exe, *_test.exe, *_smoke.exe, *_soak*.exe,
REM *_mismatch*.exe, *.new.exe -- so every harness built under a name nobody had
REM thought of survived it. Measured, that is how the folder collected six
REM *chk.exe files, a live_grace.exe and a stray empty build\build directory:
REM not one of them matched a pattern, and no pattern list ever will, because
REM the names are invented one at a time by whoever is debugging.
REM
REM Inverted, there is nothing to keep up to date. build\crucible.exe is the
REM only executable that belongs here and everything else with that extension
REM goes. Diagnostic and harness builds are meant to be disposable, so losing
REM one to a normal build is the correct outcome rather than an unfortunate
REM one -- each is a single command to rebuild. An executable that is currently
REM RUNNING simply refuses to be deleted, which is harmless and self-correcting.
REM
REM ONLY .exe is touched, and that restriction is load-bearing. Saves live in
REM this folder too -- crucible.sav plus the numbered slots -- so a cleanup
REM written against *.* would delete somebody's world. Never widen this.
for %%f in (build\*.exe) do (
    if /i not "%%~nxf"=="crucible.exe" del /q "%%f" 2>nul
)
REM An empty build\build, left behind when a build ran with the working
REM directory already inside build\. rmdir WITHOUT /s, so it removes the
REM directory only when it is genuinely empty and fails harmlessly otherwise --
REM this is a tidy-up and has no business recursively deleting anything.
if exist build\build rmdir build\build 2>nul
echo Built build\crucible.exe
