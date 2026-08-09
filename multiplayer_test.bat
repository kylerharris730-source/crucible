@echo off
setlocal
pushd "%~dp0"

if not exist build\crucible.exe (
    echo Missing build\crucible.exe - run build.bat first.
    pause
    exit /b 1
)

REM Both peers are the exact same executable and communicate over loopback.
REM A small deterministic arena starts much faster than loading/generating a
REM full world and includes several stacks for inventory replication tests.
start "Crucible LOCAL HOST" "build\crucible.exe" --host-empty
powershell -NoProfile -Command "Start-Sleep -Seconds 5"
start "Crucible LOCAL CLIENT" "build\crucible.exe" --join 127.0.0.1

popd
