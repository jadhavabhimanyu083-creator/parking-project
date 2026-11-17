@echo off
title ParkingFlow System Launcher
echo ========================================================
echo       Starting ParkingFlow Smart Parking System
echo ========================================================
echo.

:: Check if server is already running
tasklist /FI "IMAGENAME eq parkingflow_server.exe" 2>NUL | find /I /N "parkingflow_server.exe">NUL
if "%ERRORLEVEL%"=="0" (
    echo [OK] Backend server is already running on port 8080.
) else (
    echo [*] Starting C++ Authoritative Backend Server on port 8080...
    start "" "%~dp0parkingflow_server.exe" 8080 public
    timeout /t 1 /nobreak >nul
)

echo [*] Opening browser to http://localhost:8080 ...
start http://localhost:8080

echo.
echo [SUCCESS] ParkingFlow is now open!
echo You can also open public\index.html directly with any browser.
echo.
pause
