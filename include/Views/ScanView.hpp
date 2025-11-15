#ifndef VIEWS_SCANVIEW_HPP
#define VIEWS_SCANVIEW_HPP

#include <windows.h>
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
    HWND m_hwnd;
    HWND m_statusLabel;
    HWND m_barcodeDisplay;
    HWND m_scanButton;
    HINSTANCE m_hInstance;
    ScannerHAL* m_scanner;
    ScanEventCallback m_callback;
    void* m_callbackUserData;

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Event handlers
    void OnScanButtonClick();
    void OnScanReceived(const TCHAR* barcode);

    // UI layout
    void LayoutControls();
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_SCANVIEW_HPP
