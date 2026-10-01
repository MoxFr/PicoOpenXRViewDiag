@echo off
setlocal
set "LAYERDIR=%~dp0"
setx XR_API_LAYER_PATH "%LAYERDIR%" >nul
setx XR_ENABLE_API_LAYERS "XR_APILAYER_MOX_pico_view_diag" >nul
echo PicoOpenXRViewDiag enabled for NEW processes.
echo Restart Steam/PICO Connect/game launchers before testing.
echo Log: %%TEMP%%\PicoOpenXRViewDiag.log
pause
