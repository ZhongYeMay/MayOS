@echo off
echo Building and running MayOS...
echo.

python build.py
if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Starting MayOS...
echo.

"D:\Program Files\qemu\qemu-system-i386.exe" -fda mayos.img -m 16