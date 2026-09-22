@echo off
REM ==========================================================================
REM toolchain.bat -- put a new enough g++ first on PATH, or fail.
REM
REM Called (not run) by build.bat and scripts\build_deps.bat, so the PATH it
REM sets is theirs. Online play links libdatachannel, which is C++17 and
REM needs GCC 7 or newer; the old 32-bit MinGW.org GCC 6.3 in C:\MinGW cannot
REM build it. A WinLibs MinGW-w64 installed through winget is put in front of
REM it when present:
REM
REM     winget install BrechtSanders.WinLibs.POSIX.UCRT --version 14.2.0-12.0.0-r2
REM
REM CI installs a MinGW-w64 through Chocolatey and has nothing to find here.
REM
REM --- and not GCC 16 ----------------------------------------------------------
REM GCC 16.1 miscompiles libdatachannel. Built with it, a data channel that
REM closes while data is queued frees its SCTP transport while another thread
REM still holds a reference to it, and the game crashes a few seconds after a
REM player joins or leaves. Isolated with a standalone test using nothing but
REM libdatachannel's C API: GCC 16.1 crashed or failed to connect in every
REM run, GCC 14.2 passed ninety rounds out of ninety on the same sources. So
REM 16 is refused outright rather than allowed to produce a game that crashes
REM online. Try a later 16.x before lifting this, with that test.
REM ==========================================================================
for /d %%d in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT*") do (
    if exist "%%d\mingw64\bin\g++.exe" call set "PATH=%%d\mingw64\bin;%%PATH%%"
)
set GCC_MAJOR=0
for /f "tokens=1 delims=." %%v in ('g++ -dumpversion 2^>nul') do set GCC_MAJOR=%%v
if %GCC_MAJOR% LSS 7 (
    echo.
    echo g++ is version %GCC_MAJOR% -- online play needs GCC 7 or newer. Install one with:
    echo     winget install BrechtSanders.WinLibs.POSIX.UCRT --version 14.2.0-12.0.0-r2
    exit /b 1
)
if %GCC_MAJOR% EQU 16 (
    echo.
    echo g++ is version 16, which miscompiles the online-play libraries -- see
    echo scripts\toolchain.bat. Install GCC 14 instead:
    echo     winget install BrechtSanders.WinLibs.POSIX.UCRT --version 14.2.0-12.0.0-r2
    exit /b 1
)
exit /b 0
