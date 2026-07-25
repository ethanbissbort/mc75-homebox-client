# 📷 Real Scanning, HTTPS, and the Live UI

This document covers the device-only features that turn HBXClient from a
scaffold into a working MC75 application: **real barcode scanning** via the
Zebra/Symbol Scanner C API, **HTTP + HTTPS** networking via WinInet, and the
**wired-up UI** (the views are created, shown, and connected to the scanner and
sync engine).

All three are controlled by two preprocessor switches, which are **enabled by
default** in `proj/HBXClient.vcproj`:

| Macro | Default | What it turns on | Extra dependency |
|-------|:-------:|------------------|------------------|
| `HBX_USE_EMDK` | **on** | Real barcode scanning through `ScanCAPI` | Zebra **EMDK for C** (install + link) |
| `HBX_USE_WININET` | **on** | Real HTTP **and HTTPS/TLS** transport | none — `wininet.lib` ships with the SDK |

When a macro is **off**, the code falls back to a portable path that still
compiles and runs the host tests: a **simulation scanner** and a **WinSock,
HTTP-only** transport. This is exactly how the Linux host test harness builds
(both macros off) — see [BUILD.md → Host Build & Testing](BUILD.md#-host-build--testing).

---

## 1. Barcode scanning (`HBX_USE_EMDK`)

### What it does

`src/ScannerHAL.cpp` drives the MC75's imager through the Scanner C API
("ScanCAPI", part of Zebra's **EMDK for C** / the older Symbol **SMDK for C**):

| ScannerHAL method | Real ScanCAPI calls |
|-------------------|---------------------|
| `OpenScanner()` | `SCAN_Open("SCN1:")`, `SCAN_GetParameters`/`SCAN_SetParameters` (defaults), `SCAN_AllocateBuffer` |
| `EnableScanner()` / `DisableScanner()` | `SCAN_Enable` / `SCAN_Disable` |
| `TriggerScan()` | `SCAN_Flush`, then `SCAN_SetSoftTrigger(handle, FALSE)` followed by `(handle, TRUE)` — some EMDK versions want the explicit release before the pull, or the beam does not re-arm on a repeated trigger |
| `SetScanMode(0/1)` | `SCAN_SetParameters` → `TRIG_MODE_LEVEL` (continuous) / `TRIG_MODE_ONESHOT` (single) |
| `SetBeepEnabled` / `SetVibrateEnabled` | `SCAN_SetParameters` beep / vibrate fields |
| background `ScanThread()` | `SCAN_ReadLabelWait(handle, buf, 1000ms)`; on `E_SCN_SUCCESS`, `SCNBUF_GETDATA`/`SCNBUF_GETLEN` → `MultiByteToWideChar` → fire the scan callback |
| `CloseScanner()` | `SCAN_DeallocateBuffer`, `SCAN_Close` |

### Enabling the scanner

Opening the scanner is not enough to make it read anything: `SCAN_Enable` is what
lets the monitor thread's reads return labels, and nothing used to call it.
`Controller::Initialize` now enables the scanner right after configuring beep and
vibrate, so the **physical trigger works from startup** — no soft trigger and no
visit to the Scan button required. A failure there is journalled
(`SCANNER_ENABLE`) and the app keeps running, because typing a barcode by hand is
the fallback for a cracked scan window.

The scanner then follows window activation (`WM_ACTIVATE` →
`Controller::OnActivate`): disabled when the app goes to the background,
re-enabled when it comes back. Leaving the imager armed while the MC75 sits in a
holster is what empties the battery overnight.

### How a decode reaches the application

```
imager
  └─ scan thread: SCAN_ReadLabelWait → SCNBUF_GETDATA → MultiByteToWideChar
       └─ ScannerHAL::DeliverScan → the registered callback, ON THE SCAN THREAD
            └─ ScanView::ScanThunk: Str::Dup the label,
               PostMessage(MSG_SCAN_DECODED = WM_APP+1, LPARAM = the copy)
                 └─ UI thread: ScanView::OnScanReceived shows it and frees the copy
                      └─ Controller::OnScanReceived → journal, lookup, queue
```

The hand-off matters: the callback fires on the EMDK scan thread, and everything
downstream of it (a journal write, a blocking HTTP lookup, a modal dialog) must
not run there. The view copies the label and posts it, so all of that happens on
the UI thread instead. The `Lookup` button takes the same path from
`OnScanReceived` onward, which is why a typed barcode behaves exactly like a
decoded one.

### Build setup (Windows 7 + VS2008)

1. Install the **Zebra EMDK for C** (a.k.a. Motorola/Symbol EMDK for C).
2. In `proj/HBXClient.vcproj` (already done for you), keep `HBX_USE_EMDK` in
   *C/C++ → Preprocessor → Preprocessor Definitions*.
3. Point the project at your EMDK install (versions differ, so fix these to
   match your machine):
   - *C/C++ → General → Additional Include Directories* → the folder containing
     **`ScanCAPI.h`** (e.g. `C:\Program Files\Motorola EMDK for C\v2.x\Include`).
   - *Linker → General → Additional Library Directories* → the matching
     **WM65\ARMV4I** lib folder.
   - *Linker → Input → Additional Dependencies* → **`ScanAPIWM.lib`** (already
     added). Older Symbol SMDK builds instead ship `ScanAPI.lib` / `SCNAPI32.lib`
     — use whichever your EMDK provides.

### ⚠️ Version-specific knobs to verify against your EMDK

The EMDK's exact symbols vary slightly by version. If the device build does not
compile/link, check these (all are isolated in `src/ScannerHAL.cpp` /
`tests/host/shim/ScanCAPI.h`):

- The blocking read is **`SCAN_ReadLabelWait`** (not the async, window-message
  `SCAN_ReadLabelMsg`, which needs an HWND + message pump). Both are declared.
- `SCAN_GetParameters` / `SCAN_SetParameters` are named
  `SCAN_GetScanParameters` / `SCAN_SetScanParameters` in some EMDK versions.
- The `SCAN_PARAMS` field names for trigger mode / beep / vibrate.
- The `E_SCN_READTIMEOUT` numeric value and the `SCAN_MAX_LABEL_LEN` size.
- The device name string (`"SCN1:"`) if your unit enumerates a different port.

### Build without a scanner (emulator / CI)

Remove `HBX_USE_EMDK` from the project's Preprocessor Definitions. `ScannerHAL`
then uses its **simulation** path: it stands in a non-NULL handle so the
enable/trigger state machine stays honest, and **no monitor thread is created at
all** — an idle polling loop would only cost battery. Scans instead arrive
synchronously, on the caller's thread, from `TriggerScan()` (which synthesises
`SIM1`, `SIM2`, …) or from `InjectScan()`. The app builds and runs in the Windows
Mobile emulator with no EMDK installed, and everything except live scanning
works.

---

## 2. HTTP + HTTPS transport (`HBX_USE_WININET`)

`src/HttpClient.cpp` has two transports behind one public API
(`Get/Post/Put/Delete` filling an `HttpResponse`):

- **WinInet** (`HBX_USE_WININET`, default) — `InternetOpen` → `InternetConnect`
  → `HttpOpenRequest` (adds `INTERNET_FLAG_SECURE` for `https://` or port 443)
  → `HttpAddRequestHeaders` → `HttpSendRequest` → status via `HttpQueryInfo` →
  body via `InternetReadFile`. This gives **real TLS**, chunked transfer, and
  redirect handling from the OS stack. Only needs `wininet.lib`, which is part
  of the Windows Mobile SDK — **no extra install**.
- **WinSock** (fallback, macro off) — the original hand-rolled HTTP/1.1 over raw
  sockets. **HTTP only** (it cannot do TLS), but it is fully portable and is the
  path exercised by the host unit tests (`tests/unit/test_http.cpp`).

To force the portable HTTP-only path (e.g. debugging without WinInet), remove
`HBX_USE_WININET` from the Preprocessor Definitions.

---

## 3. Wired-up UI

`Controller` now creates and connects the views instead of leaving them
orphaned:

- Creates `ScanView`, `QueueView`, and `ItemView` as child windows of the main
  window (`Controller::CreateViews`).
- **ScanView** ← the scanner (via `SetScanner`) and forwards decoded barcodes to
  `Controller::OnScanReceived`.
- **QueueView** ← the `SyncEngine`; its Sync button calls
  `Controller::OnSyncRequested`, and `RefreshQueue()` lists the real pending
  transactions from `SyncEngine::GetQueuedTransactions`.
- **ItemView** ← save events flow to `Controller::OnItemSave`
  (`HbClient::CreateItem`/`UpdateItem`, or queue an `ITEM_UPDATE` transaction
  when offline).
- A Windows Mobile **soft-key menu bar** (`SHCreateMenuBar` + the `IDR_MENUBAR`
  resource in `resources/layout.rc`) switches between Scan/Queue and triggers a
  sync. Menu commands are handled in `Controller::WindowProc` (`WM_COMMAND`).

### Offline transaction formats (produced by scans / edits, consumed by `SyncEngine::ProcessQueuedTransaction`)

```
ITEM_SCAN     data = "SCAN:<barcode>"
              data = "SCANLOC:<barcodeLength>:<barcode><locationId>"
ITEM_UPDATE   data = "UPDATE:<item-json>"   e.g. UPDATE:{"id":"42","barcode":"123","name":"Widget","quantity":3}
```

The location form is length-prefixed rather than separator-delimited: Code 128
and QR can both encode `:` and `@`, so any delimiter is a barcode a customer will
eventually scan. Replay reads `<barcodeLength>` characters as the barcode and
takes the remainder as the location id.

`SyncEngine::QueueTransaction` wraps these as `[tick] TYPE: DATA`, and the
journal wraps *that* in its own `TRANS <seq>:` record header — so a line handed
back by `GetQueuedTransactions` carries all three layers, and
`Journal::PayloadOf` strips the outermost one before dispatch.

---

## 4. What still needs a real device

The host harness compile-checks **both** the default and device paths
(`make -C tests/host check` runs `compile-all` + `compile-device` + the tests),
but the EMDK, WinInet, and windowing behavior can only be *exercised* on an MC75
(or the WM6.5 emulator, minus the scanner). Validate on-device:

- A live trigger pull decodes and populates the scan display **without** first
  pressing the on-screen Scan button (that is what enabling at startup buys).
- The app stays responsive while a lookup runs, and backgrounding it stops the
  imager (check the scanner LED, or battery drain over a shift).
- HTTPS requests to your HomeBox API succeed (correct cert handling).
- The soft-key menu switches views and the queue list shows pending items.

---

[← Back to BUILD.md](BUILD.md) · [Windows 7 setup →](WINDOWS7_BUILD.md)
