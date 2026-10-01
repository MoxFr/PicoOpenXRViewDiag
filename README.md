# PicoOpenXRViewDiag

Read-only OpenXR diagnostic API layer for Windows x64. It logs `xrLocateViews` and projection layers passed to `xrEndFrame` so LEFT/RIGHT pose, FOV, display time, swapchain and imageArrayIndex can be compared.

## Build without Visual Studio installed locally
1. Create a GitHub repository and upload this entire folder, including `.github/workflows/build-windows-x64.yml`.
2. Open **Actions > Build Windows x64 > Run workflow**.
3. When the run finishes, download the artifact **PicoOpenXRViewDiag-Windows-x64**.

## Test
1. Extract the artifact ZIP to a permanent folder.
2. Run `Enable-PicoDiag.bat`.
3. Completely restart PICO Connect / Steam / the launcher so they inherit the new environment variables.
4. Start the OpenXR VR mod and reproduce the bad-eye/double-image problem.
5. Run `Show-Log.bat` or open `%TEMP%\PicoOpenXRViewDiag.log`.
6. Run `Disable-PicoDiag.bat` after the test and restart the launchers.

The layer does not modify poses, FOVs, swapchains or frame data. It only logs them.
