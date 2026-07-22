#ifndef SCANNERHAL_HPP
#define SCANNERHAL_HPP

#include <windows.h>

#ifdef HBX_USE_EMDK
// Real Symbol / Zebra EMDK "Scanner C API" header (device build).
// On the host test build this resolves to tests/host/shim/ScanCAPI.h.
#include <ScanCAPI.h>
#endif

namespace HBX {

/**
 * Hardware Abstraction Layer for Motorola MC75 scanner
 * Interfaces with Zebra EMDK for barcode scanning
 */
class ScannerHAL {
public:
    ScannerHAL();
    ~ScannerHAL();

    // Scanner initialization
    bool Initialize();
    bool Shutdown();
    bool IsInitialized() const;

    // Scanning operations
    bool EnableScanner();
    bool DisableScanner();
    bool TriggerScan();

    // Data retrieval
    bool GetLastScan(TCHAR* barcode, DWORD maxLen);
    const TCHAR* GetScannerStatus() const;

    // Configuration
    bool SetScanMode(int mode);  // 0=continuous, 1=single
    bool SetBeepEnabled(bool enabled);
    bool SetVibrateEnabled(bool enabled);

    // Callback for scan events
    typedef void (*ScanCallback)(const TCHAR* barcode, void* userData);
    void SetScanCallback(ScanCallback callback, void* userData);

private:
    HANDLE m_scannerHandle;
    bool m_initialized;
    bool m_enabled;
    TCHAR* m_lastBarcode;
    ScanCallback m_callback;
    void* m_callbackUserData;

    // EMDK interface methods
    bool OpenScanner();
    void CloseScanner();
    static DWORD WINAPI ScanThread(LPVOID param);
    HANDLE m_scanThread;
    bool m_scanThreadRunning;

#ifdef HBX_USE_EMDK
    // Reusable decode buffer, allocated by SCAN_AllocateBuffer in OpenScanner
    // and released by SCAN_DeallocateBuffer in CloseScanner (device build only).
    LPSCAN_BUFFER m_scanBuffer;
#endif
};

} // namespace HBX

#endif // SCANNERHAL_HPP
