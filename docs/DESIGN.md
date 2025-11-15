# 🏗️ Design Document

> **Architecture and design decisions for MC75 HomeBox Client**

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Architecture Pattern](#-architecture-pattern)
- [Component Design](#-component-design)
- [Data Flow](#-data-flow)
- [Offline-First Strategy](#-offline-first-strategy)
- [Memory Management](#-memory-management)
- [Threading Model](#-threading-model)
- [Error Handling](#-error-handling)
- [Platform Constraints](#-platform-constraints)
- [Design Decisions](#-design-decisions)

---

## 🎯 Overview

The MC75 HomeBox Client is designed as a **native C++ application** for the **Windows Mobile 6.5 Professional** platform, targeting **Motorola MC75 handheld devices**. The design prioritizes **offline-first operation**, **resource efficiency**, and **hardware integration** with the device's barcode scanner.

### 🎨 Design Principles

| Principle | Rationale | Implementation |
|-----------|-----------|----------------|
| 🔌 **Offline-First** | MC75 devices operate in warehouses with intermittent connectivity | Transaction queuing with background sync |
| ⚡ **Resource-Efficient** | Limited RAM (64MB) and storage on embedded device | Manual memory management, minimal dependencies |
| 🔧 **Hardware-Integrated** | Tight coupling with Zebra EMDK scanner hardware | Hardware abstraction layer (ScannerHAL) |
| 📦 **Modular** | Separation of concerns for maintainability | MVC-inspired architecture |
| 🛡️ **Defensive** | Network failures, low battery, memory constraints | Journaling, graceful degradation |

---

## 🏛️ Architecture Pattern

### Modified MVC (Model-View-Controller)

The application follows a **modified MVC pattern** adapted for embedded Windows Mobile development:

```
┌─────────────────────────────────────────────────────────────────┐
│                          CONTROLLER                             │
│                     (Application Logic)                         │
│                                                                 │
│  ┌──────────────────────────────────────────────────────┐      │
│  │  • Application lifecycle management                  │      │
│  │  • Component coordination                            │      │
│  │  • Event routing and state management                │      │
│  │  • UI updates and message dispatching                │      │
│  └──────────────────────────────────────────────────────┘      │
└────────┬──────────────────────────────────────────┬────────────┘
         │                                          │
    ┌────▼─────┐                              ┌────▼────┐
    │  VIEWS   │                              │ MODELS  │
    └────┬─────┘                              └────┬────┘
         │                                          │
    ┌────▼──────────────────────┐        ┌─────────▼──────────────┐
    │  • ScanView               │        │  • Item                │
    │  • ItemView               │        │  • Location            │
    │  • QueueView              │        │  • JsonLite (parser)   │
    │  • ViewHelpers            │        │                        │
    └───────────────────────────┘        └────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                      INFRASTRUCTURE LAYER                        │
├──────────────────────────────────────────────────────────────────┤
│  • HbClient (API Client)      • SyncEngine (Offline Sync)       │
│  • HttpClient (HTTP Layer)    • Journal (Transaction Log)       │
│  • ScannerHAL (Hardware)      • Config (Configuration)          │
└──────────────────────────────────────────────────────────────────┘
```

### Layer Responsibilities

#### **Controller Layer**
- Manages application lifecycle (Initialize → Run → Shutdown)
- Coordinates between infrastructure components
- Routes events from views to appropriate handlers
- Maintains application state machine
- Updates UI based on state changes

#### **View Layer**
- Presents UI to the user
- Captures user input (button clicks, text entry)
- Displays data from models
- No business logic (presentation only)
- Communicates with controller via callbacks

#### **Model Layer**
- Represents business entities (Item, Location)
- Handles JSON serialization/deserialization
- Validates data integrity
- No UI dependencies

#### **Infrastructure Layer**
- **HbClient**: REST API communication
- **HttpClient**: Low-level HTTP operations
- **SyncEngine**: Offline/online synchronization
- **Journal**: Transaction logging and persistence
- **ScannerHAL**: Hardware abstraction for barcode scanner
- **Config**: Configuration file management

---

## 🧩 Component Design

### 1️⃣ Controller

**File**: `src/Controller.cpp`, `include/Controller.hpp`

**Responsibility**: Central orchestrator for the entire application

**State Machine**:
```
┌──────────┐
│  INIT    │
└────┬─────┘
     │ Initialize()
     ▼
┌──────────┐     OnScanReceived()     ┌──────────┐
│  IDLE    │ ───────────────────────> │ SCANNING │
└────┬─────┘                          └────┬─────┘
     │                                     │
     │ OnSyncRequested()                   │ Lookup Complete
     │                                     ▼
     │                              ┌──────────┐
     └────────────────────────────> │  SYNCING │
                                    └────┬─────┘
                                         │ Sync Complete
                                         ▼
                                    ┌──────────┐
                                    │  IDLE    │
                                    └──────────┘
```

**Key Methods**:
- `Initialize()`: Bootstraps all components in dependency order
- `Run()`: Enters Windows message loop
- `Shutdown()`: Cleans up resources in reverse order
- `OnScanReceived()`: Handles barcode scan events
- `OnSyncRequested()`: Triggers manual synchronization

**Component Creation Order**:
```cpp
1. Config       → Load configuration first
2. Journal      → Logging available for subsequent components
3. HbClient     → API client for remote operations
4. ScannerHAL   → Hardware integration
5. SyncEngine   → Depends on HbClient and Journal
6. UI           → Last, after all services ready
```

---

### 2️⃣ HbClient (API Client)

**File**: `src/HbClient.cpp`, `include/HbClient.hpp`

**Responsibility**: High-level interface to HomeBox backend API

**Authentication Flow**:
```
┌────────────┐
│ HbClient   │
└─────┬──────┘
      │ Authenticate(deviceId, apiKey)
      ▼
┌──────────────────┐
│ POST /api/v1/    │
│  auth/device     │
└─────┬────────────┘
      │ {"token": "..."}
      ▼
┌──────────────────┐
│ Store token      │
│ m_authToken      │
└─────┬────────────┘
      │ Set m_authenticated = true
      ▼
┌──────────────────┐
│ All subsequent   │
│ requests include │
│ Authorization:   │
│ Bearer <token>   │
└──────────────────┘
```

**API Methods**:

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `Authenticate()` | `POST /api/v1/auth/device` | Device authentication |
| `GetItem()` | `GET /api/v1/items/{barcode}` | Fetch item by barcode |
| `CreateItem()` | `POST /api/v1/items` | Create new item |
| `UpdateItem()` | `PUT /api/v1/items/{id}` | Update existing item |
| `UpdateItemLocation()` | `PUT /api/v1/items/{barcode}/location` | Move item to location |
| `GetLocation()` | `GET /api/v1/locations/{id}` | Fetch location details |
| `GetAllLocations()` | `GET /api/v1/locations` | Fetch all locations |
| `SyncPendingTransactions()` | `POST /api/v1/sync` | Batch sync operations |

**Design Features**:
- ✅ Bearer token authentication
- ✅ Automatic JSON serialization/deserialization
- ✅ HTTP status code validation (200-299 = success)
- ✅ Configurable base URL for multiple environments
- ✅ Stateless requests (authenticated via token)

---

### 3️⃣ SyncEngine (Offline/Online Sync)

**File**: `src/SyncEngine.cpp`, `include/SyncEngine.hpp`

**Responsibility**: Manages offline transaction queuing and synchronization

**Sync Status State Machine**:
```
┌─────────────┐
│  SYNC_IDLE  │ ◄──────────┐
└──────┬──────┘            │
       │ Sync() called     │
       ▼                   │
┌──────────────────┐       │
│ SYNC_IN_PROGRESS │       │
└──────┬───────────┘       │
       │                   │
       ├──────────────┐    │
       │              │    │
       ▼              ▼    │
┌──────────────┐  ┌────────────┐
│ SYNC_SUCCESS │  │ SYNC_FAILED│
└──────┬───────┘  └─────┬──────┘
       │                │
       └────────────────┴────────┘
```

**Queue Management**:
- Transactions stored in `Journal` as file entries
- Each transaction: `TYPE|DATA|TIMESTAMP`
- Queue persists across application restarts
- FIFO processing during sync

**Connectivity Detection**:
```cpp
bool CheckConnectivity():
    1. DNS lookup of API base URL
    2. If successful → IsOnline() = true
    3. If failed → IsOnline() = false
    4. Cache result for 60 seconds to avoid overhead
```

**Sync Algorithm**:
```
1. Check connectivity
   ├─ Offline → Return false, update status
   └─ Online → Continue

2. Read all queued transactions from Journal

3. For each transaction:
   ├─ Parse transaction type and data
   ├─ Execute via HbClient API
   ├─ If success:
   │  ├─ Mark as synced in Journal
   │  └─ Remove from queue
   └─ If failure:
      └─ Keep in queue, continue to next

4. Update sync status:
   ├─ All succeeded → SYNC_SUCCESS
   ├─ Some failed → SYNC_PARTIAL
   └─ All failed → SYNC_FAILED

5. Return result
```

---

### 4️⃣ Journal (Transaction Log)

**File**: `src/Journal.cpp`, `include/Journal.hpp`

**Responsibility**: Persistent transaction logging and audit trail

**Log Entry Format**:
```
[TIMESTAMP] [LEVEL] [TAG] Message
```

**Example Entries**:
```
[2025-11-15 14:32:15] INFO [INIT] Application initialized successfully
[2025-11-15 14:32:45] TRANS [SCAN] Barcode: 1234567890
[2025-11-15 14:33:02] SYNCED [SCAN] Transaction synced to server
[2025-11-15 14:35:12] ERROR [API_CALL] Connection timeout
```

**Transaction Lifecycle**:
```
1. User scans item
   └─ LogTransaction("SCAN", barcode, "Barcode scanned")

2. Queued for sync (if offline)
   └─ Entry remains in journal as "TRANS"

3. Sync successful
   └─ LogInfo("Transaction synced to server")
   └─ Mark entry as "SYNCED"

4. Sync failed
   └─ LogError("SYNC_FAILED", error message)
   └─ Entry remains as "TRANS" for retry
```

**File Persistence**:
- Location: `\Program Files\HBXClient\hbx.journal`
- Format: Plain text (UTF-16 on Windows Mobile)
- Append-only (no in-place edits)
- Survives application crashes and device reboots

---

### 5️⃣ ScannerHAL (Hardware Abstraction Layer)

**File**: `include/ScannerHAL.hpp`

**Responsibility**: Abstract Zebra EMDK scanner hardware

**Hardware Integration**:
```
┌──────────────────┐
│   ScannerHAL     │
└────────┬─────────┘
         │ Initialize()
         ▼
┌──────────────────┐
│  Zebra EMDK API  │
│  (ScanCAPI.h)    │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│  MC75 Hardware   │
│  Physical Scanner│
└──────────────────┘
```

**Scan Thread Model**:
```
Main Thread              Scan Thread
    │                        │
    │ Initialize()           │
    ├───────────────────────>│ CreateThread()
    │                        │
    │                        │ Loop:
    │                        │   WaitForScanEvent()
    │                        │   ReadBarcode()
    │<───────────────────────┤ PostMessage(barcode)
    │ OnScanReceived()       │
    │                        │
```

**EMDK Function Calls**:
- `SCAN_Open()`: Initialize scanner hardware
- `SCAN_Enable()`: Enable scanning
- `SCAN_ReadLabelWait()`: Wait for scan (blocking)
- `SCAN_Disable()`: Disable scanning
- `SCAN_Close()`: Release scanner hardware

**Safety Features**:
- ✅ Thread-safe scan event handling
- ✅ Graceful failure if hardware unavailable
- ✅ Automatic retry on transient errors
- ✅ Proper cleanup on shutdown

---

### 6️⃣ Views (UI Components)

**Files**: `src/Views/*.cpp`, `include/Views/*.hpp`

#### **ScanView**
- Primary scanning interface
- Large barcode display area
- "Scan" button for manual trigger
- Status label for feedback

**Layout**:
```
┌─────────────────────────────┐
│   Status: Ready to scan     │
├─────────────────────────────┤
│                             │
│   ┌───────────────────┐     │
│   │  1234567890       │     │  ← Barcode Display
│   └───────────────────┘     │
│                             │
│   ┌───────────────────┐     │
│   │      SCAN         │     │  ← Scan Button
│   └───────────────────┘     │
└─────────────────────────────┘
```

#### **ItemView**
- Item editing interface
- 6 input fields (barcode, name, description, location, quantity, category)
- Save/Cancel buttons
- Change tracking

**Layout**:
```
┌─────────────────────────────┐
│ Barcode:    [1234567890   ] │
│ Name:       [Widget A      ] │
│ Description:[High quality  ] │
│             [widget        ] │
│ Location:   [A-01          ] │
│ Quantity:   [100           ] │
│ Category:   [Electronics   ] │
│                             │
│  [Save]          [Cancel]   │
└─────────────────────────────┘
```

#### **QueueView**
- Offline queue management
- ListView with pending transactions
- Sync/Clear buttons
- Status and item count display

**Layout**:
```
┌─────────────────────────────┐
│ Queue Status: Idle          │
│ Items: 3                    │
├─────────────────────────────┤
│ Transaction      │ Status   │
├──────────────────┼──────────┤
│ SCAN: 123456     │ Pending  │
│ UPDATE: Widget A │ Pending  │
│ SCAN: 789012     │ Pending  │
└─────────────────────────────┘
│  [Sync]          [Clear]    │
└─────────────────────────────┘
```

---

### 7️⃣ Models (Data Entities)

**Files**: `src/Models/*.cpp`, `include/Models/*.hpp`

#### **Item Model**
```cpp
class Item {
    TCHAR* m_id;           // Unique identifier
    TCHAR* m_barcode;      // Barcode (UPC/EAN/Code128)
    TCHAR* m_name;         // Item name
    TCHAR* m_description;  // Description
    TCHAR* m_locationId;   // Current location ID
    TCHAR* m_category;     // Category/type
    int m_quantity;        // Quantity in stock

    // Serialization
    bool FromJson(const TCHAR* json);
    TCHAR* ToJson() const;
    bool IsValid() const;
};
```

#### **Location Model**
```cpp
class Location {
    TCHAR* m_id;           // Unique identifier
    TCHAR* m_name;         // Location name
    TCHAR* m_description;  // Description
    TCHAR* m_parentId;     // Parent location (hierarchy)

    // Serialization
    bool FromJson(const TCHAR* json);
    TCHAR* ToJson() const;
    bool IsValid() const;
};
```

#### **JsonLite Parser**
Custom lightweight JSON parser designed for embedded environment:
- ✅ No external dependencies
- ✅ Manual parsing (no DOM tree)
- ✅ Minimal memory footprint
- ✅ Supports objects, arrays, strings, numbers, booleans
- ✅ Builder API for JSON generation

**Parsing Strategy**:
```
Input: {"name":"Widget","qty":10}

1. Tokenize: Find key-value pairs
2. Extract: Pull out strings and numbers
3. Populate: Fill model properties
4. Validate: Check required fields

No intermediate representation → Direct model population
```

---

## 📊 Data Flow

### Scan-to-Sync Complete Flow

```
┌────────────┐
│    User    │
│  Triggers  │
│   Scan     │
└─────┬──────┘
      │
      ▼
┌────────────────────┐
│   ScannerHAL       │
│   Read barcode     │
└─────┬──────────────┘
      │ OnScanReceived(barcode)
      ▼
┌────────────────────┐
│   Controller       │
│   Route event      │
└─────┬──────────────┘
      │
      ├──────────────────┐
      │                  │
      ▼                  ▼
 ┌──────────┐      ┌──────────┐
 │ Online?  │      │ Offline? │
 │  YES     │      │  YES     │
 └────┬─────┘      └────┬─────┘
      │                  │
      ▼                  ▼
┌──────────────┐   ┌──────────────┐
│  HbClient    │   │  SyncEngine  │
│  GetItem()   │   │  QueueTrans()│
└─────┬────────┘   └─────┬────────┘
      │                  │
      ▼                  ▼
┌──────────────┐   ┌──────────────┐
│  API Call    │   │  Journal     │
│  Response    │   │  Write       │
└─────┬────────┘   └─────┬────────┘
      │                  │
      ▼                  ▼
┌──────────────┐   ┌──────────────┐
│  ItemView    │   │  QueueView   │
│  Display     │   │  Add Item    │
└──────────────┘   └──────────────┘

       Later:
       ┌──────────────┐
       │  User clicks │
       │  Sync button │
       └─────┬────────┘
             │
             ▼
       ┌──────────────┐
       │  SyncEngine  │
       │  Sync()      │
       └─────┬────────┘
             │
             ▼
       ┌──────────────┐
       │  HbClient    │
       │  Send batch  │
       └─────┬────────┘
             │
             ▼
       ┌──────────────┐
       │  Update UI   │
       │  Clear queue │
       └──────────────┘
```

---

## 🔄 Offline-First Strategy

### Design Philosophy

**Principle**: *Assume network unavailability; treat connectivity as a bonus.*

**Benefits**:
- ✅ Users can work continuously without interruption
- ✅ No data loss during network outages
- ✅ Automatic synchronization when connectivity returns
- ✅ Better user experience in warehouse environments

### Implementation

#### **1. Transaction Queuing**
```cpp
User Action → Check IsOnline()
              ├─ Online  → Direct API call
              └─ Offline → Queue transaction

Queue stored in Journal:
  TRANS|SCAN|1234567890|2025-11-15T14:32:15
  TRANS|UPDATE|{"id":"123","qty":50}|2025-11-15T14:33:02
```

#### **2. Connectivity Detection**
```cpp
CheckConnectivity():
    1. Try DNS lookup of API hostname
    2. If successful → online
    3. If failed → offline
    4. Cache result for 60 seconds
```

#### **3. Automatic Sync**
```
Background timer (every 5 minutes):
  └─ If IsOnline():
      └─ SyncEngine.Sync()
          ├─ Process queued transactions
          ├─ Mark synced items
          └─ Update UI
```

#### **4. User-Initiated Sync**
```
User clicks "Sync" button:
  └─ QueueView → Controller → SyncEngine
      └─ Force immediate sync attempt
      └─ Show progress/results
```

---

## 💾 Memory Management

### Manual Memory Management Strategy

**Rationale**: Windows Mobile 6.5 predates C++11 smart pointers, and embedded environment requires explicit control.

**Rules**:
1. ✅ Every `new` has a matching `delete`
2. ✅ Every `new[]` has a matching `delete[]`
3. ✅ Set pointers to `NULL` after deletion
4. ✅ Check for `NULL` before dereferencing
5. ✅ Clean up in reverse order of allocation

**Ownership Patterns**:

#### **Controller Owns Components**
```cpp
Controller::Controller() {
    m_config = new Config();       // Controller owns
    m_journal = new Journal();     // Controller owns
    m_hbClient = new HbClient();   // Controller owns
}

Controller::~Controller() {
    delete m_config;    // Controller deletes
    delete m_journal;   // Controller deletes
    delete m_hbClient;  // Controller deletes
}
```

#### **String Management**
```cpp
// WRONG: Memory leak
void SetName(const TCHAR* name) {
    m_name = new TCHAR[lstrlen(name) + 1];
    lstrcpy(m_name, name);
}  // If called twice, first allocation leaks!

// CORRECT: Clean up first
void SetName(const TCHAR* name) {
    if (m_name) {
        delete[] m_name;  // Free existing
        m_name = NULL;
    }
    if (name) {
        int len = lstrlen(name) + 1;
        m_name = new TCHAR[len];
        lstrcpy(m_name, name);
    }
}
```

#### **Return Value Ownership**
```cpp
// Caller must delete returned pointer
TCHAR* Item::ToJson() const {
    TCHAR* json = new TCHAR[2048];
    // ... build JSON ...
    return json;  // Caller's responsibility to delete
}

// Usage:
TCHAR* json = item.ToJson();
// ... use json ...
delete[] json;  // Caller deletes
```

**Memory Leak Prevention**:
- ✅ Use destructors for cleanup
- ✅ Initialize all pointers to `NULL`
- ✅ Check for `NULL` before delete (safe but redundant)
- ✅ Test with limited memory scenarios

---

## 🧵 Threading Model

### Single-Threaded with Background Scanner

**Main Thread**:
- Windows message loop
- UI updates
- All business logic
- API calls (blocking acceptable)

**Scanner Thread**:
- Dedicated thread for hardware polling
- Blocks on `SCAN_ReadLabelWait()`
- Posts message to main thread on scan
- No direct UI access

**Thread Communication**:
```cpp
Scanner Thread:
    while (running) {
        barcode = SCAN_ReadLabelWait();
        if (barcode) {
            PostMessage(mainWindow, WM_SCAN_RECEIVED, barcode);
        }
    }

Main Thread:
    case WM_SCAN_RECEIVED:
        Controller.OnScanReceived(barcode);
        break;
```

**Synchronization**:
- No shared data structures (message passing only)
- Scanner thread → Main thread communication via Windows messages
- Main thread owns all application state

**Benefits**:
- ✅ Simple, no race conditions
- ✅ No mutexes or critical sections needed
- ✅ Easy to debug and maintain

---

## 🛡️ Error Handling

### Defensive Programming Strategy

**Principles**:
1. ✅ Validate all inputs
2. ✅ Check all return values
3. ✅ Log all errors
4. ✅ Graceful degradation
5. ✅ Never crash

**Error Handling Patterns**:

#### **API Call Failure**
```cpp
bool success = hbClient->GetItem(barcode, &item);
if (!success) {
    if (!syncEngine->IsOnline()) {
        // Offline - queue for later
        syncEngine->QueueTransaction("SCAN", barcode);
        MessageBox(hwnd, TEXT("Offline - queued"), ...);
    } else {
        // Online but failed - log and notify
        journal->LogError("API_CALL", "GetItem failed");
        MessageBox(hwnd, TEXT("Item not found"), ...);
    }
}
```

#### **Scanner Hardware Failure**
```cpp
if (!scanner->Initialize()) {
    journal->LogError("SCANNER_INIT", "Failed to initialize");
    // Continue without scanner - user can manually enter barcodes
}
```

#### **Memory Allocation Failure**
```cpp
TCHAR* buffer = new TCHAR[1024];
if (!buffer) {
    journal->LogError("OUT_OF_MEMORY", "Buffer allocation failed");
    return false;  // Abort operation gracefully
}
```

**Error Logging**:
- All errors logged to Journal
- Timestamp, error code, message
- Persistent across restarts
- Available for debugging

---

## 🔧 Platform Constraints

### Windows Mobile 6.5 Limitations

| Constraint | Impact | Mitigation |
|-----------|--------|------------|
| **No C++11** | No auto, lambdas, smart pointers | Manual memory management, explicit types |
| **Limited STL** | Some STL features missing/broken | Custom implementations (JsonLite) |
| **64MB RAM** | Memory-constrained environment | Careful allocation, no caching |
| **ARM CPU** | Integer-only on some operations | Custom double formatting |
| **No Exceptions** | Exception handling unreliable | Return codes, validation |
| **Unicode Only** | TCHAR = WCHAR always | wsprintf, lstrcpy for all strings |
| **ActiveSync Deploy** | No over-the-air updates | CAB installer packages |

### Compiler: Visual Studio 2008

**Preprocessor Defines**:
```cpp
WIN32              // Windows platform
_WIN32_WCE=0x0600  // Windows CE 6.0 (WM 6.5)
UNDER_CE           // Windows CE environment
ARMV4I             // ARM architecture
```

**Runtime Library**: Multi-threaded DLL (/MD for Release, /MDd for Debug)

**Optimizations**:
- Release: Maximize Speed (/O2)
- Debug: Disabled (/Od)

---

## 🎯 Design Decisions

### Why Manual Memory Management?

**Decision**: Use `new`/`delete` instead of smart pointers

**Rationale**:
- Visual Studio 2008 predates C++11 `std::unique_ptr`/`std::shared_ptr`
- Platform has limited STL support
- Explicit control beneficial on resource-constrained device
- Simpler debugging on embedded platform

**Trade-off**: More verbose code, but compatible and predictable

---

### Why Custom JSON Parser?

**Decision**: Implement JsonLite instead of using existing library

**Rationale**:
- No dependencies (embedded environment)
- Minimal footprint (<2KB code)
- Only parse what we need (no full DOM)
- Full control over memory allocation
- No third-party licensing issues

**Trade-off**: Limited JSON features, but sufficient for our API

---

### Why Offline-First Architecture?

**Decision**: Queue transactions by default, sync when online

**Rationale**:
- Warehouse environments have poor Wi-Fi coverage
- Cellular data expensive/unavailable on devices
- Users need uninterrupted workflow
- Data integrity critical (no lost scans)

**Trade-off**: Complexity in sync logic, but essential for usability

---

### Why MVC Pattern?

**Decision**: Separate views, models, and controller

**Rationale**:
- Maintainability (separation of concerns)
- Testability (business logic separate from UI)
- Scalability (easy to add new views)
- Standard pattern familiar to developers

**Trade-off**: More files/classes, but cleaner architecture

---

### Why Single Thread + Scanner Thread?

**Decision**: Main thread + dedicated scanner polling thread

**Rationale**:
- UI responsiveness (scanner blocking call doesn't freeze UI)
- Simple synchronization (message passing only)
- Hardware requirement (EMDK blocking read)
- No race conditions (no shared state)

**Trade-off**: Slight overhead of thread creation, but worth it for responsiveness

---

## 📐 Future Design Considerations

### Potential Enhancements

1. **Multi-View Navigation**
   - Tab-based interface
   - View stack for navigation history

2. **Background Sync Timer**
   - Automatic periodic sync
   - Configurable interval

3. **Batch Operations**
   - Scan multiple items
   - Bulk location updates

4. **Offline Cache**
   - Cache item database locally
   - Faster lookups without API

5. **Event System**
   - Decouple components with events
   - Observer pattern for state changes

**Design Principle**: *Maintain simplicity and resource efficiency*

---

<div align="center">

**🎨 Clean Architecture for Embedded Excellence**

[← Back to README](../README.md) | [Next: API Notes →](API_NOTES.md)

</div>
