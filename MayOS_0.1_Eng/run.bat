@echo off
echo ================================
echo      MayOS ver0.1
echo   Simple Operating System
echo ================================
echo.
echo Features:
echo - Interactive command line
echo - Built-in commands: help, clear, info, time, reboot
echo - Real-time clock
echo - System information
echo.
echo Instructions:
echo - Type "help" to see available commands
echo - Type "time" to see current time
echo - Type "reboot" to restart
echo - Close QEMU window to exit
echo.
pause

"D:\Program Files\qemu\qemu-system-i386.exe" -fda mayos.img -m 16

echo.
echo MayOS session ended
pause