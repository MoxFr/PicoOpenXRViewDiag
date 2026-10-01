@echo off
setlocal
set "LAYERDIR=%~dp0build\Release"
set "XR_API_LAYER_PATH=%LAYERDIR%"
set "XR_ENABLE_API_LAYERS=XR_APILAYER_MOX_pico_view_diag"
echo Layer enabled for programs launched from THIS window only.
echo Log: %%TEMP%%\PicoOpenXRViewDiag.log
echo.
echo Launch your OpenXR game/mod from this command prompt.
cmd /k
