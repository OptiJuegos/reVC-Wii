@echo off

cd /d "%~dp0"

"C:\devkitPro\devkitPPC\bin\powerpc-eabi-strip.exe" --strip-all -o "build-wii\src\boot.elf" "build-wii\src\reVC.elf"
