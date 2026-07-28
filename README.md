# 📱 MC75 HomeBox Client

> 🔧 **Native C++ inventory management client for Motorola MC75 handheld devices**

[![Platform](https://img.shields.io/badge/Platform-Windows%20Mobile%206.5-blue)](https://en.wikipedia.org/wiki/Windows_Mobile)
[![Language](https://img.shields.io/badge/Language-C%2B%2B-orange)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Device](https://img.shields.io/badge/Device-Motorola%20MC75-green)](https://www.zebra.com/)
[![License](https://img.shields.io/badge/License-Proprietary-red)]()

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Features](#-features)
- [Architecture](#-architecture)
- [Quick Start](#-quick-start)
- [Documentation](#-documentation)
- [Development](#-development)
- [License](#-license)

---

## 🎯 Overview

The **MC75 HomeBox Client** is a native C++ application designed for Motorola MC75 handheld scanners running Windows Mobile 6.5 Professional. It provides barcode scanning, item tracking and offline transaction queuing against **two inventory backends**:

- **HomeBox** — full item CRUD, the original backend.
- **NetBox** — DCIM device lookup, status change and move. See [NETBOX.md](docs/NETBOX.md).

Exactly one backend is active at a time, named by `activeBackend` in `hb_conf.json`. The offline queue is shared: every record is tagged with its backend's instance id, so queued work always replays against the server it was recorded for — even after the operator switches backends.

### 🏭 Built For

- **Target Device**: Motorola MC75 Enterprise Digital Assistant
- **Operating System**: Windows Mobile 6.5 Professional (ARMV4I)
- **Compiler**: Visual Studio 2008
- **SDK**: Windows Mobile 6.5 Professional SDK + Zebra EMDK

---

## ✨ Features

### 🔍 Core Functionality

| Feature | Description | Status |
|---------|-------------|--------|
| 📦 **Barcode Scanning** | Hardware-integrated scanner with EMDK support | ✅ Complete |
| 🔀 **Two Backends** | HomeBox and NetBox, one active at a time | ✅ Complete |
| 📊 **Item Management** | Full CRUD operations for HomeBox inventory items | ✅ Complete |
| 🖥️ **NetBox Devices** | Scan-to-device lookup, status change, atomic move | ✅ Complete |
| 🌐 **API Integration** | RESTful communication with both backends | ✅ Complete |
| 💾 **Offline Queue** | Transaction queuing with automatic sync, routed per backend | ✅ Complete |
| 📝 **Transaction Journal** | Audit trail with timestamp logging | ✅ Complete |
| 🔄 **Automatic Sync** | Timer-driven sync attempts from the UI thread when online | ✅ Complete |
| ➕ **NetBox device creation** | Four mandatory foreign keys — a web-UI job, not a handheld one | ⛔ Out of scope |

### 🎨 User Interface

- **📱 Scan View**: Real-time barcode scanning interface
- **📋 Item View**: Comprehensive HomeBox item editing with 6 input fields
- **🖥️ Device View**: NetBox device detail — a viewer with targeted actions, not an editor
- **🔢 Picker View**: Reusable chooser, and the disambiguation list when one scan matches several devices
- **📊 Queue View**: ListView-based transaction queue manager
- **🎯 Status Display**: Live sync status and item count indicators

### 🔧 Technical Features

- ⚡ **Lightweight JSON Parser**: Custom implementation for embedded environment
- 🔐 **Pluggable Authentication**: HomeBox exchanges a device key for a bearer session token; NetBox uses a static API token with a configurable `Token` / `Bearer` scheme
- 📡 **Smart Connectivity**: DNS-based connectivity detection
- 💪 **Manual Memory Management**: Optimized for resource-constrained devices
- 🌍 **Unicode Support**: Full TCHAR/WCHAR string handling

---

## 🏗️ Architecture

### 📐 Design Pattern: MVC

```
┌─────────────────────────────────────────────────┐
│                 Controller                      │
│            (Application Logic)                  │
└─────────────┬───────────────────────┬───────────┘
              │                       │
      ┌───────▼────────┐     ┌───────▼────────┐
      │     Views      │     │    Models      │
      │  - ScanView    │     │  - Item        │
      │  - ItemView    │     │  - Location    │
      │  - QueueView   │     │  - JsonLite    │
      └────────────────┘     └────────────────┘
```

### 🔌 Core Components

#### **HbClient / NbClient** 🌐
- Two `InventoryBackend` implementations behind one interface
- `HbClient`: HomeBox item CRUD, session-token authentication
- `NbClient`: NetBox DCIM lookup / status / move, static API token
- JSON request/response handling

#### **SyncEngine** 🔄
- Offline transaction queuing
- Backend registry: records are tagged `<instanceId>.<TYPE>` and replayed against the backend they were recorded for
- Background synchronization
- Connectivity monitoring

#### **ScannerHAL** 📱
- Zebra EMDK integration
- Hardware abstraction layer
- Threaded scan monitoring

#### **Journal** 📝
- Transaction logging
- File-based persistence
- Audit trail generation

---

## 🚀 Quick Start

### Prerequisites

```bash
✅ Windows development environment
✅ Visual Studio 2008
✅ Windows Mobile 6.5 Professional SDK (ARMV4I)
✅ Zebra EMDK for C/C++
✅ ActiveSync or Windows Mobile Device Center
```

### 🔨 Build

```bash
# Open the solution - it lives at the repository ROOT.
# proj/ holds only the two .vcproj files the solution references.
start mc75-homebox-client.sln

# Build configurations available:
# - Debug|Windows Mobile 6.5 Professional SDK (ARMV4I)
# - Release|Windows Mobile 6.5 Professional SDK (ARMV4I)

# Or use the build script - it derives every path from its own location,
# so it runs from any working directory:
scripts\build_winmobile.bat            REM Release (default)
scripts\build_winmobile.bat Debug
```

> The project resolves the Windows Mobile SDK and the Zebra EMDK through the
> `%WINDOWSMOBILE65SDK%` and `%ZEBRAEMDK%` environment variables rather than
> hard-coded paths — see [BUILD.md → Environment Variables](docs/BUILD.md#environment-variables).

### 🧪 Test on any host (no device/SDK required)

The platform-independent core logic can be compiled and tested on any
Linux/macOS machine using the bundled Win32/CE shim — only `g++`/`clang++`
and `make` are needed:

```bash
./scripts/build_host_debug.sh
# -> compile-checks all 21 source files against the shim, repeats the check with
#    the device macros (HBX_USE_EMDK + HBX_USE_WININET), then runs the
#    unit + integration suite:
#    ==== 138/138 test cases passed, 1740/1740 checks passed ====
```

The script is a thin wrapper around `make -C tests/host check`, which is the
gate CI should run (`compile-all` + `compile-device` + the suite).

See [BUILD.md → Host Build & Testing](docs/BUILD.md#-host-build--testing) for details.

### 📦 Deploy

```bash
# Deploy to a connected MC75 (ActiveSync / WMDC). Like the build script, it
# locates the CAB relative to itself, so any working directory will do:
scripts\deploy_to_device.bat           REM Release (default)
scripts\deploy_to_device.bat Debug

# Manual deployment:
# 1. Build the HBXClientCab project
# 2. Copy bin\Release\HBXClient.CAB (Debug builds: HBXClient_Debug.CAB) to the device
# 3. Tap the .CAB file on the device to install
```

### ⚙️ Configure

An annotated `hb_conf.json` template ships in the repository root and the CAB
deploys it. The app looks in `\Program Files\HBXClient\`, then `\My Documents\`,
then `\Storage Card\`, and runs on documented defaults if it finds none. Every
key is optional; see [DEPLOYMENT.md](docs/DEPLOYMENT.md) for the full table,
including the `apiKey` (yours) / `authToken` (the app's) distinction.

```json
{
  "activeBackend": "hb",
  "apiBaseUrl": "https://your-homebox-api.com",
  "deviceId": "MC75-001",
  "apiKey": "your-api-key-here",
  "netboxInstanceId": "nb",
  "netboxBaseUrl": "http://192.168.20.5",
  "netboxToken": "your-netbox-token",
  "netboxAuthScheme": "Token",
  "syncIntervalSeconds": 300,
  "journalPath": "\\My Documents\\hbx_journal.log"
}
```

Set `activeBackend` to `"nb"` to make NetBox the active backend. NetBox stays
off entirely while `netboxBaseUrl` is empty. **HTTPS to a current NetBox is not
achievable from this hardware** — the supported configuration is plain HTTP on
a segmented scanner VLAN, explained in
[NETBOX.md → Transport](docs/NETBOX.md#-transport-https-does-not-work-from-this-device).

---

## 📚 Documentation

Comprehensive documentation is available in the `docs/` directory:

| Document | Description | Link |
|----------|-------------|------|
| 🏗️ **DESIGN.md** | Architecture and design decisions | [View](docs/DESIGN.md) |
| 🔨 **BUILD.md** | Detailed build instructions | [View](docs/BUILD.md) |
| 🪟 **WINDOWS7_BUILD.md** | From-scratch Windows 7 + VS2008 build setup | [View](docs/WINDOWS7_BUILD.md) |
| 📷 **SCANNING.md** | Real EMDK scanning, HTTPS, and UI features | [View](docs/SCANNING.md) |
| 📱 **MC75_SETUP.md** | Reset & fully update an MC75 over USB, then deploy | [View](docs/MC75_SETUP.md) |
| 🚀 **DEPLOYMENT.md** | Deployment procedures and CAB packaging | [View](docs/DEPLOYMENT.md) |
| 🌐 **API_NOTES.md** | HomeBox API integration guide | [View](docs/API_NOTES.md) |
| 🖥️ **NETBOX.md** | NetBox backend: scope, tokens, transport, workflow, troubleshooting | [View](docs/NETBOX.md) |
| 🤖 **CLAUDE.md** | AI assistant development guide | [View](CLAUDE.md) |

---

## 💻 Development

### 📁 Project Structure

```
mc75-homebox-client/
├── 📄 src/                    # Source files (.cpp)
│   ├── main.cpp              # Application entry point
│   ├── Controller.cpp        # Main controller
│   ├── Config.cpp            # hb_conf.json loader
│   ├── StrUtil.cpp           # HBX::Str bounded string / UTF-8 helpers
│   ├── HttpClient.cpp        # HTTP/HTTPS transports
│   ├── HbClient.cpp          # HomeBox API client
│   ├── NbClient.cpp          # NetBox DCIM client
│   ├── SyncEngine.cpp        # Sync manager + backend registry
│   ├── Journal.cpp           # Transaction journal + offline queue
│   ├── ScannerHAL.cpp        # Scanner abstraction
│   ├── Views/                # UI components
│   │   ├── ScanView.cpp
│   │   ├── ItemView.cpp
│   │   ├── QueueView.cpp
│   │   ├── DeviceView.cpp
│   │   └── PickerView.cpp
│   └── Models/               # Data models
│       ├── Item.cpp
│       ├── Location.cpp
│       ├── Device.cpp
│       ├── AssetSummary.cpp
│       └── JsonLite.cpp
│
├── 📋 include/                # Header files (.hpp)
│   ├── Views/
│   └── Models/
│
├── 🎨 resources/              # UI resources
│   ├── icons/
│   ├── layout.rc
│   └── strings.rc
│
├── 🔧 proj/                   # Visual Studio projects
│   ├── HBXClient.vcproj
│   └── HBXClientCab.vcproj
│
├── 🧪 tests/                  # Test files
│   ├── unit/
│   └── integration/
│
├── 📜 scripts/                # Build scripts
│   ├── build_winmobile.bat
│   └── deploy_to_device.bat
│
└── 📖 docs/                   # Documentation
```

### 🎯 Key Technologies

- **Language**: C++ (pre-C++11, VS2008 compatible)
- **Platform**: Windows Mobile 6.5 Professional
- **Architecture**: ARMV4I (32-bit ARM)
- **UI Framework**: Win32 API (CreateWindow, MessageBox, etc.)
- **Scanner SDK**: Zebra EMDK for C/C++
- **Networking**: WinInet on the device build (`HBX_USE_WININET`), WinSock HTTP/1.1 otherwise
- **Data Format**: JSON (custom lightweight parser)

### 🔍 Code Quality

What the repository can actually demonstrate:

```bash
✅ No TODO / FIXME markers in src/ or include/
✅ Host compile gate clean: all 21 sources build against the Win32/CE shim
✅ Device compile gate clean: the same 21 with HBX_USE_EMDK + HBX_USE_WININET
✅ 138/138 test cases, 1740/1740 checks (make -C tests/host check)
✅ Bounded string / UTF-8 handling centralised in HBX::Str (include/StrUtil.hpp)
✅ Offline-first: scans are journalled and replayed rather than dropped
```

And what it does **not** demonstrate:

```bash
⚠️ The suite only RUNS the platform-independent core - StrUtil, JsonLite, Item,
   Location, Device, AssetSummary, Config, Journal, HttpClient, HbClient,
   NbClient, SyncEngine (tests/host/Makefile). Controller, the five views,
   ScannerHAL and main are compile-checked only.
⚠️ The EMDK and WinInet paths are compile-checked against the shim headers; they
   can only be exercised on an MC75 (or the WM6.5 emulator, minus the scanner).
⚠️ NbClient is tested against constructed NetBox payloads, not a live NetBox.
   Its URL construction, move-body shapes, code classification and device
   parsing are covered; nothing here proves a real server accepts them.
⚠️ Memory is managed by hand (new[]/delete[]); ownership is documented per method
   in the headers, but nothing in the build enforces it.
```

---

## 🤝 Contributing

This is a proprietary enterprise application. For internal development:

1. 🔀 Create a feature branch: `claude/feature-name-sessionid`
2. 💻 Follow C++03 standards (no C++11+ features)
3. 📝 Update documentation
4. ✅ Test on actual MC75 hardware
5. 🚀 Push to remote with retry logic

### 📏 Coding Standards

- **Memory**: Manual management (new/delete, no smart pointers)
- **Strings**: TCHAR for Unicode compatibility
- **Encoding**: Windows-1252 for .vcproj files
- **Indentation**: Follow existing code style
- **Comments**: Document platform-specific behavior

---

## 📄 License

**Proprietary** - Copyright © 2025 Homestead
All rights reserved.

---

## 📞 Support

For technical support or questions:

- 📧 Internal development team
- 📚 See documentation in `docs/`
- 🤖 AI assistant guide in `CLAUDE.md`

---

<div align="center">

**Built with ❤️ for Motorola MC75**

🔨 Visual Studio 2008 | 📱 Windows Mobile 6.5 | 🔍 Zebra EMDK

</div>
