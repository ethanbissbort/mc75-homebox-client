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
   Install to C:\Program Files\Zebra Technologies\EMDK-C
   If you install elsewhere, set %ZEBRAEMDK% - the project resolves the
   EMDK include/lib paths through that variable (see Environment Variables)
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
✅ C:\Program Files\Windows Mobile 6.5 SDK\PocketPC\Include\Armv4i\
✅ C:\Program Files\Zebra Technologies\EMDK-C\Include\
✅ C:\Program Files\Microsoft Visual Studio 9.0\
```

### Environment Variables

`proj/HBXClient.vcproj` does **not** hard-code the SDK or EMDK location — it
references these two variables, so a non-default install never requires editing
the project file:

```batch
set WINDOWSMOBILE65SDK=C:\Program Files\Windows Mobile 6.5 SDK
set ZEBRAEMDK=C:\Program Files\Zebra Technologies\EMDK-C
```

- `scripts\build_winmobile.bat` sets both to the values above when they are not
  already defined, and warns if the resulting directories do not exist.
- For **IDE** builds set them system-wide (System Properties → Environment
  Variables) *before* starting Visual Studio — VS reads the environment once at
  launch.
- Leaving them unset makes the extra include/library paths expand to a bare
  `\PocketPC\Include\Armv4i`, which VS reports as a "cannot open include
  directory" warning. The Windows Mobile headers themselves still resolve
  through the selected platform, but `ScanCAPI.h` / `ScanAPIWM.lib` from the
  EMDK will **not** be found and the `HBX_USE_EMDK` build will fail.

---

## 🏗️ Solution Structure

### Visual Studio Solution

**File**: `mc75-homebox-client.sln` (repository root; the two `.vcproj` files it
references live in `proj/`)

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
src/StrUtil.cpp              // Bounded string / UTF-8 helpers (HBX::Str)

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

Exactly as configured in `proj/HBXClient.vcproj` (both configurations):

```
..\include
$(WINDOWSMOBILE65SDK)\PocketPC\Include\Armv4i
$(ZEBRAEMDK)\Include
```

#### Library Directories

```
$(WINDOWSMOBILE65SDK)\PocketPC\Lib\Armv4i
$(ZEBRAEMDK)\Lib\ARMV4I
```

> Paths are relative to `proj/`, which is why the project include is
> `..\include`. See [Environment Variables](#environment-variables) for how the
> two macros are resolved.

#### Linked Libraries

```
coredll.lib      // Windows CE core
aygshell.lib     // Application shell (soft-key menu bar)
commctrl.lib     // Common controls (list view)
ole32.lib        // OLE support
oleaut32.lib     // OLE automation
winsock.lib      // Networking (fallback HTTP transport)
wininet.lib      // Real HTTP/HTTPS transport (HBX_USE_WININET; ships with the SDK)
ScanAPIWM.lib    // Zebra Scanner C API for real scanning (HBX_USE_EMDK; from the EMDK for C)
```

> Real scanning (`HBX_USE_EMDK`) and the WinInet HTTP/HTTPS transport
> (`HBX_USE_WININET`) are enabled by default. `wininet.lib` is part of the SDK;
> `ScanAPIWM.lib` comes from the **Zebra EMDK for C**. To build without a
> scanner, remove `HBX_USE_EMDK` — see [SCANNING.md](SCANNING.md).

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
CAB File:            HBXClient_Debug.CAB
Runtime Library:     Multi-threaded Debug DLL (/MDd)
Optimization:        Disabled (/Od)
Debug Info:          Program Database (/Zi)
Warnings:            Level 3 (/W3)
Preprocessor:        WIN32;_WIN32_WCE=0x0600;UNDER_CE;WIN32_PLATFORM_PSPC;_DEBUG;
                     HBX_USE_WININET;HBX_USE_EMDK
```

### Release Configuration

**Platform**: Windows Mobile 6.5 Professional SDK (ARMV4I)

```
Output Directory:    ../bin/Release/
Intermediate Dir:    ../obj/Release/
Output File:         HBXClient.exe
CAB File:            HBXClient.CAB
Runtime Library:     Multi-threaded DLL (/MD)
Optimization:        Maximize Speed (/O2)
Inline:              Only __inline (/Ob1)
Intrinsics:          Enabled (/Oi), favour fast code (/Ot), no frame pointers (/Oy)
Debug Info:          Not linked (GenerateDebugInformation=false)
Warnings:            Level 3 (/W3)
Preprocessor:        WIN32;_WIN32_WCE=0x0600;UNDER_CE;WIN32_PLATFORM_PSPC;NDEBUG;
                     HBX_USE_WININET;HBX_USE_EMDK
```

> `_WIN32_WCE=0x0600` is the Windows CE 6 kernel version underneath Windows
> Mobile 6.5 — it is **not** the Windows Mobile version number.

---

## 🏃 Building the Application

### Method 1: Visual Studio GUI

1. **Open Solution**
   ```
   File → Open → Project/Solution
   Navigate to: mc75-homebox-client.sln (repository root)
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
REM From anywhere - the script resolves every path from its own location
scripts\build_winmobile.bat            REM Release (default)
scripts\build_winmobile.bat Debug
```

**What the script does**:

1. Resolves the repository root from `%~dp0`, so the working directory does not
   matter.
2. Validates the configuration argument (`Debug` / `Release`, default
   `Release`) and that `mc75-homebox-client.sln` exists.
3. Requires `%VS90COMNTOOLS%` and calls `vsvars32.bat`.
4. Defaults `%WINDOWSMOBILE65SDK%` / `%ZEBRAEMDK%` when unset and warns if the
   SDK/EMDK directories are missing.
5. Runs `devenv /Clean` then `devenv /Build` for
   `<Config>|Windows Mobile 6.5 Professional SDK (ARMV4I)`.
6. Prints the resulting `.exe` and `.CAB` paths, and the deploy command.

### Method 3: Command Line (devenv)

```batch
REM Run from the repository root (the .sln path is relative to the CWD here)
call "%VS90COMNTOOLS%\vsvars32.bat"

devenv mc75-homebox-client.sln /Build "Debug|Windows Mobile 6.5 Professional SDK (ARMV4I)"
devenv mc75-homebox-client.sln /Build "Release|Windows Mobile 6.5 Professional SDK (ARMV4I)"
```

> Use `devenv`, not MSBuild: MSBuild 3.5 cannot build VS2008 native `.vcproj`
> files, and smart-device projects additionally need the IDE's deployment
> machinery. `build_winmobile.bat` wraps exactly these commands.

---

## 📜 Build Scripts

> All three scripts resolve their paths from their own location, so they can be
> invoked from the repository root, from `scripts\`, or from a shortcut with an
> unrelated working directory.

### `build_winmobile.bat`

**Purpose**: Automated Windows Mobile device build

**Usage**:
```batch
scripts\build_winmobile.bat [Debug|Release]     REM default: Release
```

**Features**:
- ✅ Validates the configuration argument and the solution path
- ✅ Requires VS2008 (`%VS90COMNTOOLS%`) and warns about a missing SDK/EMDK
- ✅ Cleans, then builds the solution
- ✅ Reports build status and the `.exe` / `.CAB` output paths

### `deploy_to_device.bat`

**Purpose**: Copy the built CAB to a connected MC75 over ActiveSync/WMDC.

**Usage**:
```batch
scripts\deploy_to_device.bat [Debug|Release]    REM default: Release
```

Picks `bin\Release\HBXClient.CAB` or `bin\Debug\HBXClient_Debug.CAB` to match
the CAB project's output names, copies it to `\Temp\` on the device, and fails
loudly if the device is absent or the copy does not complete.

### `build_host_debug.sh`

**Purpose**: The host gate in one command — compile-check the entire codebase
(host *and* device configurations) and run the unit + integration tests on a
POSIX host (Linux/macOS) **without** the Windows Mobile SDK, VS2008, or a
device. This is the CI entry point; it is a thin wrapper around
`make -C tests/host check`.

**Usage**:
```bash
./scripts/build_host_debug.sh
CXX=clang++ ./scripts/build_host_debug.sh   # override the compiler
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
  `%s` semantics match `wsprintfW`. The shim also enforces the real
  `wsprintf`/`wsprintfW` **1024-character output cap**, so a format call that
  would be truncated on the device is truncated on the host too.
- The `lstr*` / `wcs*` string helpers become inline wrappers over `<cstring>`.
- File I/O (`CreateFile`/`ReadFile`/`WriteFile`/`SetFilePointer`/…) is mapped to
  POSIX `open`/`read`/`write`/`lseek`, so `Journal` and `Config` exercise real
  files.
- The CE critical-section API maps to POSIX mutexes, which is why the host build
  links with `-pthread`.
- `<winsock.h>` maps to BSD sockets; the GUI surface (`<commctrl.h>`, window
  APIs) is provided as inert stubs so the UI translation units compile.

### What runs where

| Layer | Host build |
|-------|-----------|
| `StrUtil`, `JsonLite`, `Item`, `Location`, `Journal`, `Config`, `SyncEngine` queue/replay, `HttpClient` URL parsing, `HbClient` request gating | **compiled + unit/integration tested** |
| GUI views, `Controller`, `main`, `ScannerHAL` | **compile-checked** (need a real device to run) |

The compiled-and-tested set is `CORE_SRC` in `tests/host/Makefile`; every source
under `src/` is compile-checked by the two gates regardless.

### Running

```bash
# Full gate: compile every source file + run the test suite
./scripts/build_host_debug.sh

# Or drive the Makefile directly:
make -C tests/host check          # compile-all + compile-device + run tests
make -C tests/host run            # build + run tests only
make -C tests/host compile-all    # syntax-check every source (host default paths)
make -C tests/host compile-device # syntax-check the device paths (HBX_USE_EMDK + HBX_USE_WININET)
make -C tests/host clean
```

A successful run ends with every gate clean and no failing case — the totals
move as tests are added, so match the shape, not the numbers:

```
All sources compile cleanly.
All device paths compile cleanly.
==== <N>/<N> test cases passed, <M>/<M> checks passed ====
Host debug build & tests completed successfully.
```

Any `FAIL <path>` line, or a passed count below the total, fails the gate and
the script exits non-zero.

### Layout

```
tests/host/
├── shim/                 # Win32/CE shim headers (windows.h, winsock.h, wchar.h, commctrl.h)
├── test_framework.hpp    # tiny zero-dependency assertion framework (TEST_CASE / CHECK*)
├── test_main.cpp         # runner entry point
└── Makefile              # build gate + test runner
tests/unit/               # test_strutil.cpp, test_json.cpp, test_journal.cpp, test_http.cpp
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
2. Set %ZEBRAEMDK% to the install directory (see Environment Variables) and
   restart Visual Studio - the project already references $(ZEBRAEMDK)\Include
3. Or build without real scanning by removing HBX_USE_EMDK (see SCANNING.md)
```

#### ❌ Error: Unresolved External Symbol

```
error LNK2019: unresolved external symbol _SCAN_Open
```

**Solution**:
```
1. Set %ZEBRAEMDK% (see Environment Variables) and restart Visual Studio - the
   project already references $(ZEBRAEMDK)\Lib\ARMV4I

2. Confirm the EMDK ships ScanAPIWM.lib (the Windows Mobile import library) in
   that directory; it is already listed in Additional Dependencies
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

**Location**: `bin/Release/` (Release) or `bin/Debug/` (Debug)

```
HBXClient.CAB          // Release CAB installer package
HBXClient_Debug.CAB    // Debug CAB installer package
```

**Contents** (identical for both configurations):
- HBXClient.exe
- hb_conf.json (only if present in the repository root)
- Installation manifest

Everything else the app needs is linked into the executable — the `.rc` files
under `resources/` are compile-time resource scripts and are deliberately **not**
deployed; installing them would only waste device storage.

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
2. ✅ **Run all tests** (`./scripts/build_host_debug.sh` — compile gates + suite)
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

REM Or deploy the CAB via ActiveSync (Release by default)
scripts\deploy_to_device.bat
scripts\deploy_to_device.bat Debug
```

---

<div align="center">

**🎉 Build complete! Ready to deploy to MC75 devices.**

[← Back to README](../README.md) | [Next: Deployment →](DEPLOYMENT.md)

</div>
