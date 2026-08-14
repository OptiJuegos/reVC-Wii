@echo off

cd /d "%~dp0"

:: esto es lo mas negro que hice hoy un batch para esto
"C:\devkitPro\msys2\usr\bin\pacman.exe" -S --needed --noconfirm ppc-mpg123
pause