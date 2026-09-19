@echo off
setlocal enabledelayedexpansion
if not exist build mkdir build
if not exist build\obj mkdir build\obj

REM ===========================================================================
REM powderlike -- the sandbox front-end over the same simulation.
REM
REM Not part of a release. There is no entry for it in the launcher, nothing in
REM the GitHub workflow builds it, and that is deliberate: it is a second window
REM onto the physics for looking at the physics, not a product with its own
REM version to support.
REM
REM Every .cpp in src EXCEPT src\main.cpp, plus src\powder\main.cpp. That is the
REM same shape the test suite uses (see scripts\run_tests.sh) and for the same
REM reason: one main() per binary, everything else shared. Discovered rather
REM than listed -- adding a file to src\ adds it here too, which is the rule
REM build.bat already explains at length and which has already cost this project
REM a broken link once.
REM ===========================================================================
set SRC=
for %%f in (src\*.cpp) do (
    if /i not "%%~nxf"=="main.cpp" set SRC=!SRC! %%f
)
set SRC=!SRC! src\powder\main.cpp

REM network.cpp reads CINDERLIFT_BUILD_ID and version.h reads CINDERLIFT_VERSION.
REM powderlike never opens a socket, but it links the file, so both have to be
REM defined or the compile fails on a missing macro.
set BUILD_ID=powderlike
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
echo Version !CL_VERSION!

REM Built to a scratch name and moved into place only on success, for the reason
REM build.bat gives: g++ claims its output before it knows the link will work,
REM so compiling straight to the real name means a failed build costs you the
REM executable you had.
g++ -std=c++11 -O3 -Wall -Wextra -mwindows -static -static-libgcc -static-libstdc++ ^
    -DCINDERLIFT_BUILD_ID=\"!BUILD_ID!\" -DCINDERLIFT_VERSION=\"!CL_VERSION!\" ^
    !SRC! -o build\powderlike.new.exe ^
    -lgdi32 -luser32 -lwinmm -lmsimg32 -lws2_32

if errorlevel 1 (
    del /q build\powderlike.new.exe 2>nul
    echo.
    echo BUILD FAILED -- build\powderlike.exe left as it was
    exit /b 1
)

move /y build\powderlike.new.exe build\powderlike.exe >nul
if errorlevel 1 (
    echo.
    echo BUILT, BUT COULD NOT REPLACE build\powderlike.exe -- is it running?
    echo The new build is at build\powderlike.new.exe
    exit /b 1
)
echo Built build\powderlike.exe
