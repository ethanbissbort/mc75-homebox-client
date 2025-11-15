@echo off
REM Build script for Windows Mobile 6.5 target

echo ===============================================
echo  HomeBox Client - Windows Mobile 6.5 Build
echo ===============================================

set SOLUTION=mc75-homebox-client.sln
set CONFIG=Release
set PLATFORM=Windows Mobile 6.5 Professional SDK (ARMV4I)

REM Check if Visual Studio 2008 is available
if not defined VS90COMNTOOLS (
    echo ERROR: Visual Studio 2008 not found
    echo Please install Visual Studio 2008
    exit /b 1
)

REM Set up build environment
call "%VS90COMNTOOLS%\vsvars32.bat"

REM Clean previous build
echo Cleaning previous build...
devenv "%SOLUTION%" /Clean "%CONFIG%|%PLATFORM%"

REM Build solution
echo Building solution...
devenv "%SOLUTION%" /Build "%CONFIG%|%PLATFORM%"

if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD SUCCESSFUL
echo Output: bin\%CONFIG%\HBXClient.exe
echo CAB installer: bin\%CONFIG%\HBXClient.CAB
echo.
