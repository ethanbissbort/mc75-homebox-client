#include "../../include/Views/QueueView.hpp"
#include <commctrl.h>

namespace HBX {
namespace Views {

QueueView::QueueView()
    : m_hwnd(NULL)
    , m_listView(NULL)
    , m_syncButton(NULL)
    , m_clearButton(NULL)
    , m_statusLabel(NULL)
    , m_countLabel(NULL)
    , m_hInstance(NULL)
    , m_syncEngine(NULL)
    , m_syncCallback(NULL)
    , m_callbackUserData(NULL)
{
}

QueueView::~QueueView()
{
    Destroy();
}

bool QueueView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Create main window
    m_hwnd = CreateWindow(
        TEXT("STATIC"),
        TEXT("Queue View"),
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
        TEXT("Queue Status: Idle"),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        10, 10, 220, 20,
        m_hwnd,
        (HMENU)3001,
        hInstance,
        NULL
    );

    // Create count label
    m_countLabel = CreateWindow(
        TEXT("STATIC"),
        TEXT("Items: 0"),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        10, 35, 220, 20,
        m_hwnd,
        (HMENU)3002,
        hInstance,
        NULL
    );

    // Create list view
    m_listView = CreateWindow(
        WC_LISTVIEW,
        TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
        10, 60, 220, 180,
        m_hwnd,
        (HMENU)3003,
        hInstance,
        NULL
    );

    // Create sync button
    m_syncButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Sync"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 250, 90, 35,
        m_hwnd,
        (HMENU)3004,
        hInstance,
        NULL
    );

    // Create clear button
    m_clearButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Clear"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, 250, 90, 35,
        m_hwnd,
        (HMENU)3005,
        hInstance,
        NULL
    );

    InitializeListView();
    LayoutControls();

    return true;
}

void QueueView::Show(bool visible)
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
    }
}

void QueueView::Destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

HWND QueueView::GetHandle() const
{
    return m_hwnd;
}

void QueueView::RefreshQueue()
{
    if (!m_listView || !m_syncEngine) {
        return;
    }

    // Clear current list
    ListView_DeleteAllItems(m_listView);

    // Get pending transactions from sync engine's journal
    // In a real implementation, we'd query the sync engine
    // For now, just update the count
    SetItemCount(0);
}

void QueueView::SetSyncEngine(SyncEngine* syncEngine)
{
    m_syncEngine = syncEngine;
}

void QueueView::UpdateSyncStatus(SyncEngine::SyncStatus status)
{
    if (!m_statusLabel) {
        return;
    }

    TCHAR statusText[128];
    switch (status) {
        case SyncEngine::SYNC_IDLE:
            lstrcpy(statusText, TEXT("Queue Status: Idle"));
            break;
        case SyncEngine::SYNC_IN_PROGRESS:
            lstrcpy(statusText, TEXT("Queue Status: Syncing..."));
            break;
        case SyncEngine::SYNC_SUCCESS:
            lstrcpy(statusText, TEXT("Queue Status: Success"));
            break;
        case SyncEngine::SYNC_PARTIAL:
            lstrcpy(statusText, TEXT("Queue Status: Partial Sync"));
            break;
        case SyncEngine::SYNC_FAILED:
            lstrcpy(statusText, TEXT("Queue Status: Failed"));
            break;
        case SyncEngine::SYNC_OFFLINE:
            lstrcpy(statusText, TEXT("Queue Status: Offline"));
            break;
        default:
            lstrcpy(statusText, TEXT("Queue Status: Unknown"));
            break;
    }

    SetWindowText(m_statusLabel, statusText);
}

void QueueView::AddQueuedItem(const TCHAR* description)
{
    if (!m_listView || !description) {
        return;
    }

    LVITEM item;
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(m_listView);
    item.iSubItem = 0;
    item.pszText = (TCHAR*)description;

    ListView_InsertItem(m_listView, &item);

    // Update count
    SetItemCount(ListView_GetItemCount(m_listView));
}

void QueueView::RemoveQueuedItem(int index)
{
    if (!m_listView) {
        return;
    }

    ListView_DeleteItem(m_listView, index);

    // Update count
    SetItemCount(ListView_GetItemCount(m_listView));
}

void QueueView::ClearQueue()
{
    if (!m_listView) {
        return;
    }

    ListView_DeleteAllItems(m_listView);

    // Update count
    SetItemCount(0);
}

void QueueView::SetItemCount(int count)
{
    if (m_countLabel) {
        TCHAR buffer[64];
        wsprintf(buffer, TEXT("Items: %d"), count);
        SetWindowText(m_countLabel, buffer);
    }
}

void QueueView::SetSyncCallback(SyncCallback callback, void* userData)
{
    m_syncCallback = callback;
    m_callbackUserData = userData;
}

LRESULT CALLBACK QueueView::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    QueueView* pThis = NULL;

    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (QueueView*)pCreate->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)pThis);
    } else {
        pThis = (QueueView*)GetWindowLong(hwnd, GWL_USERDATA);
    }

    switch (uMsg) {
        case WM_COMMAND:
            if (pThis) {
                WORD ctrlId = LOWORD(wParam);
                WORD notifyCode = HIWORD(wParam);

                if (ctrlId == 3004 && notifyCode == BN_CLICKED) {
                    pThis->OnSyncClick();
                    return 0;
                } else if (ctrlId == 3005 && notifyCode == BN_CLICKED) {
                    pThis->OnClearClick();
                    return 0;
                }
            }
            break;

        case WM_NOTIFY:
            if (pThis) {
                NMHDR* pnmhdr = (NMHDR*)lParam;
                if (pnmhdr->idFrom == 3003 && pnmhdr->code == LVN_ITEMCHANGED) {
                    NMLISTVIEW* pnmlv = (NMLISTVIEW*)lParam;
                    if (pnmlv->uNewState & LVIS_SELECTED) {
                        pThis->OnItemSelected(pnmlv->iItem);
                    }
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

void QueueView::OnSyncClick()
{
    if (m_syncCallback) {
        m_syncCallback(m_callbackUserData);
    }
}

void QueueView::OnClearClick()
{
    // Confirm before clearing
    int result = MessageBox(
        m_hwnd,
        TEXT("Clear all queued transactions? This cannot be undone."),
        TEXT("Confirm Clear"),
        MB_YESNO | MB_ICONWARNING
    );

    if (result == IDYES) {
        ClearQueue();

        // In a real implementation, also clear from the sync engine/journal
        if (m_syncEngine) {
            // m_syncEngine->ClearQueue();
        }
    }
}

void QueueView::OnItemSelected(int index)
{
    // Item selected in list view
    // Could be used to show item details or enable/disable buttons
    // For now, just a placeholder
}

void QueueView::LayoutControls()
{
    if (!m_hwnd) {
        return;
    }

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);

    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    int margin = 10;

    // Layout status label
    if (m_statusLabel) {
        MoveWindow(m_statusLabel, margin, margin, width - (2 * margin), 20, TRUE);
    }

    // Layout count label
    if (m_countLabel) {
        MoveWindow(m_countLabel, margin, 35, width - (2 * margin), 20, TRUE);
    }

    // Layout list view
    if (m_listView) {
        int listHeight = height - 150;
        if (listHeight < 100) listHeight = 100;
        MoveWindow(m_listView, margin, 60, width - (2 * margin), listHeight, TRUE);
    }

    // Layout buttons at bottom
    int buttonY = height - 45;
    int buttonWidth = (width - (3 * margin)) / 2;

    if (m_syncButton) {
        MoveWindow(m_syncButton, margin, buttonY, buttonWidth, 35, TRUE);
    }

    if (m_clearButton) {
        MoveWindow(m_clearButton, margin + buttonWidth + margin, buttonY, buttonWidth, 35, TRUE);
    }
}

void QueueView::InitializeListView()
{
    if (!m_listView) {
        return;
    }

    // Set extended styles
    ListView_SetExtendedListViewStyle(m_listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Add columns
    LVCOLUMN column;
    column.mask = LVCF_TEXT | LVCF_WIDTH;

    // Transaction column
    column.pszText = TEXT("Transaction");
    column.cx = 180;
    ListView_InsertColumn(m_listView, 0, &column);

    // Status column (optional, could add more columns)
    column.pszText = TEXT("Status");
    column.cx = 60;
    ListView_InsertColumn(m_listView, 1, &column);
}

} // namespace Views
} // namespace HBX
