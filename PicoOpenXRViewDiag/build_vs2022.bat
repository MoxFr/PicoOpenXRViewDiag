@echo off
setlocal
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo Built: build\Release\PicoOpenXRViewDiag.dll
