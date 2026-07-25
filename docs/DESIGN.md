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

**Sync Status State Machine** (all six states of `SyncEngine::SyncStatus`):
```
┌─────────────┐
│  SYNC_IDLE  │ ◄─────────────────────────┐
└──────┬──────┘                           │
       │ Sync() called                    │
       ▼                                  │
┌──────────────────┐                      │
│ SYNC_IN_PROGRESS │                      │
└──────┬───────────┘                      │
       │                                  │
       ├───────────────┬──────────────┬───┴──────────┐
       ▼               ▼              ▼              ▼
┌──────────────┐ ┌─────────────┐ ┌────────────┐ ┌──────────────┐
│ SYNC_SUCCESS │ │ SYNC_PARTIAL│ │SYNC_FAILED │ │ SYNC_OFFLINE │
│ all replayed │ │ some stayed │ │ none got   │ │ no route to  │
│              │ │ queued      │ │ through    │ │ the server   │
└──────────────┘ └─────────────┘ └────────────┘ └──────────────┘
```

`SYNC_PARTIAL` is a success from the caller's point of view — `Sync()` returns
true — because the entries that failed are still queued and will be retried.
`SYNC_FAILED` additionally discards the cached connectivity answer.

**Queue Management**:
- Transactions are stored in the `Journal`, which owns the durable queue
- `QueueTransaction` writes the payload `[tick] TYPE: DATA`; the journal wraps it
  in its own record header (see the Journal section below)
- `DATA` is `SCAN:<barcode>`, `SCANLOC:<barcodeLength>:<barcode><locationId>`, or
  `UPDATE:<item json>`. The location form carries an explicit length because a
  Code 128 or QR barcode may legally contain any separator character
- Queue persists across application restarts — the MC75 loses its process on
  every battery swap, so `Journal::Initialize` rebuilds the pending set from disk
- FIFO processing during sync; a failed entry stays queued for the next attempt

**Connectivity Detection**:
```cpp
bool CheckConnectivity() const:
    1. gethostbyname() on the host parsed out of the API base URL
    2. If it resolves → online
    3. If it fails → offline
    4. Cache the answer for 5 seconds (kConnectivityCacheMs)

// The probe is a blocking DNS lookup that costs seconds on GPRS, and the UI
// status line, IsOnline() and Sync() all ask for it - hence the cache. A sync
// where every transaction failed invalidates it immediately rather than
// reporting "online" for the rest of the window.
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

**Responsibility**: Audit trail **and** the durable offline queue — one file, two
kinds of record

**Record Format** (one CRLF-terminated line each, as documented in
`include/Journal.hpp`):
```
[2026-07-25 09:14:02] INFO: application started
[2026-07-25 09:14:07] ERROR: HTTP_500: server rejected update
[2026-07-25 09:14:09] AUDIT: SCAN 123456789: Barcode scanned
[2026-07-25 09:14:09] TRANS 00000042: [51234] ITEM_SCAN: SCAN:123456789
[2026-07-25 09:15:11] SYNCED 00000042
```

Two properties of that layout carry the design:

1. **Only `TRANS` records are queue work.** `INFO` / `ERROR` / `AUDIT` are pure
   history. `LogTransaction` writes `AUDIT`, so journaling a scan does not also
   enqueue it — the controller journals every scan, and when both used the
   `TRANS` label each one became a queue entry that could never be replayed.
2. **A `SYNCED` marker names a sequence number, not the text of the record it
   acknowledges.** Correlating by an 8-digit fixed-width sequence is exact and
   bounded; embedding the original line instead both overflowed a fixed buffer
   and mis-classified any payload containing the word "SYNCED".

**Transaction Lifecycle**:
```
1. User scans item
   └─ Journal::LogTransaction("SCAN", barcode, ...)   → AUDIT record (history only)

2. Cannot reach the server
   └─ SyncEngine::QueueScan → Journal::QueueTransaction → TRANS <seq> record
      (the assigned sequence is returned to the caller)

3. Sync successful
   └─ Journal::MarkTransactionSynced(line) → parses <seq> → SYNCED <seq> record
      The transaction leaves the pending set; the record itself goes at the next
      Compact().

4. Sync failed
   └─ No marker is written; the TRANS record stays pending and is retried
```

**File Persistence**:
- Location: whatever `journalPath` in `hb_conf.json` says
  (default `\My Documents\hbx_journal.log`)
- Format: **UTF-8** bytes on disk, decoded back to TCHAR on read
- Append-only, then compacted: `Compact()` rewrites the file keeping the
  unacknowledged transactions and the most recent ERROR records, and runs
  automatically once the file passes `SetMaxFileBytes` (default 256 KB, since
  the MC75's persistent store is small)
- Sequence numbering continues across compaction and across restarts
- All public methods are serialised by a critical section: the EMDK scanner
  thread and the UI thread share one `Journal` and one file pointer

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

**Scan Thread Model** (device build, `HBX_USE_EMDK`):
```
UI Thread                        Scan Thread
    │                                │
    │ Initialize() + EnableScanner() │
    ├───────────────────────────────>│ CreateThread()
    │                                │
    │                                │ Loop while running:
    │                                │   SCAN_ReadLabelWait(..., 1000 ms)
    │                                │   on E_SCN_SUCCESS: DeliverScan()
    │                                │     └─ invokes the registered callback
    │                                │        ON THIS THREAD
    │                                │
    │   ScanView::ScanThunk runs here ┘  (copies the label, PostMessage)
    │<─── MSG_SCAN_DECODED (WM_APP+1, LPARAM = heap copy) ───
    │ ScanView::OnScanReceived → Controller::OnScanReceived
```

The callback itself is **not** on the UI thread — `ScannerHAL` calls it from the
monitor thread. Marshalling is the *view's* job: `ScanView::ScanThunk` duplicates
the barcode and posts it, and the window procedure takes ownership of that copy
and frees it. Everything downstream (journal write, blocking lookup, message
boxes) then runs on the UI thread.

In the simulation build (`HBX_USE_EMDK` off) no monitor thread is created at all;
`TriggerScan` / `InjectScan` deliver a decode synchronously on the caller's
thread. An idle polling loop would only cost battery.

**EMDK Function Calls**:
- `SCAN_Open("SCN1:")` / `SCAN_Close()`: acquire and release the scanner
- `SCAN_AllocateBuffer()` / `SCAN_DeallocateBuffer()`: the reusable decode buffer
- `SCAN_Enable()` / `SCAN_Disable()`: arm and disarm the beam
- `SCAN_GetParameters()` / `SCAN_SetParameters()`: trigger mode, beep, vibrate
- `SCAN_Flush()` + `SCAN_SetSoftTrigger()`: soft trigger from the Scan button
- `SCAN_ReadLabelWait(handle, buffer, 1000)`: blocking read with a 1 s timeout,
  which is what keeps the loop responsive to shutdown

**Safety Features**:
- ✅ The callback + its `userData` are published and snapshotted under one lock,
  so a decode can never pair a new callback with a stale context
- ✅ Shared state is reference counted. `Controller` can delete the `ScannerHAL`
  while a decode is still in flight; the last owner out releases the hardware, so
  the decode buffer and handle stay valid as long as the thread can touch them
- ✅ Shutdown detaches the callback before the views are destroyed, and waits up
  to 5 s for the thread; on timeout it hands its reference to the thread rather
  than freeing state the thread may still read
- ✅ Graceful failure if hardware is unavailable — `Controller::Initialize`
  journals the failure and continues, so manual barcode entry still works
- ✅ The scanner follows window activation (`WM_ACTIVATE`), because leaving the
  imager armed in a holster is what empties an MC75 battery overnight

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

The thread boundary is the first thing to read here: the decode arrives on the
scanner thread and is handed to the UI thread before anything else happens.

```
┌────────────┐
│    User    │  trigger pull, or the on-screen Scan button
│  Triggers  │
│   Scan     │
└─────┬──────┘
      │
      ▼
┌────────────────────┐
│   ScannerHAL       │   ── SCANNER THREAD ──
│   ReadLabelWait    │
│   DeliverScan()    │
└─────┬──────────────┘
      │ ScanView::ScanThunk: copy the label,
      │ PostMessage(MSG_SCAN_DECODED)
      ▼
┌────────────────────┐
│   ScanView         │   ── UI THREAD from here on ──
│   OnScanReceived   │   shows the barcode, frees the copy
└─────┬──────────────┘
      │ Controller::OnScanReceived(barcode)
      ▼
┌────────────────────┐
│   Controller       │  AUDIT the scan (not queue work)
│   Route event      │  EnsureAuthenticated, then LookupItem
└─────┬──────────────┘
      │
      ├─────────────────────────┐
      │ lookup resolved         │ lookup did not resolve
      ▼                         ▼
┌──────────────┐    ┌──────────────────────────┐
│  HbClient    │    │ Reachable AND authed?    │
│  GetItem()   │    └────┬────────────────┬────┘
└─────┬────────┘         │ yes            │ no
      │                  ▼                ▼
      │         ┌────────────────┐ ┌──────────────┐
      │         │ "Not found -   │ │  SyncEngine  │
      │         │  create it?"   │ │  QueueScan() │
      │         └────────────────┘ └─────┬────────┘
      ▼                                  ▼
┌──────────────┐                  ┌──────────────┐
│  ItemView    │                  │  Journal     │
│  Display     │                  │  TRANS <seq> │
└──────────────┘                  └─────┬────────┘
                                        ▼
                                  ┌──────────────┐
                                  │ Title bar +  │
                                  │ QueueView    │
                                  └──────────────┘

       Later - the 15 s WM_TIMER poll, or the Sync button:
       ┌──────────────────────────────┐
       │ Controller::RunSync          │  refuses to overlap a scan lookup
       └─────┬────────────────────────┘
             ▼
       ┌──────────────────────────────┐
       │ SyncEngine::Sync()           │
       │  · CheckConnectivity()       │  → SYNC_OFFLINE and stop
       │  · GetPendingTransactions()  │
       │  · replay each, one at a time│  → HbClient GetItem / UpdateItemLocation
       │  · MarkTransactionSynced()   │     / UpdateItem
       └─────┬────────────────────────┘
             ▼
       ┌──────────────────────────────┐
       │ SYNC_SUCCESS / SYNC_PARTIAL /│  failures stay queued for the next run
       │ SYNC_FAILED, then refresh UI │
       └──────────────────────────────┘
```

Note what is *not* in the picture: there is no batch endpoint. `Sync()` walks the
pending records and replays each one through the ordinary item API, marking each
acknowledged individually, so a partial success is a normal outcome
(`SYNC_PARTIAL`) rather than an all-or-nothing failure.

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
User action → try the server first
              ├─ call succeeded            → done, nothing is queued
              └─ call failed / unreachable → queue it

Queue stored in the Journal as TRANS records:
  [2026-07-25 09:14:09] TRANS 00000042: [51234] ITEM_SCAN: SCAN:123456789
  [2026-07-25 09:14:31] TRANS 00000043: [73180] ITEM_UPDATE: UPDATE:{"id":"123",...}
```

The client does not ask "am I online?" and then choose a path. It attempts the
call, and queues only what actually failed — a probe that says "online" seconds
before a request that times out would otherwise lose the scan.

One escape hatch: if `offlineModeEnabled` is `false` in `hb_conf.json` the scan is
**discarded** instead of queued, and the operator is told so immediately. That is
the point of the setting — an installation that does not want deferred work
should not discover an empty queue at the end of a shift.

#### **2. Connectivity Detection**
```cpp
CheckConnectivity():
    1. gethostbyname() on the host from apiBaseUrl
    2. Resolves    → online
    3. Fails       → offline
    4. Cache the answer for 5 seconds; a sync in which everything failed
       discards the cache immediately
```

It is used to decide *how to interpret* a failed lookup — "no such item" is a
conclusion only a device that can reach the server is allowed to draw — and to
short-circuit a sync that has no chance of working.

#### **3. Automatic Sync**
```
WM_TIMER on the main window, every 15 s (AUTOSYNC_TICK_MS):
  └─ skip entirely if a scan lookup or a sync is already running
  └─ SyncEngine::ShouldAutoSync(GetTickCount()):
      ├─ auto-sync enabled (syncIntervalSeconds > 0)?
      ├─ work queued?
      └─ first attempt, or syncIntervalSeconds elapsed?  (wrap-safe comparison)
          └─ Controller::RunSync(interactive = false)
```

A queue recovered from disk at startup is due immediately rather than after a
full interval — the app restarts on every battery swap, and waiting five minutes
each time would be the common case, not the rare one. A background sync never
raises a dialog: the device is usually in a holster with nobody to dismiss it.

#### **4. User-Initiated Sync**
```
User taps "Sync" (QueueView or the soft-key menu):
  └─ QueueView → Controller::OnSyncRequested → RunSync(interactive = true)
      └─ same code path, but failures are reported in a message box
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

#### **String Management — `HBX::Str` owns the bounded cases**

`include/StrUtil.hpp` is the one place that knows how to move text safely. It
exists because the alternative — formatting caller- or server-supplied text into
a fixed stack buffer with `wsprintf` — is not merely ugly here, it is wrong:
**on Windows CE `wsprintf` stops after 1024 characters**, so it silently
truncates on the device, and that cap does not bound a call that starts partway
into a buffer at all.

```cpp
// Fixed destination: bounded, always NUL-terminated, reports truncation.
TCHAR title[128];
Str::Copy(title, 128, TEXT("HomeBox Client"));
Str::Append(title, 128, TEXT(" [Queue: "));
Str::AppendInt(title, 128, queueDepth);
Str::Append(title, 128, TEXT("]"));

// Unbounded content: grow instead of guessing a size. Allocation failure is
// sticky, so one check at the end covers every append.
Str::Buffer body;
body.AppendChar((TCHAR)'{');
body.AppendJsonPair(TEXT("deviceId"), deviceId);   // value is escaped
body.AppendChar((TCHAR)'}');
if (body.Failed()) { return false; }
```

The same header owns the JSON escaping and the real UTF-8 ⇄ TCHAR conversions
used by the journal, both HTTP transports and `Config`. One implementation
serves both TCHAR widths (device `WCHAR`, host `char`) — the width is inspected
with `sizeof()` and the compiler folds the branch — so there is no `#ifdef`'d
copy that can rot in the configuration nobody builds.

#### **Ownership of Heap Returns**
```cpp
// Buffer::Detach hands over its storage; the caller deletes it.
TCHAR* Item::ToJson() const {
    Str::Buffer out;
    // ... append escaped fields ...
    if (out.Failed()) { return NULL; }   // NULL means "could not build it"
    return out.Detach();
}

TCHAR* json = item.ToJson();
if (json) {
    // ... use json ...
    delete[] json;
}
```

Multi-level returns state their contract in the header. `Journal::GetPendingTransactions`
hands back a `TCHAR*[]` of heap strings: the caller must `delete[]` each entry and
then the array — including the empty case, where the array is still allocated.

**Memory Leak Prevention**:
- ✅ Destructors do the cleanup; `Controller::Shutdown` is idempotent so the
  failure paths in `Initialize` release the same components the normal exit does
- ✅ Every owner class here is non-copyable (private copy ctor / assignment), so
  a stray copy cannot produce a double `delete[]`
- ✅ Ownership transfer is documented at the declaration, not inferred at the
  call site
- ✅ A posted `MSG_SCAN_DECODED` frees its payload in the window procedure even
  when the view has already detached — a message in flight still owns memory

---

## 🧵 Threading Model

### UI Thread + Background Scanner

**UI Thread**:
- Windows message loop
- UI updates
- All business logic
- API calls — blocking, and deliberately so: they run here, not on the scan
  thread, and `Controller` refuses to start a sync while a lookup is in flight
  (`m_busy`) because both drive the single `HbClient`

**Scanner Thread** (device build only, `HBX_USE_EMDK`):
- Dedicated thread for hardware polling
- Blocks on `SCAN_ReadLabelWait(..., 1000)` — the 1 s timeout is what lets it
  notice a shutdown request
- Delivers the decode by calling the registered callback **on this thread**
- Never touches a window, never makes an HTTP call

**Thread Communication** — the marshalling is the view's job:
```cpp
// Scanner thread: ScannerHAL::DeliverScan invokes the callback here.
void ScanView::ScanThunk(const TCHAR* barcode, void* userData) {
    ScanView* self = (ScanView*)userData;
    TCHAR* copy = Str::Dup(barcode);            // the decode buffer is reused
    if (!PostMessage(self->m_hwnd, MSG_SCAN_DECODED, 0, (LPARAM)copy)) {
        delete[] copy;                          // nobody will receive it
    }
}

// UI thread: the window procedure owns the copy and frees it.
case MSG_SCAN_DECODED: {
    TCHAR* barcode = (TCHAR*)lParam;
    if (pThis) { pThis->OnScanReceived(barcode); }   // → Controller
    delete[] barcode;
    return 0;
}
```

Doing the work directly in the callback is what this replaces, and it was not a
style problem: it ran the journal write, a blocking HTTP lookup and a modal
message box on the scanner thread — driving windows owned by the UI thread from
a worker, and sharing one `HttpClient` socket between two threads.

**Synchronization** — message passing is the *main* mechanism, not the only one:
| Shared thing | Guard | Why it is shared |
|---|---|---|
| `Journal` (file handle, counters) | private `CRITICAL_SECTION` on every public method | the scan thread and the UI thread both journal |
| `ScannerHAL::SharedState` (callback + `userData`, last barcode) | `CRITICAL_SECTION`, plus a reference count on the state itself | the monitor thread outlives an early `ScannerHAL` deletion |
| `HttpClient` (socket, header list) | `CRITICAL_SECTION` | it protects the instance from corruption; it does **not** make concurrent requests meaningful — one request at a time per instance, one instance per thread if you need parallelism |

**Consequences**:
- ✅ All application state is still reached from the UI thread only
- ✅ A decode arriving during teardown is either dropped or freed, never
  delivered to a destroyed view
- ⚠️ A long API call blocks the UI thread. That is the accepted trade for a
  single-`HbClient` design; the scan-timeout timer and the busy flag exist so the
  screen never *looks* wedged

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

### Why UI Thread + Scanner Thread?

**Decision**: One UI thread that owns the application state, plus a dedicated
scanner polling thread that only reads labels

**Rationale**:
- Hardware requirement — the EMDK read is blocking, so it cannot live in the
  message loop
- UI responsiveness (a 1 s blocking read does not freeze the screen)
- Keeping *all* work on the UI thread means one `HbClient`, one socket, and one
  owner of every view

**Trade-off**: the decode has to be marshalled across the boundary — a heap copy
and a `PostMessage` per scan — and the few objects both threads touch
(`Journal`, the scanner's shared state, `HttpClient`) need real critical
sections. That is cheaper than making the whole application thread-safe, and far
cheaper than the alternative it replaced, which was to run the business logic on
whichever thread happened to deliver the barcode.

---

## 📐 Future Design Considerations

### Potential Enhancements

1. **Multi-View Navigation**
   - Tab-based interface
   - View stack for navigation history

2. **Batched Sync Endpoint**
   - Replay the queue in one request instead of one call per entry
   - (Periodic auto-sync itself is implemented — see Offline-First Strategy)

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
