@echo off
setlocal
set "OUT=D:\Projects\Build Cache\Hulk Recompiled\release"
if not exist "%OUT%\HulkRecompiled.exe" (
  echo Build the project with build.bat first.
  exit /b 1
)
set HULK_TEST_INPUT=
set HULK_CAPTURE_DELAY_MS=
set HULK_CAPTURE_INTERVAL_MS=
set HULK_CAPTURE_COUNT=
set XBE_CAPTURE_DIR=
set XBE_PERF=
start "Hulk Recompiled" /d "%OUT%" "%OUT%\HulkRecompiled.exe"
