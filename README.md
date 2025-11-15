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
| 🔄 **Background Sync** | Automatic synchronization when online | ✅ Complete |

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
# Open solution
cd proj
start mc75-homebox-client.sln

# Build configurations available:
# - Debug|Windows Mobile 6.5 Professional SDK (ARMV4I)
# - Release|Windows Mobile 6.5 Professional SDK (ARMV4I)

# Or use build script:
cd scripts
build_winmobile.bat
```

### 📦 Deploy

```bash
# Deploy to connected MC75 device:
cd scripts
deploy_to_device.bat

# Manual deployment:
# 1. Build HBXClientCab project
# 2. Copy bin/Release/HBXClient.cab to device
# 3. Tap .cab file on device to install
```

### ⚙️ Configure

Create `hb_conf.json` in installation directory:

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
│   ├── HbClient.cpp          # API client
│   ├── SyncEngine.cpp        # Sync manager
│   ├── Journal.cpp           # Transaction journal
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

```bash
✅ Zero TODO items remaining
✅ Full implementation of all features
✅ Comprehensive inline documentation
✅ Memory leak prevention
✅ Platform-appropriate error handling
✅ Offline-first architecture
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
