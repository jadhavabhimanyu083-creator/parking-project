@echo off
title ParkingFlow - C++17 Authoritative Engine
echo =================================================================
echo   Building ParkingFlow - Smart Parking Slot Management System
echo   Second-Year Engineering DSA Project
echo =================================================================
echo.
echo [1/2] Compiling C++17 native backend with MinGW g++...
g++ -std=c++17 -O2 backend\src\main.cpp backend\src\HttpServer.cpp backend\src\ParkingManager.cpp -o parkingflow_server.exe -lws2_32

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Compilation failed! Please ensure MinGW g++ is in your PATH.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Launching Authoritative Server on port 8080...
echo Open http://localhost:8080 in your web browser.
echo.
parkingflow_server.exe 8080 public
