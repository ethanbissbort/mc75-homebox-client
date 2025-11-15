@echo off
REM Deploy CAB installer to connected MC75 device

echo ===============================================
echo  HomeBox Client - Device Deployment
echo ===============================================

set CAB_FILE=..\bin\Release\HBXClient.CAB
set DEVICE_PATH=\Temp\HBXClient.CAB

if not exist "%CAB_FILE%" (
    echo ERROR: CAB file not found: %CAB_FILE%
    echo Please build the project first
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

echo.
echo CAB file copied to device: %DEVICE_PATH%
echo Please install the CAB on your MC75 device
echo.
