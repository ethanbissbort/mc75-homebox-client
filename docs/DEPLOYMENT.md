# 🚀 Deployment Guide

> **Complete deployment procedures for MC75 HomeBox Client**

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Deployment Methods](#-deployment-methods)
- [CAB Package Creation](#-cab-package-creation)
- [Device Deployment](#-device-deployment)
- [Configuration](#-configuration)
- [Post-Deployment](#-post-deployment)
- [Troubleshooting](#-troubleshooting)
- [Uninstallation](#-uninstallation)

---

## 🎯 Overview

This guide covers all deployment methods for installing the MC75 HomeBox Client on Motorola MC75 devices running Windows Mobile 6.5 Professional.

### Deployment Options

| Method | Speed | Difficulty | Use Case |
|--------|-------|------------|----------|
| 📦 **CAB File** | ⭐⭐⭐ | Easy | Production deployment |
| 🔌 **ActiveSync** | ⭐⭐ | Medium | Development testing |
| 📁 **Manual Copy** | ⭐ | Hard | Debug scenarios |
| 🌐 **OTA (Over-the-Air)** | ⭐⭐⭐ | Medium | Enterprise mass deployment |

---

## 📦 Deployment Methods

### Method 1: CAB Installer (Recommended)

**Best for**: Production deployments, end users

#### Steps:

1. **Build CAB Package**
   ```batch
   cd proj
   Open mc75-homebox-client.sln in Visual Studio
   Right-click HBXClientCab project → Build
   ```

2. **Locate CAB File**
   ```
   📁 bin/Release/HBXClient.cab
   ```

3. **Transfer to Device**
   ```
   Method A: USB + ActiveSync
   - Connect MC75 via USB
   - Copy HBXClient.cab to device
   - Navigate to file on device
   - Tap to install

   Method B: SD Card
   - Copy HBXClient.cab to SD card
   - Insert SD card into MC75
   - Navigate to \Storage Card\
   - Tap HBXClient.cab
   ```

4. **Install on Device**
   ```
   - Tap HBXClient.cab
   - Follow installation prompts
   - Default install path: \Program Files\HBXClient\
   - Installation takes ~30 seconds
   ```

### Method 2: ActiveSync Deployment

**Best for**: Development, testing

#### Prerequisites:
- ✅ ActiveSync 4.5+ (Windows XP)  or
- ✅ Windows Mobile Device Center (Windows Vista+)
- ✅ USB cable
- ✅ MC75 with USB drivers installed

#### Steps:

```batch
1. Connect Device
   - Plug MC75 into USB port
   - Wait for ActiveSync/WMDC to recognize device
   - Partnership should appear in ActiveSync

2. Deploy via Visual Studio
   - Open solution in Visual Studio 2008
   - Select "Device" as target
   - Press F5 (Debug) or Ctrl+F5 (Run)
   - VS will deploy automatically

3. Deploy via Script
   cd scripts
   deploy_to_device.bat
```

### Method 3: Manual File Copy

**Best for**: Quick debugging, advanced users

```batch
1. Build executable
   cd proj
   Build HBXClient project (Release)

2. Connect device
   USB → ActiveSync/WMDC
   or
   Network share

3. Copy files
   Source: bin/Release/HBXClient.exe
   Destination: \Mobile Device\Program Files\HBXClient\

4. Copy configuration (optional)
   Source: hb_conf.json
   Destination: \Mobile Device\Program Files\HBXClient\

5. Create shortcut (optional)
   \Windows\Start Menu\Programs\
   Point to: \Program Files\HBXClient\HBXClient.exe
```

### Method 4: OTA Deployment

**Best for**: Enterprise mass deployment

#### Prerequisites:
- 📱 Wi-Fi or cellular data connection
- 🌐 Web server with CAB file
- 📧 Email or SMS notification system

#### Steps:

1. **Prepare Web Server**
   ```nginx
   # Place CAB file on web server
   http://your-server.com/updates/HBXClient.cab

   # Ensure MIME type is set
   MIME: application/vnd.ms-cab-compressed
   ```

2. **Send Link to Devices**
   ```
   Via Email:
   - Send link to MC75 email
   - User taps link
   - CAB downloads and installs

   Via SMS:
   - Send HTTP link via SMS
   - User opens link
   - Download starts automatically
   ```

3. **Automatic Update**
   ```cpp
   // Within app (future enhancement)
   - Check for updates on startup
   - Download new CAB if available
   - Prompt user to install
   ```

---

## 📦 CAB Package Creation

### Project: HBXClientCab

**File**: `proj/HBXClientCab.vcproj`

### CAB Contents

```
HBXClient.cab
├── HBXClient.exe           // Main executable
├── install.inf             // Installation manifest
└── (optional files)
    ├── hb_conf.json       // Default configuration
    ├── README.txt         // User documentation
    └── icons/             // Application icons
```

### Installation Manifest (`install.inf`)

```ini
[Version]
Signature="$Chicago$"
Provider="Homestead"
CESignature="$Windows Mobile$"

[CEStrings]
AppName="HBX Client"
InstallDir=%CE1%\%AppName%

[Strings]
CompanyName="Homestead"

[CEDevice]
VersionMin=6.5
VersionMax=6.99
BuildMin=0
BuildMax=0xE0000000

[DefaultInstall]
CopyFiles=Files.App
AddReg=RegData

[Files.App]
HBXClient.exe,,,0

[DestinationDirs]
Files.App=0,%InstallDir%

[RegData]
HKLM,SOFTWARE\Homestead\HBXClient,InstallDir,0x00000000,%CE1%\%AppName%
HKLM,SOFTWARE\Homestead\HBXClient,Version,0x00000000,"1.0.0"
```

### Building CAB

#### Visual Studio:
```
1. Right-click HBXClientCab project
2. Select "Build"
3. CAB file created in bin/Release/
```

#### Command Line:
```batch
REM Using CabWiz utility
cabwiz.exe HBXClient.inf /dest bin\Release /err errors.log
```

### CAB Configuration

**Product Info**:
```
Product Name:     HBX Client
Version:          1.0.0.0
Manufacturer:     Homestead
Install Location: \Program Files\HBXClient
Size:             ~300 KB
```

**Device Requirements**:
```
OS: Windows Mobile 6.5 Professional
CPU: ARM (ARMV4I)
Memory: 64 MB RAM minimum
Storage: 1 MB free space
```

---

## 📱 Device Deployment

### Pre-Deployment Checklist

```bash
✅ MC75 device powered on and unlocked
✅ Sufficient battery (>30%)
✅ Available storage (>5 MB)
✅ ActiveSync partnership established (if using USB)
✅ Backup existing data
✅ Close all running applications
```

### Deployment Steps

#### 1. Prepare Device

```
Settings → System → Memory
- Check available storage
- Clear temp files if needed

Settings → System → Power
- Verify battery level
- Connect to charger if low
```

#### 2. Transfer CAB

**Via USB/ActiveSync**:
```batch
1. Connect MC75 to PC via USB
2. Wait for ActiveSync to connect
3. Open "Mobile Device" in Windows Explorer
4. Navigate to My Documents or Storage Card
5. Copy HBXClient.cab to device
```

**Via SD Card**:
```batch
1. Copy HBXClient.cab to SD card on PC
2. Insert SD card into MC75
3. Navigate to \Storage Card\ on device
```

**Via Network**:
```batch
1. Connect MC75 to Wi-Fi
2. Open Internet Explorer Mobile
3. Navigate to: http://your-server/HBXClient.cab
4. Tap to download
```

#### 3. Install Application

```
On MC75:
1. Use File Explorer to locate HBXClient.cab
2. Tap the CAB file
3. Installation wizard appears
4. Tap "Install" or "Yes"
5. Select install location:
   - Device (recommended)
   - Storage Card
6. Wait for installation (~30 seconds)
7. Tap "Done" when complete
```

#### 4. Verify Installation

```
Start Menu → Programs → HBXClient
- Application icon should appear
- Tap to launch
- Should open to scan view
- Check "About" for version number
```

---

## ⚙️ Configuration

### Configuration File: `hb_conf.json`

**Location Options**:
- `\Program Files\HBXClient\hb_conf.json` (preferred)
- `\My Documents\hb_conf.json` (alternative)
- `\Storage Card\hb_conf.json` (portable)

### Sample Configuration

```json
{
  "apiBaseUrl": "https://api.homebox.example.com",
  "deviceId": "MC75-WAREHOUSE-001",
  "apiKey": "your-api-key-here",
  "syncIntervalSeconds": 300,
  "journalPath": "\\My Documents\\hbx_journal.log",
  "scannerBeepEnabled": true,
  "scannerVibrateEnabled": true,
  "offlineMode": false,
  "logLevel": "INFO"
}
```

### Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `apiBaseUrl` | string | (required) | HomeBox API server URL |
| `deviceId` | string | (required) | Unique device identifier |
| `apiKey` | string | (required) | API authentication key |
| `syncIntervalSeconds` | int | 300 | Auto-sync interval (seconds) |
| `journalPath` | string | `\My Documents\hbx_journal.log` | Transaction log path |
| `scannerBeepEnabled` | bool | true | Enable beep on scan |
| `scannerVibrateEnabled` | bool | true | Enable vibrate on scan |
| `offlineMode` | bool | false | Start in offline mode |
| `logLevel` | string | "INFO" | Logging level (DEBUG, INFO, WARN, ERROR) |

### Deployment Scenarios

#### Scenario 1: Single Device Setup

```batch
1. Deploy CAB to device
2. Create hb_conf.json manually
3. Edit deviceId to unique value
4. Launch application
5. Test connectivity
```

#### Scenario 2: Mass Deployment (10+ devices)

```batch
1. Pre-configure hb_conf.json templates
2. Generate unique deviceId for each unit
3. Include config in CAB package
4. Deploy via OTA or SD card
5. Verify deployment on sample units
6. Roll out to remaining fleet
```

#### Scenario 3: Development/Testing

```batch
1. Use debug build
2. Point apiBaseUrl to test server
3. Enable verbose logging
4. Set shorter sync interval (60s)
5. Use storage card for easy log access
```

---

## ✅ Post-Deployment

### First Launch Checklist

```bash
✅ Application starts without errors
✅ Configuration file loaded successfully
✅ API connection established
✅ Scanner initializes properly
✅ Can scan barcode (test item)
✅ Item lookup returns data
✅ Offline queue functional
✅ Sync completes successfully
```

### Testing Procedure

**1. Scanner Test**
```
Launch app → Scan View
Press Scan button or trigger
Scan test barcode
Verify beep/vibrate
Check barcode appears on screen
```

**2. API Test**
```
Scan known item barcode
Wait for item lookup
Verify item details display
Check all fields populated
```

**3. Offline Test**
```
Disable Wi-Fi
Scan item
Verify queued for sync
Re-enable Wi-Fi
Trigger sync
Verify sync success
```

**4. Transaction Journal Test**
```
Navigate to journal file location
Open hbx_journal.log
Verify entries logged with timestamps
Check TRANS and SYNCED entries
```

### User Training

**Key Points to Cover**:
- ✅ How to launch application
- ✅ Basic scanning workflow
- ✅ Understanding queue view
- ✅ Manual sync procedure
- ✅ Troubleshooting offline mode
- ✅ When to restart application

---

## 🔧 Troubleshooting

### Common Deployment Issues

#### ❌ CAB Installation Fails

**Symptoms**:
```
"Installation failed"
"Incompatible device"
"Insufficient storage"
```

**Solutions**:
```
1. Check device OS version (must be WM 6.5)
2. Verify available storage (need 5+ MB)
3. Close all running applications
4. Soft reset device and retry
5. Check CAB file not corrupted (re-download)
```

#### ❌ Application Won't Launch

**Symptoms**:
```
Icon appears but app doesn't start
App crashes immediately
Error message on launch
```

**Solutions**:
```
1. Check all DLLs present:
   - coredll.dll
   - aygshell.dll
   - commctrl.dll

2. Verify installation path
   - Should be: \Program Files\HBXClient\

3. Check device memory
   - Need 64+ MB RAM available

4. Review journal log for errors

5. Reinstall application
```

#### ❌ Scanner Not Working

**Symptoms**:
```
No beep on scan
Barcode not captured
Scanner doesn't trigger
```

**Solutions**:
```
1. Check EMDK drivers installed
2. Verify scanner HAL initialized
3. Test with Zebra demo app
4. Check scanner hardware (trigger button)
5. Restart application
6. Restart device
```

#### ❌ API Connection Fails

**Symptoms**:
```
"Connection failed"
"Authentication error"
"Server unreachable"
```

**Solutions**:
```
1. Verify network connectivity
   - Test with Internet Explorer
   - Ping API server

2. Check configuration
   - Verify apiBaseUrl correct
   - Verify apiKey valid
   - Check deviceId unique

3. Review firewall settings
4. Check API server status
5. Enable verbose logging
```

---

## 🗑️ Uninstallation

### Method 1: Settings Menu (Recommended)

```
1. Start → Settings
2. System → Remove Programs
3. Locate "HBX Client"
4. Tap to select
5. Tap "Remove"
6. Confirm removal
7. Wait for uninstall (~15 seconds)
```

### Method 2: Manual Removal

```
1. Close application if running

2. Delete application folder
   \Program Files\HBXClient\
   (Delete entire folder)

3. Delete configuration
   \My Documents\hb_conf.json

4. Delete journal
   \My Documents\hbx_journal.log

5. Remove shortcuts
   \Windows\Start Menu\Programs\HBXClient.lnk

6. Clean registry (optional)
   HKLM\SOFTWARE\Homestead\HBXClient
```

### Data Preservation

**Before Uninstalling**:
```batch
1. Sync all pending transactions
   Queue View → Sync button

2. Backup journal file
   Copy \My Documents\hbx_journal.log to PC

3. Export configuration
   Copy hb_conf.json to safe location

4. Document last sync time
   Note for audit trail
```

---

## 📊 Deployment Metrics

### Success Criteria

```bash
✅ Installation time < 2 minutes
✅ First launch successful
✅ Configuration auto-detected
✅ API connectivity within 30 seconds
✅ Scanner functional immediately
✅ Zero crashes in first hour
✅ Successful sync within 5 minutes
```

### Fleet Deployment

**Timeline for 50 Devices**:
```
Day 1: Pilot (5 devices)
- Deploy to test users
- Monitor for 24 hours
- Gather feedback

Day 2-3: Phase 1 (20 devices)
- Deploy to early adopters
- Provide on-site support
- Document issues

Day 4-5: Phase 2 (25 devices)
- Full rollout to remaining users
- Remote support available
- Final adjustments

Day 6-7: Stabilization
- Monitor all devices
- Address any issues
- Collect metrics
```

---

<div align="center">

**🎉 Deployment Complete! Users ready to scan.**

[← Back to Build](BUILD.md) | [Back to README](../README.md) | [Next: API Notes →](API_NOTES.md)

</div>
