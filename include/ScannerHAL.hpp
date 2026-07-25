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
 *
 * THREADING
 * ---------
 * On the device build a monitor thread polls the EMDK for decoded labels and
 * delivers each one through the registered ScanCallback. The callback therefore
 * runs on the SCAN THREAD, not the UI thread: handlers must copy what they need
 * and return promptly. Anything slow (HTTP, a modal message box) blocks further
 * decodes and delays shutdown.
 *
 * Everything the monitor thread touches lives in a reference-counted block that
 * outlives the ScannerHAL if it has to: the object can be deleted while a decode
 * is still in flight, and the thread must not be left pointing at freed memory.
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

    /**
     * Delivers `barcode` exactly as a hardware decode would: it becomes the
     * last scan and is passed to the registered callback on the calling thread.
     *
     * This is the only source of scans in the simulation (non-EMDK) build, and
     * it is what lets the scan -> queue -> sync pipeline be driven off-device.
     */
    bool InjectScan(const TCHAR* barcode);

private:
    enum { MAX_BARCODE_CHARS = 256 };

    /**
     * State shared with the monitor thread, reference counted because the two
     * owners do not have nested lifetimes: Controller can delete the ScannerHAL
     * while a slow decode still has the thread inside the loop. The last owner
     * to release it also releases the scanner hardware, so the EMDK buffer and
     * handle stay valid for as long as the thread can still touch them.
     */
    struct SharedState {
        volatile LONG refCount;
        volatile LONG running;    // monitor loop keeps going while non-zero
        volatile LONG enabled;
        CRITICAL_SECTION lock;    // guards callback + userData + lastBarcode
        ScanCallback callback;
        void* callbackUserData;
        TCHAR lastBarcode[MAX_BARCODE_CHARS];
        bool hasLastBarcode;
        HANDLE scannerHandle;
#ifdef HBX_USE_EMDK
        // Reusable decode buffer, allocated by SCAN_AllocateBuffer in
        // OpenScanner and released with the shared state (device build only).
        LPSCAN_BUFFER scanBuffer;
#else
        LONG simSequence;         // numbers the synthetic simulation barcodes
#endif
    };

    static SharedState* CreateState();
    static void AddRefState(SharedState* state);
    static void ReleaseState(SharedState* state);
    static void ReleaseHardware(SharedState* state);
    static void DeliverScan(SharedState* state, const TCHAR* barcode);

    // EMDK interface methods
    bool OpenScanner();
    void CloseScanner();
    static DWORD WINAPI ScanThread(LPVOID param);

    SharedState* m_state;
    bool m_initialized;
    HANDLE m_scanThread;

    // Not copyable: owns the shared state reference and a thread handle.
    ScannerHAL(const ScannerHAL&);
    ScannerHAL& operator=(const ScannerHAL&);
};

} // namespace HBX

#endif // SCANNERHAL_HPP
