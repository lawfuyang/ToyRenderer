@echo off
setlocal enabledelayedexpansion

:: Change to the directory containing the batch file
cd /d "%~dp0"

:: Check if CMake is installed
where cmake >nul 2>&1

if %errorlevel% neq 0 (
    echo ERROR: CMake is not installed.
    exit /b 1
)

:: cmake call
cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -S "%cd%" -B "%cd%\build"

echo:
pause