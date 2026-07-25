#ifndef VIEWS_SCANVIEW_HPP
#define VIEWS_SCANVIEW_HPP

#include <windows.h>
#include "ViewHelpers.hpp"
#include "../ScannerHAL.hpp"

namespace HBX {
namespace Views {

/**
 * Barcode scanning interface view
 * Handles scanner UI and user interactions
 */
class ScanView {
public:
    ScanView();
    ~ScanView();

    // Window management
    bool Create(HWND parentWnd, HINSTANCE hInstance);
    void Show(bool visible);
    void Destroy();
    HWND GetHandle() const;

    // UI updates
    void SetStatus(const TCHAR* status);
    void DisplayBarcode(const TCHAR* barcode);
    void ShowError(const TCHAR* error);
    void ClearDisplay();

    // Scanner integration
    void SetScanner(ScannerHAL* scanner);
    void EnableScanButton(bool enabled);

    // Callback for scan events
    typedef void (*ScanEventCallback)(const TCHAR* barcode, void* userData);
    void SetScanCallback(ScanEventCallback callback, void* userData);

private:
    enum {
        // Posted by ScanThunk from the EMDK scanner thread; lParam owns a heap
        // TCHAR copy of the decoded label that WindowProc must delete[].
        MSG_SCAN_DECODED = WM_APP + 1,

        // Resets the status label when a triggered scan decodes nothing (the
        // beam times out silently, so no callback ever arrives).
        TIMER_SCAN_TIMEOUT = 1,
        SCAN_TIMEOUT_MS = 5000,

        // The MC75's decoder caps a label at 255 characters.
        MAX_BARCODE_CHARS = 256
    };

    HWND m_hwnd;
    HWND m_statusLabel;
    HWND m_barcodeDisplay;
    HWND m_scanButton;
    HWND m_lookupButton;
    HINSTANCE m_hInstance;
    ScannerHAL* m_scanner;
    ScanEventCallback m_callback;
    void* m_callbackUserData;

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Trampoline that forwards ScannerHAL scan-callbacks to the UI thread.
    static void ScanThunk(const TCHAR* barcode, void* userData);

    // Event handlers
    void OnScanButtonClick();
    void OnLookupButtonClick();
    void OnScanReceived(const TCHAR* barcode);
    void OnScanTimeout();

    // UI layout
    void LayoutControls();
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_SCANVIEW_HPP
