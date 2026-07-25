#include "../../include/Views/ScanView.hpp"
#include "../../include/Views/ViewHelpers.hpp"
#include "../../include/StrUtil.hpp"

namespace HBX {
namespace Views {

namespace {

/**
 * Strips leading and trailing blanks from a manually typed barcode in place.
 * The MC75 keypad makes a stray space easy to produce and the server treats
 * " 12345" as a different (missing) item.
 */
void TrimInPlace(TCHAR* text)
{
    if (!text) {
        return;
    }

    int start = 0;
    while (text[start] == (TCHAR)' ' || text[start] == (TCHAR)'\t') {
        start++;
    }

    int end = start;
    int lastNonBlank = start - 1;
    while (text[end] != 0) {
        if (text[end] != (TCHAR)' ' && text[end] != (TCHAR)'\t' &&
            text[end] != (TCHAR)'\r' && text[end] != (TCHAR)'\n') {
            lastNonBlank = end;
        }
        end++;
    }

    int len = lastNonBlank - start + 1;
    if (len < 0) {
        len = 0;
    }
    for (int i = 0; i < len; i++) {
        text[i] = text[start + i];
    }
    text[len] = 0;
}

} // namespace

ScanView::ScanView()
    : m_hwnd(NULL)
    , m_statusLabel(NULL)
    , m_barcodeDisplay(NULL)
    , m_scanButton(NULL)
    , m_lookupButton(NULL)
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

    // Register a window class that installs ScanView::WindowProc. The built-in
    // STATIC class would never call our WindowProc, so button clicks / resize
    // messages would be dropped and the view would be inert.
    static const TCHAR* kClassName = TEXT("HBXScanView");
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = ScanView::WindowProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClass(&wc); // harmless if already registered by a prior Create()

    // Create main window using our class; pass 'this' as the creation param so
    // WM_CREATE can stash the instance pointer (see WindowProc).
    m_hwnd = CreateWindow(
        kClassName,
        TEXT("Scan View"),
        WS_CHILD | WS_VISIBLE,
        0, 0, 240, 320,
        parentWnd,
        NULL,
        hInstance,
        (LPVOID)this
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

    // Barcode field. It is deliberately writable: a cracked scan window or a
    // damaged label is routine in a warehouse, and typing the number on the
    // keypad is then the only way to keep working.
    m_barcodeDisplay = CreateWindow(
        TEXT("EDIT"),
        TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
        10, 50, 220, 30,
        m_hwnd,
        (HMENU)1002,
        hInstance,
        NULL
    );

    if (m_barcodeDisplay) {
        SendMessage(m_barcodeDisplay, EM_LIMITTEXT, (WPARAM)(MAX_BARCODE_CHARS - 1), 0);
    }

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

    // Submits whatever is in the barcode field, so the typed fallback reaches
    // exactly the same handler as a hardware decode.
    m_lookupButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Lookup"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        60, 150, 120, 40,
        m_hwnd,
        (HMENU)1004,
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
        KillTimer(m_hwnd, (UINT_PTR)TIMER_SCAN_TIMEOUT);
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

    // Route hardware scan events into this view so a real barcode read reaches
    // OnScanReceived (and from there the view's own scan callback).
    if (m_scanner) {
        m_scanner->SetScanCallback(&ScanView::ScanThunk, this);
    }
}

// static
void ScanView::ScanThunk(const TCHAR* barcode, void* userData)
{
    ScanView* self = (ScanView*)userData;
    if (!self || !self->m_hwnd || !barcode) {
        return;
    }

    // This runs on the EMDK scanner thread. Touching the controls here (or
    // letting the controller's blocking HTTP lookup run from here) would drive
    // windows owned by the UI thread from a worker and share one HttpClient
    // socket between two threads, so hand the label over and return at once.
    TCHAR* copy = Str::Dup(barcode);
    if (!copy) {
        return;
    }

    if (!PostMessage(self->m_hwnd, (UINT)MSG_SCAN_DECODED, 0, (LPARAM)copy)) {
        delete[] copy;
    }
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
        case MSG_SCAN_DECODED: {
            // Ownership of the posted copy transfers here; free it even when
            // the view has already been detached, or the string leaks.
            TCHAR* barcode = (TCHAR*)lParam;
            if (pThis) {
                pThis->OnScanReceived(barcode);
            }
            delete[] barcode;
            return 0;
        }

        case WM_COMMAND:
            if (pThis) {
                WORD ctrlId = LOWORD(wParam);
                WORD notifyCode = HIWORD(wParam);

                if (ctrlId == 1003 && notifyCode == BN_CLICKED) {
                    pThis->OnScanButtonClick();
                    return 0;
                }
                if (ctrlId == 1004 && notifyCode == BN_CLICKED) {
                    pThis->OnLookupButtonClick();
                    return 0;
                }
            }
            break;

        case WM_TIMER:
            if (pThis && wParam == (WPARAM)TIMER_SCAN_TIMEOUT) {
                pThis->OnScanTimeout();
                return 0;
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
    if (!m_scanner) {
        ShowError(TEXT("Scanner not initialized"));
        return;
    }

    // Arm the scanner (soft trigger). The decoded barcode arrives
    // asynchronously via the scanner callback wired in SetScanner
    // (ScanThunk -> posted message -> OnScanReceived).
    SetStatus(TEXT("Scanning..."));
    if (!m_scanner->TriggerScan()) {
        SetStatus(TEXT("Ready to scan"));
        ShowError(TEXT("Failed to trigger scan. Type the barcode and press Lookup."));
        return;
    }

    // A trigger that hits nothing (beam not aimed at a label, decoder timeout)
    // produces no callback at all, so nothing else would ever clear
    // "Scanning..." and the view would look permanently busy.
    if (m_hwnd) {
        SetTimer(m_hwnd, (UINT_PTR)TIMER_SCAN_TIMEOUT, (UINT)SCAN_TIMEOUT_MS, NULL);
    }
}

void ScanView::OnLookupButtonClick()
{
    TCHAR barcode[MAX_BARCODE_CHARS];
    barcode[0] = 0;

    if (m_barcodeDisplay) {
        GetWindowText(m_barcodeDisplay, barcode, MAX_BARCODE_CHARS);
    }
    TrimInPlace(barcode);

    if (lstrlen(barcode) == 0) {
        ShowError(TEXT("Enter a barcode first."));
        return;
    }

    OnScanReceived(barcode);
}

void ScanView::OnScanReceived(const TCHAR* barcode)
{
    if (!barcode || lstrlen(barcode) == 0) {
        return;
    }

    if (m_hwnd) {
        KillTimer(m_hwnd, (UINT_PTR)TIMER_SCAN_TIMEOUT);
    }

    // Show the scanned value and notify whoever registered for scan events.
    // Always on the UI thread: either a button click or the posted message.
    DisplayBarcode(barcode);
    SetStatus(TEXT("Scan received"));

    if (m_callback) {
        m_callback(barcode, m_callbackUserData);
    }
}

void ScanView::OnScanTimeout()
{
    if (m_hwnd) {
        KillTimer(m_hwnd, (UINT_PTR)TIMER_SCAN_TIMEOUT);
    }
    SetStatus(TEXT("No barcode read - try again or type it"));
}

void ScanView::LayoutControls()
{
    if (!m_hwnd) {
        return;
    }

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);

    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    const int margin = 10;
    const int buttonHeight = 40;
    int contentWidth = width - (2 * margin);
    if (contentWidth < 60) {
        contentWidth = 60;
    }

    if (m_statusLabel) {
        MoveWindow(m_statusLabel, margin, margin, contentWidth, 30, TRUE);
    }

    if (m_barcodeDisplay) {
        MoveWindow(m_barcodeDisplay, margin, 50, contentWidth, 30, TRUE);
    }

    // Both buttons sit just under the barcode field rather than being anchored
    // to the bottom: the SIP can cover the lower half of the MC75 screen while
    // the user is typing a barcode manually.
    int buttonWidth = (contentWidth - margin) / 2;
    if (buttonWidth < 50) {
        buttonWidth = 50;
    }
    int buttonY = 95;

    if (m_scanButton) {
        MoveWindow(m_scanButton, margin, buttonY, buttonWidth, buttonHeight, TRUE);
    }
    if (m_lookupButton) {
        MoveWindow(m_lookupButton, margin + buttonWidth + margin, buttonY,
                   buttonWidth, buttonHeight, TRUE);
    }
}

} // namespace Views
} // namespace HBX
