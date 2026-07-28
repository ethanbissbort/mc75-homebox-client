#include "../../include/Views/DeviceView.hpp"
#include "../../include/Views/ViewHelpers.hpp"
#include "../../include/StrUtil.hpp"
#include <commctrl.h>

// The host shim carries only the list-view surface the original views used; the
// real <commctrl.h> and <windows.h> define these, so each guard is a no-op on
// the device build.
#ifndef LVM_SETITEMTEXT
#define LVM_SETITEMTEXT (LVM_FIRST + 116)
#endif
#ifndef ListView_SetItemText
#define ListView_SetItemText(w, i, iSub, txt) \
    do { LVITEM _lvi = {0}; _lvi.iSubItem = (iSub); _lvi.pszText = (txt); \
         SendMessage((w), LVM_SETITEMTEXT, (WPARAM)(int)(i), (LPARAM)&_lvi); } while (0)
#endif
#ifndef WM_SETFONT
#define WM_SETFONT 0x0030
#endif

namespace HBX {
namespace Views {

namespace {

// Header geometry. These match ItemView / QueueView so the three screens do not
// visibly jump when the operator moves between them.
const int kMargin       = 8;
const int kTitleHeight  = 22;
const int kLineHeight   = 18;
const int kButtonHeight = 30;
const int kGap          = 4;

} // namespace

DeviceView::DeviceView()
    : m_hwnd(NULL)
    , m_titleLabel(NULL)
    , m_subtitleLabel(NULL)
    , m_statusLabel(NULL)
    , m_listView(NULL)
    , m_moveButton(NULL)
    , m_statusButton(NULL)
    , m_backButton(NULL)
    , m_hInstance(NULL)
    , m_titleFont(NULL)
    , m_hasAsset(false)
    , m_canMove(false)
    , m_canChangeStatus(false)
    , m_actionCallback(NULL)
    , m_callbackUserData(NULL)
{
}

DeviceView::~DeviceView()
{
    Destroy();

    // The controls that referenced the font are gone by now, so it is safe to
    // release it.
    if (m_titleFont) {
        ViewHelpers::DeleteFont(m_titleFont);
        m_titleFont = NULL;
    }
}

bool DeviceView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Register a window class that installs DeviceView::WindowProc so button
    // clicks and resize messages reach this instance (the built-in STATIC class
    // would drop them).
    static const TCHAR* kClassName = TEXT("HBXDeviceView");
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = DeviceView::WindowProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClass(&wc); // harmless if already registered by a prior Create()

    m_hwnd = CreateWindow(
        kClassName,
        TEXT("Device View"),
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

    m_titleLabel = CreateWindow(TEXT("STATIC"), TEXT(""),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        kMargin, kMargin, 224, kTitleHeight, m_hwnd, NULL, hInstance, NULL);

    m_subtitleLabel = CreateWindow(TEXT("STATIC"), TEXT(""),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        kMargin, 34, 224, kLineHeight, m_hwnd, NULL, hInstance, NULL);

    m_statusLabel = CreateWindow(TEXT("STATIC"), TEXT(""),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        kMargin, 54, 224, kLineHeight, m_hwnd, NULL, hInstance, NULL);

    // Bold only on the title. A failed CreateFontIndirect leaves the control on
    // the default font, which is a cosmetic loss and not worth failing Create.
    m_titleFont = ViewHelpers::CreateBoldFont();
    if (m_titleFont && m_titleLabel) {
        SendMessage(m_titleLabel, WM_SETFONT, (WPARAM)m_titleFont, (LPARAM)TRUE);
    }

    m_listView = CreateWindow(
        WC_LISTVIEW,
        TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
        kMargin, 76, 224, 120,
        m_hwnd,
        (HMENU)ID_LISTVIEW,
        hInstance,
        NULL
    );

    m_moveButton = CreateWindow(TEXT("BUTTON"), TEXT("Move"),
        WS_CHILD | BS_PUSHBUTTON,
        kMargin, 220, 72, kButtonHeight, m_hwnd, (HMENU)ID_MOVE_BUTTON, hInstance, NULL);

    m_statusButton = CreateWindow(TEXT("BUTTON"), TEXT("Status"),
        WS_CHILD | BS_PUSHBUTTON,
        kMargin, 220, 72, kButtonHeight, m_hwnd, (HMENU)ID_STATUS_BUTTON, hInstance, NULL);

    // Back is always available: it is the only way off this screen that does
    // not depend on the soft-key menu bar, which can fail to create.
    m_backButton = CreateWindow(TEXT("BUTTON"), TEXT("Back"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        kMargin, 220, 72, kButtonHeight, m_hwnd, (HMENU)ID_BACK_BUTTON, hInstance, NULL);

    InitializeListView();
    LayoutControls();

    return true;
}

void DeviceView::Show(bool visible)
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
    }
}

void DeviceView::Destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

HWND DeviceView::GetHandle() const
{
    return m_hwnd;
}

void DeviceView::DisplayAsset(const Models::AssetSummary* asset)
{
    if (!asset) {
        Clear();
        return;
    }

    m_asset = *asset;
    m_hasAsset = true;

    RefreshHeader();
    PopulateList();
}

const Models::AssetSummary* DeviceView::GetAsset() const
{
    return m_hasAsset ? &m_asset : NULL;
}

void DeviceView::Clear()
{
    m_asset.Clear();
    m_hasAsset = false;

    if (m_titleLabel)    SetWindowText(m_titleLabel, TEXT(""));
    if (m_subtitleLabel) SetWindowText(m_subtitleLabel, TEXT(""));
    if (m_statusLabel)   SetWindowText(m_statusLabel, TEXT(""));
    if (m_listView)      ListView_DeleteAllItems(m_listView);
}

void DeviceView::SetActionsAvailable(bool canMove, bool canChangeStatus)
{
    m_canMove = canMove;
    m_canChangeStatus = canChangeStatus;

    if (m_moveButton) {
        ShowWindow(m_moveButton, canMove ? SW_SHOW : SW_HIDE);
    }
    if (m_statusButton) {
        ShowWindow(m_statusButton, canChangeStatus ? SW_SHOW : SW_HIDE);
    }

    // The remaining buttons share the width the hidden ones gave up.
    LayoutControls();
}

void DeviceView::SetNotice(const TCHAR* notice)
{
    if (!m_statusLabel) {
        return;
    }

    if (notice && notice[0] != 0) {
        SetWindowText(m_statusLabel, notice);
        return;
    }

    RefreshHeader();
}

void DeviceView::SetActionCallback(ActionCallback callback, void* userData)
{
    m_actionCallback = callback;
    m_callbackUserData = userData;
}

void DeviceView::RaiseAction(int action)
{
    if (m_actionCallback) {
        m_actionCallback(action, m_callbackUserData);
    }
}

void DeviceView::RefreshHeader()
{
    if (!m_hasAsset) {
        return;
    }

    if (m_titleLabel) {
        // A device with no name is normal in NetBox - it is not a required
        // field - so fall back to whatever was scanned rather than showing an
        // empty header.
        const TCHAR* title = m_asset.GetTitle();
        if (!title || title[0] == 0) {
            title = m_asset.GetCode();
        }
        SetWindowText(m_titleLabel, title ? title : TEXT(""));
    }

    if (m_subtitleLabel) {
        SetWindowText(m_subtitleLabel, m_asset.GetSubtitle());
    }

    if (m_statusLabel) {
        // Status plus the system it came from. Naming the backend on the detail
        // screen as well as in the title bar is deliberate: the title bar is
        // the least-looked-at strip on the screen while a scanner is being
        // aimed, and acting on the wrong system is the failure this guards.
        TCHAR line[HEADER_CHARS];
        line[0] = 0;

        const TCHAR* status = m_asset.GetStatus();
        if (status && status[0] != 0) {
            Str::Append(line, HEADER_CHARS, status);
        }

        const TCHAR* source = m_asset.GetSource();
        if (source && source[0] != 0) {
            if (line[0] != 0) {
                Str::Append(line, HEADER_CHARS, TEXT(" - "));
            }
            Str::Append(line, HEADER_CHARS, source);
        }

        SetWindowText(m_statusLabel, line);
    }
}

void DeviceView::PopulateList()
{
    if (!m_listView) {
        return;
    }

    ListView_DeleteAllItems(m_listView);

    const int count = m_asset.GetFieldCount();
    for (int i = 0; i < count; i++) {
        LVITEM item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = (TCHAR*)m_asset.GetLabel(i);

        int inserted = ListView_InsertItem(m_listView, &item);
        if (inserted >= 0) {
            ListView_SetItemText(m_listView, inserted, 1, (TCHAR*)m_asset.GetValue(i));
        }
    }
}

LRESULT CALLBACK DeviceView::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    DeviceView* pThis = NULL;

    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (DeviceView*)pCreate->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)pThis);
    } else {
        pThis = (DeviceView*)GetWindowLong(hwnd, GWL_USERDATA);
    }

    switch (uMsg) {
        case WM_COMMAND:
            if (pThis && HIWORD(wParam) == BN_CLICKED) {
                switch (LOWORD(wParam)) {
                    case ID_MOVE_BUTTON:
                        pThis->RaiseAction(ACTION_MOVE);
                        return 0;
                    case ID_STATUS_BUTTON:
                        pThis->RaiseAction(ACTION_STATUS);
                        return 0;
                    case ID_BACK_BUTTON:
                        pThis->RaiseAction(ACTION_BACK);
                        return 0;
                    default:
                        break;
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

void DeviceView::LayoutControls()
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

    int contentWidth = width - (2 * kMargin);
    if (contentWidth < 60) {
        contentWidth = 60;
    }

    int y = kMargin;
    if (m_titleLabel) {
        MoveWindow(m_titleLabel, kMargin, y, contentWidth, kTitleHeight, TRUE);
    }
    y += kTitleHeight + 2;

    if (m_subtitleLabel) {
        MoveWindow(m_subtitleLabel, kMargin, y, contentWidth, kLineHeight, TRUE);
    }
    y += kLineHeight + 2;

    if (m_statusLabel) {
        MoveWindow(m_statusLabel, kMargin, y, contentWidth, kLineHeight, TRUE);
    }
    y += kLineHeight + kGap;

    // One bottom-anchored button row. Fixed coordinates taken from a nominal
    // 320 px form fall off the bottom of the MC75 once the navigation bar and
    // the soft-key menu bar have taken their strips.
    int buttonY = height - kButtonHeight - kMargin;

    int listHeight = buttonY - y - kGap;
    if (listHeight < 40) {
        listHeight = 40;
    }
    if (m_listView) {
        MoveWindow(m_listView, kMargin, y, contentWidth, listHeight, TRUE);
    }

    // Only the buttons that are actually offered take up space, so a backend
    // without a status concept gets two wide buttons rather than two narrow
    // ones and a hole.
    HWND row[3];
    int visible = 0;
    if (m_moveButton && m_canMove) {
        row[visible++] = m_moveButton;
    }
    if (m_statusButton && m_canChangeStatus) {
        row[visible++] = m_statusButton;
    }
    if (m_backButton) {
        row[visible++] = m_backButton;
    }
    if (visible == 0) {
        return;
    }

    int buttonWidth = (contentWidth - (kGap * (visible - 1))) / visible;
    if (buttonWidth < 40) {
        buttonWidth = 40;
    }

    for (int i = 0; i < visible; i++) {
        MoveWindow(row[i], kMargin + i * (buttonWidth + kGap), buttonY,
                   buttonWidth, kButtonHeight, TRUE);
    }
}

void DeviceView::InitializeListView()
{
    if (!m_listView) {
        return;
    }

    ListView_SetExtendedListViewStyle(m_listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN column = {0};
    column.mask = LVCF_TEXT | LVCF_WIDTH;

    column.pszText = (TCHAR*)TEXT("Field");
    column.cx = 72;
    ListView_InsertColumn(m_listView, 0, &column);

    column.pszText = (TCHAR*)TEXT("Value");
    column.cx = 146;
    ListView_InsertColumn(m_listView, 1, &column);
}

} // namespace Views
} // namespace HBX
