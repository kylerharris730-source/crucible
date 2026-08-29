@echo off
setlocal
pushd "%~dp0"

if not exist build\cinderlift.exe (
    echo Missing build\cinderlift.exe - run build.bat first.
    pause
    exit /b 1
)

REM Number of joined clients to launch. The host fills slot zero, so the
REM maximum is three for a four-player game. Default is one, which is the
REM everyday iteration case; pass 3 to exercise a full session.
set CLIENTS=%1
if "%CLIENTS%"=="" set CLIENTS=1

REM Both peers are the exact same executable and communicate over loopback.
REM A small deterministic arena starts much faster than loading/generating a
REM full world and includes several stacks for inventory replication tests.
start "Cinderlift LOCAL HOST" "build\cinderlift.exe" --host-empty
powershell -NoProfile -Command "Start-Sleep -Seconds 5"

for /l %%i in (1,1,%CLIENTS%) do (
    REM --label only changes the window caption. Several local clients are
    REM otherwise indistinguishable on one desktop.
    start "Cinderlift LOCAL CLIENT %%i" "build\cinderlift.exe" --join 127.0.0.1 --label %%i
    powershell -NoProfile -Command "Start-Sleep -Seconds 3"
)

popd
