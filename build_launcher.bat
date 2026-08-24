@echo off
setlocal
if not exist build mkdir build

REM The launcher has no runtime files and is safe to distribute by itself. It
REM installs the game into %%LOCALAPPDATA%%\Crucible on first launch.
g++ -std=c++11 -O2 -Wall -Wextra -mwindows -static-libgcc -static-libstdc++ ^
    launcher\main.cpp -o build\crucible-launcher.new.exe ^
    -lwininet -ladvapi32 -lshell32 -lgdi32 -luser32
if errorlevel 1 (
    del /q build\crucible-launcher.new.exe 2>nul
    echo LAUNCHER BUILD FAILED -- existing launcher left untouched
    exit /b 1
)
move /y build\crucible-launcher.new.exe build\crucible-launcher.exe >nul
if errorlevel 1 (
    echo BUILT, BUT COULD NOT REPLACE build\crucible-launcher.exe
    exit /b 1
)
echo Built build\crucible-launcher.exe
