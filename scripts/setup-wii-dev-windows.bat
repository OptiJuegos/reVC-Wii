@echo off
setlocal

:: Prefer the official graphical installer path (C:\devkitPro).
:: Fall back to a stock MSYS2 install that has the dkp repositories configured.
set "PACMAN="
set "BASH="
if exist "C:\devkitPro\msys2\usr\bin\pacman.exe" (
    set "PACMAN=C:\devkitPro\msys2\usr\bin\pacman.exe"
    set "BASH=C:\devkitPro\msys2\usr\bin\bash.exe"
) else if exist "C:\msys64\usr\bin\pacman.exe" (
    set "PACMAN=C:\msys64\usr\bin\pacman.exe"
    set "BASH=C:\msys64\usr\bin\bash.exe"
)

if "%PACMAN%"=="" (
    echo Error: pacman not found.
    echo Install the official toolchain from https://devkitpro.org/wiki/Getting_Started
    echo or MSYS2 from https://www.msys2.org/ and add the dkp repositories.
    exit /b 1
)

echo Using: %PACMAN%
"%PACMAN%" -Syu --needed --noconfirm wii-dev
if errorlevel 1 exit /b 1
"%PACMAN%" -S --needed --noconfirm ppc-mpg123
if errorlevel 1 exit /b 1
:: Host-side tools used by scripts/build-wii.sh (powerpc-eabi-cmake wraps cmake)
"%PACMAN%" -S --needed --noconfirm cmake make
if errorlevel 1 exit /b 1

echo.
echo Done. Open MSYS2 /devkitPro MSYS and run scripts/build-wii.sh
echo Example:
echo   %BASH% -lc "source /etc/profile.d/devkit-env.sh; cd /c/Users/%USERNAME%/Desktop/revc-wii; ./scripts/build-wii.sh"
exit /b 0
