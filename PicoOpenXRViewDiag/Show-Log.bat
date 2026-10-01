@echo off
if exist "%TEMP%\PicoOpenXRViewDiag.log" (
  notepad "%TEMP%\PicoOpenXRViewDiag.log"
) else (
  echo No log found at: %TEMP%\PicoOpenXRViewDiag.log
  echo The layer may not have been loaded by the OpenXR application.
  pause
)
