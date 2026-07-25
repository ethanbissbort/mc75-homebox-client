#include "../include/ScannerHAL.hpp"
#include "../include/StrUtil.hpp"

// The host shim declares WaitForSingleObject but not the wait result codes;
// the real windows.h does, so these definitions only ever apply off-device.
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 ((DWORD)0x00000000)
#endif

namespace HBX {

namespace {

// Shutdown budget for the monitor thread. The loop's blocking read uses a 1s
// timeout, so a healthy thread exits well inside this; the margin covers a
// decode callback that is still running.
const DWORD kThreadJoinTimeoutMs = 5000;

} // namespace

ScannerHAL::ScannerHAL()
    : m_state(CreateState())
    , m_initialized(false)
    , m_scanThread(NULL)
{
}

ScannerHAL::~ScannerHAL()
{
    Shutdown();

    // Drops this object's reference. If a wedged monitor thread still holds
    // one, it frees the state (and the scanner) when it finally exits.
    ReleaseState(m_state);
    m_state = NULL;
}

ScannerHAL::SharedState* ScannerHAL::CreateState()
{
    SharedState* state = new SharedState;
    if (!state) {
        return NULL;
    }

    state->refCount = 1;
    state->running = 0;
    state->enabled = 0;
    InitializeCriticalSection(&state->lock);
    state->callback = NULL;
    state->callbackUserData = NULL;
    state->lastBarcode[0] = (TCHAR)'\0';
    state->hasLastBarcode = false;
    state->scannerHandle = NULL;
#ifdef HBX_USE_EMDK
    state->scanBuffer = NULL;
#else
    state->simSequence = 0;
#endif

    return state;
}

void ScannerHAL::AddRefState(SharedState* state)
{
    if (state) {
        InterlockedIncrement(&state->refCount);
    }
}

void ScannerHAL::ReleaseState(SharedState* state)
{
    if (!state) {
        return;
    }

    if (InterlockedDecrement(&state->refCount) != 0) {
        return;
    }

    // Last owner out closes the hardware. On a shutdown whose join timed out
    // that owner is the monitor thread itself, which is exactly the point: the
    // decode buffer and the scanner handle survive until nothing can read them.
    ReleaseHardware(state);
    DeleteCriticalSection(&state->lock);
    delete state;
}

void ScannerHAL::ReleaseHardware(SharedState* state)
{
    if (!state) {
        return;
    }

#ifdef HBX_USE_EMDK
    if (state->scanBuffer) {
        SCAN_DeallocateBuffer(state->scanBuffer);
        state->scanBuffer = NULL;
    }
    if (state->scannerHandle) {
        SCAN_Close((SCAN_HANDLE)state->scannerHandle);
    }
#endif
    state->scannerHandle = NULL;
}

void ScannerHAL::DeliverScan(SharedState* state, const TCHAR* barcode)
{
    if (!state || !barcode) {
        return;
    }

    ScanCallback callback = NULL;
    void* userData = NULL;

    EnterCriticalSection(&state->lock);
    Str::Copy(state->lastBarcode, (int)MAX_BARCODE_CHARS, barcode);
    state->hasLastBarcode = true;
    // Snapshot the pair together: SetScanCallback publishes both under this
    // same lock, so a decode can never pair a new callback with a stale
    // userData (or vice versa).
    callback = state->callback;
    userData = state->callbackUserData;
    LeaveCriticalSection(&state->lock);

    // Invoked outside the lock: the handler runs the whole scan pipeline and
    // must not be able to block GetLastScan or SetScanCallback while it does.
    if (callback) {
        callback(barcode, userData);
    }
}

bool ScannerHAL::Initialize()
{
    if (m_initialized) {
        return true;
    }

    if (!m_state) {
        return false;
    }

    if (!OpenScanner()) {
        return false;
    }

#ifdef HBX_USE_EMDK
    // The monitor thread owns a reference for its whole life, so deleting the
    // ScannerHAL mid-decode cannot pull the state out from under it.
    InterlockedExchange(&m_state->running, 1);
    AddRefState(m_state);

    m_scanThread = CreateThread(
        NULL,
        0,
        ScanThread,
        m_state,
        0,
        NULL
    );

    if (!m_scanThread) {
        InterlockedExchange(&m_state->running, 0);
        ReleaseState(m_state);
        CloseScanner();
        return false;
    }
#else
    // The simulation build has no hardware to poll: barcodes arrive through
    // InjectScan / TriggerScan on the caller's thread, so no monitor thread is
    // started (an empty polling loop would just burn battery).
#endif

    m_initialized = true;
    return true;
}

bool ScannerHAL::Shutdown()
{
    if (!m_initialized) {
        return true;
    }

    DisableScanner();
    CloseScanner();
    m_initialized = false;
    return true;
}

bool ScannerHAL::IsInitialized() const
{
    return m_initialized;
}

bool ScannerHAL::EnableScanner()
{
    if (!m_initialized || !m_state || !m_state->scannerHandle) {
        return false;
    }

    // Enable scanner via EMDK
#ifdef HBX_USE_EMDK
    DWORD result = SCAN_Enable((SCAN_HANDLE)m_state->scannerHandle);
    if (result != E_SCN_SUCCESS) {
        return false;
    }
#endif

    InterlockedExchange(&m_state->enabled, 1);
    return true;
}

bool ScannerHAL::DisableScanner()
{
    if (!m_initialized) {
        return true; // Already disabled
    }

    if (!m_state || !m_state->scannerHandle) {
        return false;
    }

    // Disable scanner via EMDK
#ifdef HBX_USE_EMDK
    SCAN_Disable((SCAN_HANDLE)m_state->scannerHandle);
#endif

    InterlockedExchange(&m_state->enabled, 0);
    return true;
}

bool ScannerHAL::TriggerScan()
{
    if (!m_state || !m_state->enabled || !m_state->scannerHandle) {
        return false;
    }

    // Trigger scan via EMDK
#ifdef HBX_USE_EMDK
    // Flush any stale/pending decode data so the next read is fresh, then
    // pull the soft trigger to fire the beam. The decoded label is captured
    // by the scan thread's blocking SCAN_ReadLabelWait().
    SCAN_Flush((SCAN_HANDLE)m_state->scannerHandle);
    // Some EMDK/SMDK versions want an explicit release (FALSE) before the
    // pull (TRUE); do both so the beam re-arms on repeated triggers.
    SCAN_SetSoftTrigger((SCAN_HANDLE)m_state->scannerHandle, FALSE);
    DWORD result = SCAN_SetSoftTrigger((SCAN_HANDLE)m_state->scannerHandle, TRUE);
    return (result == E_SCN_SUCCESS);
#else
    // Simulation: synthesise a decode so the emulator/host build exercises the
    // same store-last + callback path the device does. Delivery is synchronous
    // here, whereas the device delivers from the monitor thread.
    TCHAR barcode[32];
    barcode[0] = (TCHAR)'\0';
    Str::Append(barcode, 32, TEXT("SIM"));
    Str::AppendInt(barcode, 32, (long)InterlockedIncrement(&m_state->simSequence));

    DeliverScan(m_state, barcode);
    return true;
#endif
}

bool ScannerHAL::InjectScan(const TCHAR* barcode)
{
    if (!m_state || !barcode || barcode[0] == (TCHAR)'\0') {
        return false;
    }

    DeliverScan(m_state, barcode);
    return true;
}

bool ScannerHAL::GetLastScan(TCHAR* barcode, DWORD maxLen)
{
    if (!barcode || maxLen == 0 || !m_state) {
        return false;
    }

    int cap = (maxLen > (DWORD)0x7FFFFFFF) ? 0x7FFFFFFF : (int)maxLen;
    bool haveScan = false;

    // Locked: on the device the monitor thread overwrites this buffer from
    // under the UI thread.
    EnterCriticalSection(&m_state->lock);
    if (m_state->hasLastBarcode) {
        Str::Copy(barcode, cap, m_state->lastBarcode);
        haveScan = true;
    }
    LeaveCriticalSection(&m_state->lock);

    return haveScan;
}

const TCHAR* ScannerHAL::GetScannerStatus() const
{
    if (!m_initialized || !m_state) {
        return TEXT("Not initialized");
    }
    if (!m_state->enabled) {
        return TEXT("Disabled");
    }
    return TEXT("Ready");
}

bool ScannerHAL::SetScanMode(int mode)
{
    if (!m_initialized || !m_state || !m_state->scannerHandle) {
        return false;
    }

    // Set scan mode via EMDK
    // mode: 0 = continuous, 1 = single scan
#ifdef HBX_USE_EMDK
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_state->scannerHandle, &params) != E_SCN_SUCCESS) {
        return false;
    }
    // 0 = continuous -> level trigger; 1 = single -> one-shot trigger.
    params.dwTriggerMode = (mode == 0) ? TRIG_MODE_LEVEL : TRIG_MODE_ONESHOT;
    return (SCAN_SetParameters((SCAN_HANDLE)m_state->scannerHandle, &params) == E_SCN_SUCCESS);
#else
    (void)mode;
    return true;
#endif
}

bool ScannerHAL::SetBeepEnabled(bool enabled)
{
    if (!m_initialized || !m_state || !m_state->scannerHandle) {
        return false;
    }

    // Configure beep via EMDK
#ifdef HBX_USE_EMDK
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_state->scannerHandle, &params) != E_SCN_SUCCESS) {
        return false;
    }
    params.dwDecodeBeepEnable    = enabled ? 1 : 0;
    params.dwDecodeBeepTime      = 200;   // Duration in milliseconds
    params.dwDecodeBeepFrequency = 2500;  // Frequency in Hz
    return (SCAN_SetParameters((SCAN_HANDLE)m_state->scannerHandle, &params) == E_SCN_SUCCESS);
#else
    (void)enabled;
    return true;
#endif
}

bool ScannerHAL::SetVibrateEnabled(bool enabled)
{
    if (!m_initialized || !m_state || !m_state->scannerHandle) {
        return false;
    }

    // Configure vibrate via EMDK
#ifdef HBX_USE_EMDK
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_state->scannerHandle, &params) != E_SCN_SUCCESS) {
        return false;
    }
    params.dwDecodeVibrateEnable = enabled ? 1 : 0;
    params.dwDecodeVibrateTime   = 200;  // Duration in milliseconds
    return (SCAN_SetParameters((SCAN_HANDLE)m_state->scannerHandle, &params) == E_SCN_SUCCESS);
#else
    (void)enabled;
    // Alternative on a real device: drive the vibrator directly via the
    // Windows Mobile Vibrate() / led notification API if the scanner does not
    // own the motor.
    return true;
#endif
}

void ScannerHAL::SetScanCallback(ScanCallback callback, void* userData)
{
    if (!m_state) {
        return;
    }

    // The monitor thread is already running by the time the views register
    // themselves, so the function and its context have to be published as one
    // unit or a decode landing in between calls the new callback with the old
    // userData.
    EnterCriticalSection(&m_state->lock);
    m_state->callback = callback;
    m_state->callbackUserData = userData;
    LeaveCriticalSection(&m_state->lock);
}

bool ScannerHAL::OpenScanner()
{
    if (!m_state) {
        return false;
    }

#ifdef HBX_USE_EMDK
    // Open the default scanner. "SCN1:" is the primary scanner port exposed by
    // the Symbol/Zebra driver on the MC75; SCAN_Open returns a HANDLE.
    HANDLE hScanner = NULL;
    DWORD result = SCAN_Open(TEXT("SCN1:"), &hScanner);
    if (result != E_SCN_SUCCESS || hScanner == NULL) {
        return false;
    }
    m_state->scannerHandle = hScanner;

    // Configure scanner defaults: level (continuous) trigger with the decode
    // beep on, vibrate off. Best-effort - a config failure is not fatal.
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_state->scannerHandle, &params) == E_SCN_SUCCESS) {
        params.dwTriggerMode         = TRIG_MODE_LEVEL;
        params.dwDecodeBeepEnable    = 1;
        params.dwDecodeBeepTime      = 200;
        params.dwDecodeBeepFrequency = 2500;
        params.dwDecodeVibrateEnable = 0;
        params.dwDecodeVibrateTime   = 200;
        SCAN_SetParameters((SCAN_HANDLE)m_state->scannerHandle, &params);
    }

    // Allocate the reusable decode buffer used by the scan thread. TRUE selects
    // a text (as opposed to raw/binary) buffer format.
    m_state->scanBuffer = SCAN_AllocateBuffer(TRUE, SCAN_MAX_LABEL_LEN);
    if (m_state->scanBuffer == NULL) {
        SCAN_Close((SCAN_HANDLE)m_state->scannerHandle);
        m_state->scannerHandle = NULL;
        return false;
    }

    return true;
#else
    // Simulation: no EMDK to open, so stand in a non-NULL handle to keep the
    // enable/trigger state machine (and the views driving it) honest.
    m_state->scannerHandle = (HANDLE)0x12345678;
    return true;
#endif
}

void ScannerHAL::CloseScanner()
{
    if (!m_state) {
        return;
    }

    InterlockedExchange(&m_state->running, 0);

    bool threadStopped = true;
    if (m_scanThread) {
        threadStopped = (WaitForSingleObject(m_scanThread, kThreadJoinTimeoutMs) == WAIT_OBJECT_0);
        CloseHandle(m_scanThread);
        m_scanThread = NULL;
    }

    if (threadStopped) {
        // Nothing can reach the hardware any more, so release it here and keep
        // the state (with its callback registration) for a later Initialize().
        ReleaseHardware(m_state);
        return;
    }

    // The thread is still inside the driver - a decode callback that blocks on
    // the network or on a modal prompt makes this routine rather than
    // exceptional. Deallocating the decode buffer or closing the handle now
    // would be a use-after-free on a device that is merely slow, so hand this
    // reference to the thread instead: it releases the state, and with it the
    // hardware, when it finally leaves the loop. A fresh state carries the
    // callback registration forward so the object stays usable.
    SharedState* orphan = m_state;
    SharedState* fresh = CreateState();

    EnterCriticalSection(&orphan->lock);
    if (fresh) {
        fresh->callback = orphan->callback;
        fresh->callbackUserData = orphan->callbackUserData;
    }
    // Unregister on the orphan so a late decode cannot call into a view that
    // is about to be destroyed. The thread snapshots the pair under this same
    // lock, so once we are past here it can no longer start a callback.
    orphan->callback = NULL;
    orphan->callbackUserData = NULL;
    LeaveCriticalSection(&orphan->lock);

    m_state = fresh;
    ReleaseState(orphan);
}

DWORD WINAPI ScannerHAL::ScanThread(LPVOID param)
{
#ifdef HBX_USE_EMDK
    SharedState* state = (SharedState*)param;
    if (!state) {
        return 1;
    }

    // Monitor loop: poll the EMDK for a decoded label and deliver it. `running`
    // is volatile and cleared with InterlockedExchange, so the compiler cannot
    // hoist the load out of the loop.
    while (state->running) {
        // Only issue reads while the scanner is enabled; otherwise idle so we
        // don't spin on immediate error returns.
        if (!state->enabled || state->scannerHandle == NULL ||
            state->scanBuffer == NULL) {
            Sleep(100);
            continue;
        }

        // Blocking read with a ~1s timeout. SCAN_ReadLabelWait returns when a
        // label is decoded (E_SCN_SUCCESS) or the timeout elapses
        // (E_SCN_READTIMEOUT), which keeps the loop responsive to shutdown.
        //
        // NOTE: The task references SCAN_ReadLabelMsg. That is the asynchronous
        // variant which arms the beam and POSTs a window message on completion,
        // so it needs an HWND + message pump. For a self-contained background
        // polling thread the correct real CAPI call is the blocking
        // SCAN_ReadLabelWait(handle, buffer, timeoutMs) - it delivers exactly
        // the "on E_SCN_SUCCESS with data" semantics used below. Both symbols
        // are declared in the shim / vendor header.
        DWORD result = SCAN_ReadLabelWait((SCAN_HANDLE)state->scannerHandle,
                                          state->scanBuffer,
                                          1000); // 1 second timeout

        if (result == E_SCN_SUCCESS) {
            DWORD dataLen = SCNBUF_GETLEN(state->scanBuffer);
            const char* rawData = (const char*)SCNBUF_GETDATA(state->scanBuffer);

            if (dataLen > 0 && rawData != NULL) {
                // Convert the decoded bytes to TCHAR. On-device TCHAR is WCHAR,
                // so MultiByteToWideChar performs the real narrow->wide
                // conversion; on the host TCHAR is char and it is a bounded
                // copy. Clamp to the local buffer and NUL-terminate.
                const int kMaxChars = (int)MAX_BARCODE_CHARS - 1;
                TCHAR barcode[MAX_BARCODE_CHARS];
                int copyLen = (dataLen < (DWORD)kMaxChars) ? (int)dataLen : kMaxChars;
                int cch = MultiByteToWideChar(CP_ACP, 0, rawData, copyLen,
                                              barcode, kMaxChars);
                if (cch < 0) cch = 0;
                if (cch > kMaxChars) cch = kMaxChars;
                barcode[cch] = (TCHAR)0;

                DeliverScan(state, barcode);
            }
        }
        // On timeout / other codes just loop again and re-check the run flag.
    }

    // Releases the thread's own reference; frees the state (and the scanner)
    // if the ScannerHAL gave up waiting and already let go of its own.
    ReleaseState(state);
    return 0;
#else
    // No monitor thread exists in the simulation build - scans are delivered
    // synchronously by InjectScan / TriggerScan - so this is never entered. It
    // stays compiled so both configurations keep type-checking the entry point.
    (void)param;
    return 0;
#endif
}

} // namespace HBX
