@echo off
REM ---------------------------------------------------------------------------
REM Copy the CAB installer to a connected MC75 over ActiveSync / WMDC.
REM
REM Usage:  deploy_to_device.bat [Debug^|Release]        (default: Release)
REM
REM The CAB is located relative to this script (%~dp0), not to the caller's
REM working directory, so the script works from the repository root as well as
REM from scripts\.
REM ---------------------------------------------------------------------------
setlocal

for %%I in ("%~dp0..") do set "REPO_ROOT=%%~fI"

REM CAB_NAME matches the CabOutputFile names in proj\HBXClientCab.vcproj.
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Release"
if /I "%CONFIG%"=="Debug" goto :cfg_debug
if /I "%CONFIG%"=="Release" goto :cfg_release
echo ERROR: unknown configuration "%CONFIG%"
echo Usage: %~nx0 [Debug^|Release]
exit /b 1

:cfg_debug
set "CONFIG=Debug"
set "CAB_NAME=HBXClient_Debug.CAB"
goto :cfg_done

:cfg_release
set "CONFIG=Release"
set "CAB_NAME=HBXClient.CAB"
goto :cfg_done

:cfg_done

set "CAB_FILE=%REPO_ROOT%\bin\%CONFIG%\%CAB_NAME%"
set "DEVICE_PATH=\Temp\%CAB_NAME%"

echo ===============================================
echo  HomeBox Client - Device Deployment
echo ===============================================
echo Config : %CONFIG%
echo CAB    : %CAB_FILE%
echo.

if not exist "%CAB_FILE%" (
    echo ERROR: CAB file not found: %CAB_FILE%
    echo Please build the project first: "%~dp0build_winmobile.bat" %CONFIG%
    exit /b 1
)

echo Checking for connected device...
rapiconfig /s >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: No device connected or ActiveSync not running
    echo Please connect your MC75 device and ensure ActiveSync is running
    exit /b 1
)

echo Copying CAB to device...
cecopy "%CAB_FILE%" "%DEVICE_PATH%"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Copy failed - the CAB was NOT transferred
    exit /b 1
)

echo.
echo CAB file copied to device: %DEVICE_PATH%
echo Please install the CAB on your MC75 device
echo.
