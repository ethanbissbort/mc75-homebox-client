#include "../include/ScannerHAL.hpp"

namespace HBX {

ScannerHAL::ScannerHAL()
    : m_scannerHandle(NULL)
    , m_initialized(false)
    , m_enabled(false)
    , m_lastBarcode(NULL)
    , m_callback(NULL)
    , m_callbackUserData(NULL)
    , m_scanThread(NULL)
    , m_scanThreadRunning(false)
#ifdef HBX_USE_EMDK
    , m_scanBuffer(NULL)
#endif
{
}

ScannerHAL::~ScannerHAL()
{
    Shutdown();
    if (m_lastBarcode) {
        delete[] m_lastBarcode;
    }
}

bool ScannerHAL::Initialize()
{
    if (m_initialized) {
        return true;
    }

    // Initialize Zebra EMDK
    // In a real implementation, this would call EMDK_Init() or similar
    // For MC75 with Symbol/Zebra scanner, we'd typically use:
    // - SCAN_Open() from the Symbol scanner API
    // - Or EMDK Manager initialization for newer EMDK versions

    if (!OpenScanner()) {
        return false;
    }

    // Start scan monitoring thread
    m_scanThreadRunning = true;
    m_scanThread = CreateThread(
        NULL,
        0,
        ScanThread,
        this,
        0,
        NULL
    );

    if (!m_scanThread) {
        CloseScanner();
        return false;
    }

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
    if (!m_initialized) {
        return false;
    }

    // Enable scanner via EMDK
    if (m_scannerHandle) {
#ifdef HBX_USE_EMDK
        DWORD result = SCAN_Enable((SCAN_HANDLE)m_scannerHandle);
        if (result != E_SCN_SUCCESS) {
            return false;
        }
#endif
        m_enabled = true;
        return true;
    }

    return false;
}

bool ScannerHAL::DisableScanner()
{
    if (!m_initialized) {
        return true; // Already disabled
    }

    // Disable scanner via EMDK
    if (m_scannerHandle) {
#ifdef HBX_USE_EMDK
        SCAN_Disable((SCAN_HANDLE)m_scannerHandle);
#endif
        m_enabled = false;
        return true;
    }

    return false;
}

bool ScannerHAL::TriggerScan()
{
    if (!m_enabled || !m_scannerHandle) {
        return false;
    }

    // Trigger scan via EMDK
#ifdef HBX_USE_EMDK
    // Flush any stale/pending decode data so the next read is fresh, then
    // pull the soft trigger to fire the beam. The decoded label is captured
    // by the scan thread's blocking SCAN_ReadLabelWait().
    SCAN_Flush((SCAN_HANDLE)m_scannerHandle);
    // Some EMDK/SMDK versions want an explicit release (FALSE) before the
    // pull (TRUE); do both so the beam re-arms on repeated triggers.
    SCAN_SetSoftTrigger((SCAN_HANDLE)m_scannerHandle, FALSE);
    DWORD result = SCAN_SetSoftTrigger((SCAN_HANDLE)m_scannerHandle, TRUE);
    return (result == E_SCN_SUCCESS);
#else
    // For simulation, we'll just return true
    // In production, the scan result would arrive via callback
    // or polling in the scan thread
    return true;
#endif
}

bool ScannerHAL::GetLastScan(TCHAR* barcode, DWORD maxLen)
{
    if (!m_lastBarcode || !barcode) {
        return false;
    }
    
    lstrcpyn(barcode, m_lastBarcode, maxLen);
    return true;
}

const TCHAR* ScannerHAL::GetScannerStatus() const
{
    if (!m_initialized) {
        return TEXT("Not initialized");
    }
    if (!m_enabled) {
        return TEXT("Disabled");
    }
    return TEXT("Ready");
}

bool ScannerHAL::SetScanMode(int mode)
{
    if (!m_initialized || !m_scannerHandle) {
        return false;
    }

    // Set scan mode via EMDK
    // mode: 0 = continuous, 1 = single scan
#ifdef HBX_USE_EMDK
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_scannerHandle, &params) != E_SCN_SUCCESS) {
        return false;
    }
    // 0 = continuous -> level trigger; 1 = single -> one-shot trigger.
    params.dwTriggerMode = (mode == 0) ? TRIG_MODE_LEVEL : TRIG_MODE_ONESHOT;
    return (SCAN_SetParameters((SCAN_HANDLE)m_scannerHandle, &params) == E_SCN_SUCCESS);
#else
    (void)mode;
    return true;
#endif
}

bool ScannerHAL::SetBeepEnabled(bool enabled)
{
    if (!m_initialized || !m_scannerHandle) {
        return false;
    }

    // Configure beep via EMDK
#ifdef HBX_USE_EMDK
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_scannerHandle, &params) != E_SCN_SUCCESS) {
        return false;
    }
    params.dwDecodeBeepEnable    = enabled ? 1 : 0;
    params.dwDecodeBeepTime      = 200;   // Duration in milliseconds
    params.dwDecodeBeepFrequency = 2500;  // Frequency in Hz
    return (SCAN_SetParameters((SCAN_HANDLE)m_scannerHandle, &params) == E_SCN_SUCCESS);
#else
    (void)enabled;
    return true;
#endif
}

bool ScannerHAL::SetVibrateEnabled(bool enabled)
{
    if (!m_initialized || !m_scannerHandle) {
        return false;
    }

    // Configure vibrate via EMDK
#ifdef HBX_USE_EMDK
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_scannerHandle, &params) != E_SCN_SUCCESS) {
        return false;
    }
    params.dwDecodeVibrateEnable = enabled ? 1 : 0;
    params.dwDecodeVibrateTime   = 200;  // Duration in milliseconds
    return (SCAN_SetParameters((SCAN_HANDLE)m_scannerHandle, &params) == E_SCN_SUCCESS);
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
    m_callback = callback;
    m_callbackUserData = userData;
}

bool ScannerHAL::OpenScanner()
{
#ifdef HBX_USE_EMDK
    // Open the default scanner. "SCN1:" is the primary scanner port exposed by
    // the Symbol/Zebra driver on the MC75; SCAN_Open returns a HANDLE.
    HANDLE hScanner = NULL;
    DWORD result = SCAN_Open(TEXT("SCN1:"), &hScanner);
    if (result != E_SCN_SUCCESS || hScanner == NULL) {
        return false;
    }
    m_scannerHandle = hScanner;

    // Configure scanner defaults: level (continuous) trigger with the decode
    // beep on, vibrate off. Best-effort - a config failure is not fatal.
    SCAN_PARAMS params;
    if (SCAN_GetParameters((SCAN_HANDLE)m_scannerHandle, &params) == E_SCN_SUCCESS) {
        params.dwTriggerMode         = TRIG_MODE_LEVEL;
        params.dwDecodeBeepEnable    = 1;
        params.dwDecodeBeepTime      = 200;
        params.dwDecodeBeepFrequency = 2500;
        params.dwDecodeVibrateEnable = 0;
        params.dwDecodeVibrateTime   = 200;
        SCAN_SetParameters((SCAN_HANDLE)m_scannerHandle, &params);
    }

    // Allocate the reusable decode buffer used by the scan thread. TRUE selects
    // a text (as opposed to raw/binary) buffer format.
    m_scanBuffer = SCAN_AllocateBuffer(TRUE, SCAN_MAX_LABEL_LEN);
    if (m_scanBuffer == NULL) {
        SCAN_Close((SCAN_HANDLE)m_scannerHandle);
        m_scannerHandle = NULL;
        return false;
    }

    return true;
#else
    // Open scanner device via EMDK
    // For MC75 with Zebra EMDK, typical implementation:

    // In a real implementation with EMDK library linked:
    // SCAN_HANDLE scanHandle;
    // DWORD result = SCAN_Open(&scanHandle);
    // if (result != E_SCN_SUCCESS) return false;
    // m_scannerHandle = (HANDLE)scanHandle;

    // For now, create a simulated handle
    // In production, this would be the actual EMDK scanner handle
    m_scannerHandle = (HANDLE)0x12345678; // Simulated handle

    // Configure scanner defaults
    // Real EMDK calls would be:
    // SCAN_SetParameters(scanHandle, &params);
    // SCAN_SetCallBack(scanHandle, ScanCallback, this);

    return (m_scannerHandle != NULL);
#endif
}

void ScannerHAL::CloseScanner()
{
    // Stop scan thread first
    if (m_scanThread) {
        m_scanThreadRunning = false;
        WaitForSingleObject(m_scanThread, 5000); // Wait up to 5 seconds
        CloseHandle(m_scanThread);
        m_scanThread = NULL;
    }

    // Close scanner device via EMDK
    if (m_scannerHandle) {
#ifdef HBX_USE_EMDK
        if (m_scanBuffer) {
            SCAN_DeallocateBuffer(m_scanBuffer);
            m_scanBuffer = NULL;
        }
        SCAN_Close((SCAN_HANDLE)m_scannerHandle);
#endif
        m_scannerHandle = NULL;
    }
}

DWORD WINAPI ScannerHAL::ScanThread(LPVOID param)
{
    ScannerHAL* pThis = (ScannerHAL*)param;
    if (!pThis) {
        return 1;
    }

    // Scan monitoring thread
    // This thread would typically:
    // 1. Wait for scan events from EMDK
    // 2. Read scan data
    // 3. Invoke callback with barcode data

    while (pThis->m_scanThreadRunning) {
#ifdef HBX_USE_EMDK
        // Only issue reads while the scanner is enabled; otherwise idle so we
        // don't spin on immediate error returns.
        if (!pThis->m_enabled || pThis->m_scannerHandle == NULL ||
            pThis->m_scanBuffer == NULL) {
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
        DWORD result = SCAN_ReadLabelWait((SCAN_HANDLE)pThis->m_scannerHandle,
                                          pThis->m_scanBuffer,
                                          1000); // 1 second timeout

        if (result == E_SCN_SUCCESS) {
            DWORD dataLen = SCNBUF_GETLEN(pThis->m_scanBuffer);
            const char* rawData = (const char*)SCNBUF_GETDATA(pThis->m_scanBuffer);

            if (dataLen > 0 && rawData != NULL) {
                // Convert the decoded bytes to TCHAR. On-device TCHAR is WCHAR,
                // so MultiByteToWideChar performs the real narrow->wide
                // conversion; on the host TCHAR is char and it is a bounded
                // copy. Clamp to the local buffer and NUL-terminate.
                const int kMaxChars = 255;
                TCHAR barcode[256];
                int copyLen = (dataLen < (DWORD)kMaxChars) ? (int)dataLen : kMaxChars;
                int cch = MultiByteToWideChar(CP_ACP, 0, rawData, copyLen,
                                              barcode, kMaxChars);
                if (cch < 0) cch = 0;
                if (cch > kMaxChars) cch = kMaxChars;
                barcode[cch] = (TCHAR)0;

                // Store last barcode (free the previous one first).
                if (pThis->m_lastBarcode) {
                    delete[] pThis->m_lastBarcode;
                    pThis->m_lastBarcode = NULL;
                }
                int len = lstrlen(barcode) + 1;
                pThis->m_lastBarcode = new TCHAR[len];
                lstrcpy(pThis->m_lastBarcode, barcode);

                // Notify the listener.
                if (pThis->m_callback) {
                    pThis->m_callback(barcode, pThis->m_callbackUserData);
                }
            }
        }
        // On timeout / other codes just loop again and re-check the run flag.
#else
        // For simulation, just sleep
        Sleep(100);
#endif
    }

    return 0;
}

} // namespace HBX
