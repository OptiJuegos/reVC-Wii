@echo off
setlocal EnableExtensions

:: Update reVC on the Wii SD for Homebrew Channel (real console).
:: HBC only launches boot.dol or boot.elf — not reVC.dol.

set "REPO=%~dp0.."
set "BUILD_DOL=%REPO%\build-wii\src\reVC.dol"
set "SD_ROOT=D:"
set "SD_APP=%SD_ROOT%\apps\reVC"
set "SD_DOL=%SD_APP%\boot.dol"

if /I "%~1"=="rebuild" goto :rebuild
goto :copy

:rebuild
echo [1/2] Building...
set "BASH=C:\msys64\usr\bin\bash.exe"
if not exist "%BASH%" set "BASH=C:\devkitPro\msys2\usr\bin\bash.exe"
if not exist "%BASH%" (
    echo ERROR: MSYS2 bash not found.
    exit /b 1
)
"%BASH%" -lc "source /etc/profile.d/devkit-env.sh; export PATH=/opt/devkitpro/portlibs/wii/bin:/opt/devkitpro/devkitPPC/bin:$PATH; cd /c/Users/vhoda/Desktop/revc-wii && powerpc-eabi-cmake -S . -B build-wii -DREVC_VENDORED_LIBRW=ON -DWII_GAME_BOOT=ON -DCMAKE_DEPENDS_USE_COMPILER=FALSE && cmake --build build-wii -j4"
if errorlevel 1 (
    echo ERROR: build failed.
    exit /b 1
)

:copy
echo [copy] %BUILD_DOL%
echo    -^> %SD_DOL%
if not exist "%BUILD_DOL%" (
    echo ERROR: missing build. Run: scripts\update-sd.bat rebuild
    exit /b 1
)
if not exist "%SD_APP%" (
    echo ERROR: %SD_APP% not found. Is the SD inserted as %SD_ROOT% ?
    exit /b 1
)

copy /Y "%BUILD_DOL%" "%SD_DOL%" >nul
if errorlevel 1 (
    echo ERROR: copy failed. Close any program using the SD and retry.
    exit /b 1
)

echo.
echo OK. Eject SD, open Homebrew Channel -^> GTA Vice City Wii.
exit /b 0
