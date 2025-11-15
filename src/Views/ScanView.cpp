#include "../../include/Views/ScanView.hpp"
#include "../../include/Views/ViewHelpers.hpp"

namespace HBX {
namespace Views {

ScanView::ScanView()
    : m_hwnd(NULL)
    , m_statusLabel(NULL)
    , m_barcodeDisplay(NULL)
    , m_scanButton(NULL)
    , m_hInstance(NULL)
    , m_scanner(NULL)
    , m_callback(NULL)
    , m_callbackUserData(NULL)
{
}

ScanView::~ScanView()
{
    Destroy();
}

bool ScanView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Create main window
    m_hwnd = CreateWindow(
        TEXT("STATIC"),
        TEXT("Scan View"),
        WS_CHILD | WS_VISIBLE,
        0, 0, 240, 320,
        parentWnd,
        NULL,
        hInstance,
        NULL
    );

    if (!m_hwnd) {
        return false;
    }

    // Create status label
    m_statusLabel = CreateWindow(
        TEXT("STATIC"),
        TEXT("Ready to scan"),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 10, 220, 30,
        m_hwnd,
        (HMENU)1001,
        hInstance,
        NULL
    );

    // Create barcode display
    m_barcodeDisplay = CreateWindow(
        TEXT("EDIT"),
        TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER,
        10, 50, 220, 30,
        m_hwnd,
        (HMENU)1002,
        hInstance,
        NULL
    );

    // Create scan button
    m_scanButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Scan"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        60, 100, 120, 40,
        m_hwnd,
        (HMENU)1003,
        hInstance,
        NULL
    );

    LayoutControls();

    return true;
}

void ScanView::Show(bool visible)
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
    }
}

void ScanView::Destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

HWND ScanView::GetHandle() const
{
    return m_hwnd;
}

void ScanView::SetStatus(const TCHAR* status)
{
    if (m_statusLabel) {
        SetWindowText(m_statusLabel, status);
    }
}

void ScanView::DisplayBarcode(const TCHAR* barcode)
{
    if (m_barcodeDisplay) {
        SetWindowText(m_barcodeDisplay, barcode);
    }
}

void ScanView::ShowError(const TCHAR* error)
{
    MessageBox(m_hwnd, error, TEXT("Error"), MB_OK | MB_ICONERROR);
}

void ScanView::ClearDisplay()
{
    if (m_barcodeDisplay) {
        SetWindowText(m_barcodeDisplay, TEXT(""));
    }
}

void ScanView::SetScanner(ScannerHAL* scanner)
{
    m_scanner = scanner;
}

void ScanView::EnableScanButton(bool enabled)
{
    if (m_scanButton) {
        EnableWindow(m_scanButton, enabled ? TRUE : FALSE);
    }
}

void ScanView::SetScanCallback(ScanEventCallback callback, void* userData)
{
    m_callback = callback;
    m_callbackUserData = userData;
}

LRESULT CALLBACK ScanView::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ScanView* pThis = NULL;

    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (ScanView*)pCreate->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)pThis);
    } else {
        pThis = (ScanView*)GetWindowLong(hwnd, GWL_USERDATA);
    }

    switch (uMsg) {
        case WM_COMMAND:
            if (pThis) {
                WORD ctrlId = LOWORD(wParam);
                WORD notifyCode = HIWORD(wParam);

                if (ctrlId == 1003 && notifyCode == BN_CLICKED) {
                    pThis->OnScanButtonClick();
                    return 0;
                }
            }
            break;

        case WM_SIZE:
            if (pThis) {
                pThis->LayoutControls();
            }
            return 0;

        case WM_DESTROY:
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ScanView::OnScanButtonClick()
{
    if (m_scanner) {
        // Trigger software scan
        SetStatus(TEXT("Scanning..."));
        m_scanner->TriggerScan();

        // In a real implementation, the scan would be asynchronous
        // and we'd receive the result via the scanner callback
        // For now, just simulate a scan completion
        SetStatus(TEXT("Ready to scan"));
    } else {
        ShowError(TEXT("Scanner not initialized"));
    }
}

void ScanView::OnScanReceived(const TCHAR* barcode)
{
    if (m_callback) {
        m_callback(barcode, m_callbackUserData);
    }
}

void ScanView::LayoutControls()
{
    if (!m_hwnd) {
        return;
    }

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);

    int width = clientRect.right - clientRect.left;
    int margin = 10;

    // Layout status label
    if (m_statusLabel) {
        MoveWindow(m_statusLabel, margin, margin, width - (2 * margin), 30, TRUE);
    }

    // Layout barcode display
    if (m_barcodeDisplay) {
        MoveWindow(m_barcodeDisplay, margin, 50, width - (2 * margin), 40, TRUE);
    }

    // Layout scan button
    if (m_scanButton) {
        int buttonWidth = 120;
        MoveWindow(m_scanButton, (width - buttonWidth) / 2, 110, buttonWidth, 50, TRUE);
    }
}

} // namespace Views
} // namespace HBX
