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

The **MC75 HomeBox Client** is a native C++ application designed for Motorola MC75 handheld scanners running Windows Mobile 6.5 Professional. It provides seamless integration with the HomeBox inventory management system, enabling real-time barcode scanning, item tracking, and offline transaction queuing.

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
| 📊 **Item Management** | Full CRUD operations for inventory items | ✅ Complete |
| 🌐 **API Integration** | RESTful communication with HomeBox backend | ✅ Complete |
| 💾 **Offline Queue** | Transaction queuing with automatic sync | ✅ Complete |
| 📝 **Transaction Journal** | Audit trail with timestamp logging | ✅ Complete |
| 🔄 **Automatic Sync** | Timer-driven sync attempts from the UI thread when online | ✅ Complete |

### 🎨 User Interface

- **📱 Scan View**: Real-time barcode scanning interface
- **📋 Item View**: Comprehensive item editing with 6 input fields
- **📊 Queue View**: ListView-based transaction queue manager
- **🎯 Status Display**: Live sync status and item count indicators

### 🔧 Technical Features

- ⚡ **Lightweight JSON Parser**: Custom implementation for embedded environment
- 🔐 **Secure Authentication**: Bearer token-based API authentication
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

#### **HbClient** 🌐
- REST API communication
- Authentication management
- JSON request/response handling

#### **SyncEngine** 🔄
- Offline transaction queuing
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
# -> compile-checks all 16 source files against the shim, repeats the check with
#    the device macros (HBX_USE_EMDK + HBX_USE_WININET), then runs the
#    unit + integration suite:
#    ==== 53/53 test cases passed, 1065/1065 checks passed ====
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

Create `hb_conf.json` in the installation directory. The app looks in
`\Program Files\HBXClient\`, then `\My Documents\`, then `\Storage Card\`, and
runs on documented defaults if it finds none. Every key is optional; see
[DEPLOYMENT.md](docs/DEPLOYMENT.md) for the full table, including the `apiKey`
(yours) / `authToken` (the app's) distinction.

```json
{
  "apiBaseUrl": "https://your-homebox-api.com",
  "deviceId": "MC75-001",
  "apiKey": "your-api-key-here",
  "syncIntervalSeconds": 300,
  "journalPath": "\\My Documents\\hbx_journal.log"
}
```

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
│   ├── HbClient.cpp          # API client
│   ├── SyncEngine.cpp        # Sync manager
│   ├── Journal.cpp           # Transaction journal + offline queue
│   ├── ScannerHAL.cpp        # Scanner abstraction
│   ├── Views/                # UI components
│   │   ├── ScanView.cpp
│   │   ├── ItemView.cpp
│   │   └── QueueView.cpp
│   └── Models/               # Data models
│       ├── Item.cpp
│       ├── Location.cpp
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
- **Networking**: WinSock (HTTP/1.1 client)
- **Data Format**: JSON (custom lightweight parser)

### 🔍 Code Quality

What the repository can actually demonstrate:

```bash
✅ No TODO / FIXME markers in src/ or include/
✅ Host compile gate clean: all 16 sources build against the Win32/CE shim
✅ Device compile gate clean: the same 16 with HBX_USE_EMDK + HBX_USE_WININET
✅ 53/53 test cases, 1065/1065 checks (make -C tests/host check)
✅ Bounded string / UTF-8 handling centralised in HBX::Str (include/StrUtil.hpp)
✅ Offline-first: scans are journalled and replayed rather than dropped
```

And what it does **not** demonstrate:

```bash
⚠️ The suite only RUNS the platform-independent core - StrUtil, JsonLite, Item,
   Location, Config, Journal, HttpClient, HbClient, SyncEngine (tests/host/Makefile).
   Controller, the three views, ScannerHAL and main are compile-checked only.
⚠️ The EMDK and WinInet paths are compile-checked against the shim headers; they
   can only be exercised on an MC75 (or the WM6.5 emulator, minus the scanner).
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
