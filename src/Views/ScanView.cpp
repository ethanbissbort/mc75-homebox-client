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
    
    // TODO: Create window and controls
    
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
    // TODO: Implement window procedure
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ScanView::OnScanButtonClick()
{
    // TODO: Handle scan button click
}

void ScanView::OnScanReceived(const TCHAR* barcode)
{
    if (m_callback) {
        m_callback(barcode, m_callbackUserData);
    }
}

void ScanView::LayoutControls()
{
    // TODO: Layout controls
}

} // namespace Views
} // namespace HBX
