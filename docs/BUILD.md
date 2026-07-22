# 🔨 Build Guide

> **Complete build instructions for MC75 HomeBox Client**

> 🆕 **Setting up a fresh Windows 7 machine from scratch?** See the step‑by‑step
> [Windows 7 Build Guide](WINDOWS7_BUILD.md) — it walks through installing
> Visual Studio 2008, the Windows Mobile 6/6.5 SDKs, and every dependency in the
> correct order, and covers the project‑platform retargeting gotcha.

---

## 📋 Table of Contents

- [Build Environment](#-build-environment)
- [Prerequisites](#-prerequisites)
- [Solution Structure](#-solution-structure)
- [Build Configurations](#-build-configurations)
- [Building the Application](#-building-the-application)
- [Build Scripts](#-build-scripts)
- [Troubleshooting](#-troubleshooting)
- [Output Files](#-output-files)

---

## 🖥️ Build Environment

### Required Software

| Software | Version | Purpose |
|----------|---------|---------|
| 🪟 **Windows** | XP/Vista/7/10 | Host development environment |
| 🔧 **Visual Studio** | 2008 Professional+ | C++ compiler and IDE |
| 📱 **Windows Mobile SDK** | 6.5 Professional | Target platform SDK |
| 📡 **Zebra EMDK** | Latest for C/C++ | Scanner hardware support |
| 🔌 **ActiveSync/WMDC** | Latest | Device connectivity |

### 📥 Installation Order

1. **Visual Studio 2008**
   ```
   Install with C++ development tools
   Select "Smart Device Development" workload
   ```

2. **Windows Mobile 6.5 Professional SDK**
   ```
   Download from Microsoft
   Run SDK installer
   Verify SDK appears in VS2008 platform dropdown
   ```

3. **Zebra EMDK for C/C++**
   ```
   Download from Zebra Developer Portal
   Install to default location
   Note: Include paths will be referenced in project
   ```

4. **ActiveSync or Windows Mobile Device Center**
   ```
   For device deployment and debugging
   Test connection with MC75 device
   ```

---

## ✅ Prerequisites

### System Requirements

- 💾 **Disk Space**: 5 GB free (for VS2008 + SDKs)
- 🧠 **RAM**: 2 GB minimum, 4 GB recommended
- 💻 **Processor**: x86 or x64 compatible
- 🌐 **Internet**: For SDK downloads

### SDK Paths

Verify these paths exist after SDK installation:

```
✅ C:\Program Files\Windows Mobile 6.5 SDK\
✅ C:\Program Files\Zebra Technologies\EMDK-C\
✅ C:\Program Files\Microsoft Visual Studio 9.0\
```

### Environment Variables

Set if not automatically configured:

```batch
set WINDOWSMOBILE65SDK=C:\Program Files\Windows Mobile 6.5 SDK
set ZEBRAEMDK=C:\Program Files\Zebra Technologies\EMDK-C
```

---

## 🏗️ Solution Structure

### Visual Studio Solution

**File**: `proj/mc75-homebox-client.sln`

```
mc75-homebox-client.sln
├── 📦 HBXClient (Main executable project)
└── 📦 HBXClientCab (CAB installer project)
```

### Project: HBXClient

**Type**: Smart Device Application (C++)
**Output**: `HBXClient.exe`
**Platform**: Windows Mobile 6.5 Professional SDK (ARMV4I)

#### Source Files

```cpp
// Core
src/main.cpp                  // Entry point
src/Controller.cpp            // Application controller
src/Config.cpp               // Configuration manager

// Networking
src/HttpClient.cpp           // HTTP client
src/HbClient.cpp            // HomeBox API client

// Synchronization
src/SyncEngine.cpp          // Sync manager
src/Journal.cpp             // Transaction journal

// Hardware
src/ScannerHAL.cpp          // Scanner abstraction

// Views
src/Views/ScanView.cpp      // Scan interface
src/Views/ItemView.cpp      // Item editor
src/Views/QueueView.cpp     // Queue manager
src/Views/ViewHelpers.cpp   // UI utilities

// Models
src/Models/Item.cpp         // Item model
src/Models/Location.cpp     // Location model
src/Models/JsonLite.cpp     // JSON parser
```

#### Include Directories

```
../include
$(WINDOWSMOBILE65SDK)\Include\ARMV4I
$(ZEBRAEMDK)\Include
```

#### Library Directories

```
$(WINDOWSMOBILE65SDK)\Lib\ARMV4I
$(ZEBRAEMDK)\Lib\ARMV4I
```

#### Linked Libraries

```
coredll.lib      // Windows CE core
aygshell.lib     // Application shell
commctrl.lib     // Common controls
ole32.lib        // OLE support
oleaut32.lib     // OLE automation
winsock.lib      // Networking
```

### Project: HBXClientCab

**Type**: CAB Installer
**Output**: `HBXClient.cab`
**Dependencies**: HBXClient.exe

---

## ⚙️ Build Configurations

### Debug Configuration

**Platform**: Windows Mobile 6.5 Professional SDK (ARMV4I)

```
Output Directory:    ../bin/Debug/
Intermediate Dir:    ../obj/Debug/
Output File:         HBXClient.exe
Runtime Library:     Multi-threaded Debug DLL (/MDd)
Optimization:        Disabled (/Od)
Debug Info:          Program Database (/Zi)
Warnings:            Level 3 (/W3)
Preprocessor:        WIN32;_WIN32_WCE=0x0650;UNDER_CE;DEBUG;_DEBUG
```

### Release Configuration

**Platform**: Windows Mobile 6.5 Professional SDK (ARMV4I)

```
Output Directory:    ../bin/Release/
Intermediate Dir:    ../obj/Release/
Output File:         HBXClient.exe
Runtime Library:     Multi-threaded DLL (/MD)
Optimization:        Maximize Speed (/O2)
Inline:              Any Suitable (/Ob2)
Debug Info:          None
Warnings:            Level 3 (/W3)
Preprocessor:        WIN32;_WIN32_WCE=0x0650;UNDER_CE;NDEBUG
```

---

## 🏃 Building the Application

### Method 1: Visual Studio GUI

1. **Open Solution**
   ```
   File → Open → Project/Solution
   Navigate to: proj/mc75-homebox-client.sln
   ```

2. **Select Configuration**
   ```
   Build → Configuration Manager
   Active Solution Configuration: Debug or Release
   Active Solution Platform: Windows Mobile 6.5 Professional SDK (ARMV4I)
   ```

3. **Build**
   ```
   Build → Build Solution (F7)
   or
   Build → Rebuild Solution (Ctrl+Alt+F7)
   ```

4. **View Output**
   ```
   Output window shows build progress
   Check for errors/warnings
   ```

### Method 2: Build Script

```batch
cd scripts
build_winmobile.bat
```

**Script Contents**:
```batch
@echo off
echo 🔨 Building MC75 HomeBox Client...

REM Set paths
set SOLUTION=..\proj\mc75-homebox-client.sln
set CONFIG=Release
set PLATFORM="Windows Mobile 6.5 Professional SDK (ARMV4I)"

REM Build
"C:\Program Files\Microsoft Visual Studio 9.0\Common7\IDE\devenv.exe" ^
  %SOLUTION% /Build "%CONFIG%|%PLATFORM%"

if %ERRORLEVEL% EQU 0 (
  echo ✅ Build successful!
  echo 📦 Output: bin\%CONFIG%\HBXClient.exe
) else (
  echo ❌ Build failed with error code %ERRORLEVEL%
  exit /b 1
)
```

### Method 3: Command Line (MSBuild)

```batch
REM Navigate to project
cd proj

REM Build Debug
msbuild HBXClient.vcproj /p:Configuration=Debug /p:Platform="Windows Mobile 6.5 Professional SDK (ARMV4I)"

REM Build Release
msbuild HBXClient.vcproj /p:Configuration=Release /p:Platform="Windows Mobile 6.5 Professional SDK (ARMV4I)"
```

---

## 📜 Build Scripts

### `build_winmobile.bat`

**Purpose**: Automated Windows Mobile device build

**Usage**:
```batch
cd scripts
build_winmobile.bat [Debug|Release]
```

**Features**:
- ✅ Validates SDK installation
- ✅ Builds solution
- ✅ Reports build status
- ✅ Shows output location

### `build_host_debug.sh`

**Purpose**: Compile-check the entire codebase and run the unit + integration
tests on a POSIX host (Linux/macOS) **without** the Windows Mobile SDK, VS2008,
or a device. Ideal for CI and quick local validation of the core logic.

**Usage**:
```bash
./scripts/build_host_debug.sh
```

**Requirements**: only a C++ compiler (`g++` or `clang++`) and `make`.

See [Host Build & Testing](#-host-build--testing) below for how it works.

---

## 🧪 Host Build & Testing

The production app targets Windows Mobile 6.5 (ARMV4I) and can only be linked
with the VS2008 toolchain on Windows. To make the code testable anywhere, the
repository ships a small **Win32/CE shim** under `tests/host/shim/` that maps
the subset of the Windows API the code uses onto the host:

- `TCHAR` becomes `char` and `TEXT("x")` a narrow literal, so `wsprintf`'s
  `%s` semantics match `wsprintfW`.
- The `lstr*` / `wcs*` string helpers become inline wrappers over `<cstring>`.
- File I/O (`CreateFile`/`ReadFile`/`WriteFile`/`SetFilePointer`/…) is mapped to
  POSIX `open`/`read`/`write`/`lseek`, so `Journal` and `Config` exercise real
  files.
- `<winsock.h>` maps to BSD sockets; the GUI surface (`<commctrl.h>`, window
  APIs) is provided as inert stubs so the UI translation units compile.

### What runs where

| Layer | Host build |
|-------|-----------|
| `JsonLite`, `Item`, `Location`, `Journal`, `Config`, `HttpClient` URL parsing, `HbClient` request gating | **compiled + unit/integration tested** |
| GUI views, `Controller`, `main`, `ScannerHAL` | **compile-checked** (need a real device to run) |

### Running

```bash
# Full gate: compile every source file + run the test suite
./scripts/build_host_debug.sh

# Or drive the Makefile directly:
make -C tests/host check        # compile-all + run tests
make -C tests/host run          # build + run tests only
make -C tests/host compile-all  # syntax-check every source file
make -C tests/host clean
```

Expected tail of a successful run:

```
==== 32/32 test cases passed, 204/204 checks passed ====
Host debug build & tests completed successfully.
```

### Layout

```
tests/host/
├── shim/                 # Win32/CE shim headers (windows.h, winsock.h, wchar.h, commctrl.h)
├── test_framework.hpp    # tiny zero-dependency assertion framework (TEST_CASE / CHECK*)
├── test_main.cpp         # runner entry point
└── Makefile              # build gate + test runner
tests/unit/               # test_json.cpp, test_journal.cpp, test_http.cpp
tests/integration/        # test_api_endpoints.cpp, test_offline_sync.cpp
```

> The shim is **host-only** — it lives on the test include path and is never
> seen by the real Windows Mobile build.

---

## 🔧 Troubleshooting

### Common Build Errors

#### ❌ Error: SDK Not Found

```
error: Platform 'Windows Mobile 6.5 Professional SDK (ARMV4I)' not found
```

**Solution**:
```
1. Verify SDK installation
2. Restart Visual Studio
3. Check SDK path in project properties
4. Reinstall Windows Mobile 6.5 SDK if necessary
```

#### ❌ Error: Missing EMDK Headers

```
fatal error C1083: Cannot open include file: 'ScanCAPI.h'
```

**Solution**:
```
1. Install Zebra EMDK for C/C++
2. Add EMDK include path to project:
   Project Properties → C/C++ → General → Additional Include Directories
   Add: $(ZEBRAEMDK)\Include
```

#### ❌ Error: Unresolved External Symbol

```
error LNK2019: unresolved external symbol _SCAN_Open
```

**Solution**:
```
1. Add EMDK library path:
   Project Properties → Linker → General → Additional Library Directories
   Add: $(ZEBRAEMDK)\Lib\ARMV4I

2. Add EMDK library:
   Project Properties → Linker → Input → Additional Dependencies
   Add: ScanAPI.lib
```

#### ❌ Error: Charset Mismatch

```
warning C4819: The file contains a character that cannot be represented in the current code page
```

**Solution**:
```
Project Properties → C/C++ → Command Line → Additional Options
Add: /utf-8
```

#### ❌ Error: Out of Memory

```
fatal error C1060: compiler is out of heap space
```

**Solution**:
```
Project Properties → C/C++ → Command Line → Additional Options
Add: /Zm200
```

### Build Performance

**Slow Builds**:
- ✅ Use incremental builds (enabled by default in Debug)
- ✅ Disable precompiled headers if not needed
- ✅ Reduce warning level from /W4 to /W3
- ✅ Use Release configuration for final builds only

**Clean Build**:
```
Build → Clean Solution
Then: Build → Rebuild Solution
```

---

## 📦 Output Files

### Debug Build

**Location**: `bin/Debug/`

```
HBXClient.exe          // Executable (with debug info)
HBXClient.pdb          // Debug symbols
*.obj                  // Object files (in obj/Debug/)
```

**Size**: ~800 KB (larger due to debug info)

### Release Build

**Location**: `bin/Release/`

```
HBXClient.exe          // Executable (optimized)
*.obj                  // Object files (in obj/Release/)
```

**Size**: ~250 KB (optimized, no debug info)

### CAB Installer

**Location**: `bin/Release/`

```
HBXClient.cab          // CAB installer package
```

**Contents**:
- HBXClient.exe
- Default configuration (if exists)
- Installation manifest

---

## 🎯 Build Best Practices

### Before Building

1. ✅ **Clean solution** for major changes
2. ✅ **Update all source files** from version control
3. ✅ **Check configuration** (Debug vs Release)
4. ✅ **Verify platform** (Windows Mobile 6.5 ARMV4I)

### During Development

1. ✅ Use **Debug configuration** for active development
2. ✅ Enable **all warnings** (/W3 or /W4)
3. ✅ **Fix warnings** as they appear
4. ✅ **Incremental builds** for faster iteration

### Before Release

1. ✅ **Rebuild in Release** configuration
2. ✅ **Run all tests** (unit + integration)
3. ✅ **Test on actual hardware** (MC75 device)
4. ✅ **Verify CAB installation** works correctly
5. ✅ **Document build number** and commit hash

---

## 📊 Build Verification

### Success Criteria

```bash
✅ 0 Errors
✅ 0 Warnings (or only expected warnings)
✅ HBXClient.exe created
✅ File size reasonable (200-300 KB for Release)
✅ Can deploy to device
✅ Application launches on device
```

### Quick Test

After successful build:

```batch
REM Copy to device
copy bin\Release\HBXClient.exe "\Mobile Device\My Documents\"

REM Or deploy via ActiveSync
scripts\deploy_to_device.bat
```

---

<div align="center">

**🎉 Build complete! Ready to deploy to MC75 devices.**

[← Back to README](../README.md) | [Next: Deployment →](DEPLOYMENT.md)

</div>
