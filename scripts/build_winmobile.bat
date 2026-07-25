@echo off
REM ---------------------------------------------------------------------------
REM Build the MC75 HomeBox client for Windows Mobile 6.5 Professional (ARMV4I).
REM
REM Usage:  build_winmobile.bat [Debug^|Release]        (default: Release)
REM
REM Every path is derived from this script's own location (%~dp0), so the script
REM produces the same result whether it is started from the repository root,
REM from scripts\, or from a shortcut with an unrelated working directory.
REM ---------------------------------------------------------------------------
setlocal

REM %%~fI collapses the "scripts\.." hop so the paths we echo are readable.
for %%I in ("%~dp0..") do set "REPO_ROOT=%%~fI"

set "SOLUTION=%REPO_ROOT%\mc75-homebox-client.sln"
set "PLATFORM=Windows Mobile 6.5 Professional SDK (ARMV4I)"

REM CAB_NAME follows the CabOutputFile names in proj\HBXClientCab.vcproj, which
REM give the Debug package a distinct name so a debug build cannot be mistaken
REM for a shippable one on the device.
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

echo ===============================================
echo  HomeBox Client - Windows Mobile 6.5 Build
echo ===============================================
echo Repository : %REPO_ROOT%
echo Config     : %CONFIG%
echo.

if not exist "%SOLUTION%" (
    echo ERROR: Solution not found: %SOLUTION%
    exit /b 1
)

REM Check if Visual Studio 2008 is available
if not defined VS90COMNTOOLS (
    echo ERROR: Visual Studio 2008 not found
    echo Please install Visual Studio 2008
    exit /b 1
)

REM HBXClient.vcproj resolves the SDK and EMDK through these two environment
REM variables instead of hard-coded absolute paths, so a non-default install
REM only has to be declared here (or system-wide, for the VS2008 IDE).
if not defined WINDOWSMOBILE65SDK set "WINDOWSMOBILE65SDK=C:\Program Files\Windows Mobile 6.5 SDK"
if not defined ZEBRAEMDK set "ZEBRAEMDK=C:\Program Files\Zebra Technologies\EMDK-C"

if not exist "%WINDOWSMOBILE65SDK%\PocketPC\Include\Armv4i" (
    echo WARNING: Windows Mobile 6.5 SDK headers not found under:
    echo          %WINDOWSMOBILE65SDK%
    echo          Set WINDOWSMOBILE65SDK to your SDK install directory.
)
if not exist "%ZEBRAEMDK%\Include" (
    echo WARNING: Zebra EMDK for C headers not found under:
    echo          %ZEBRAEMDK%
    echo          Set ZEBRAEMDK to your EMDK install directory, or build
    echo          without HBX_USE_EMDK - see docs\SCANNING.md.
)

REM Set up build environment. VS90COMNTOOLS ends with a backslash on every
REM supported install; the extra separator is harmless if it ever does not.
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
echo Output: %REPO_ROOT%\bin\%CONFIG%\HBXClient.exe
echo CAB installer: %REPO_ROOT%\bin\%CONFIG%\%CAB_NAME%
echo.
echo Deploy with: "%~dp0deploy_to_device.bat" %CONFIG%
echo.
