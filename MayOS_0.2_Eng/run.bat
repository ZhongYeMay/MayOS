@echo off
title MayOS v0.2 Test Runner

echo ===============================================
echo           MayOS v0.2 Test Runner
echo ===============================================
echo.

:: 检查镜像文件是否存在
if not exist "mayos_final.img" (
    echo [INFO] Building MayOS image...
    docker run --rm -v "%cd%":/mayos mayos-builder nasm -f bin final.asm -o mayos_final.img
    if errorlevel 1 (
        echo [ERROR] Build failed!
        pause
        exit /b 1
    )
    echo [SUCCESS] Build completed!
    echo.
)

:: 查找QEMU
echo [INFO] Searching for QEMU...
set QEMU_PATH=

:: 常见安装路径
if exist "D:\Program Files\qemu\qemu-system-i386.exe" set QEMU_PATH="D:\Program Files\qemu\qemu-system-i386.exe"
if exist "C:\Program Files\qemu\qemu-system-i386.exe" set QEMU_PATH="C:\Program Files\qemu\qemu-system-i386.exe"
if exist "C:\Program Files (x86)\qemu\qemu-system-i386.exe" set QEMU_PATH="C:\Program Files (x86)\qemu\qemu-system-i386.exe"
if exist "%USERPROFILE%\qemu\qemu-system-i386.exe" set QEMU_PATH="%USERPROFILE%\qemu\qemu-system-i386.exe"
if exist "qemu\qemu-system-i386.exe" set QEMU_PATH="qemu\qemu-system-i386.exe"

:: 检查PATH环境变量
where qemu-system-i386.exe >nul 2>&1
if %errorlevel% == 0 set QEMU_PATH=qemu-system-i386.exe

:: 如果找到QEMU，直接运行
if defined QEMU_PATH (
    echo [SUCCESS] Found QEMU at %QEMU_PATH%
    goto :run_mayos
)

:: 没找到QEMU，询问是否下载
echo [WARNING] QEMU not found on this system
echo.
echo QEMU is required to run MayOS. Would you like to download it?
echo [1] Download QEMU portable version (recommended)
echo [2] Open QEMU download page in browser
echo [3] Exit
echo.
set /p choice="Enter your choice (1-3): "

if "%choice%"=="1" goto :download_qemu
if "%choice%"=="2" goto :open_browser
if "%choice%"=="3" goto :exit
goto :exit

:download_qemu
echo.
echo [INFO] Downloading QEMU portable version...
echo [INFO] This may take a few minutes...

:: 创建qemu目录
if not exist "qemu" mkdir qemu
cd qemu

:: 下载QEMU (使用PowerShell)
powershell -Command "& {[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://qemu.weilnetz.de/w64/qemu-w64-setup-20231009.exe' -OutFile 'qemu-installer.exe'}"

if not exist "qemu-installer.exe" (
    echo [ERROR] Download failed! Please check your internet connection.
    cd ..
    pause
    exit /b 1
)

echo [INFO] Installing QEMU...
:: 静默安装到当前目录
qemu-installer.exe /S /D=%cd%

:: 等待安装完成
timeout /t 10 /nobreak >nul

:: 检查安装是否成功
if exist "qemu-system-i386.exe" (
    echo [SUCCESS] QEMU installed successfully!
    set QEMU_PATH="qemu\qemu-system-i386.exe"
    cd ..
    goto :run_mayos
) else (
    echo [ERROR] QEMU installation failed!
    cd ..
    pause
    exit /b 1
)

:open_browser
echo [INFO] Opening QEMU download page...
start https://www.qemu.org/download/#windows
echo Please download and install QEMU, then run this script again.
pause
exit /b 0

:run_mayos
echo.
echo [INFO] Starting MayOS v0.2...
echo [INFO] Press Ctrl+Alt+G to release mouse cursor
echo [INFO] Press Ctrl+Alt+F to toggle fullscreen
echo [INFO] Close QEMU window to return to this script
echo.

:: 启动QEMU
%QEMU_PATH% -fda mayos_final.img -m 16M

echo.
echo [INFO] MayOS session ended
pause
exit /b 0

:exit
echo [INFO] Exiting...
pause
exit /b 0