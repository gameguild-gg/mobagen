@echo off
REM Double-click this to run every core demo and KEEP the window open.
REM (The demos are console programs; double-clicking the .exe directly just
REM  flashes a window shut because it prints and exits.)
cd /d "%~dp0.."
set BIN=build\native\bin\Release

for %%D in (ecs_demo world_demo reactive_demo messaging_demo scene_demo ^
            jobs_demo jobs_c_demo systems_demo engine_demo net_demo ^
            volume_io_test coro_demo fiber_demo jobs_bench) do (
    echo.
    echo ===================== %%D =====================
    if exist "%BIN%\%%D.exe" (
        "%BIN%\%%D.exe"
    ) else (
        echo [missing] %BIN%\%%D.exe  -- build first:  cmake --build build\native --config Release
    )
)

echo.
echo ===================== all demos finished =====================
pause
