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
    , m_selectedIndex(-1)
{
}

QueueView::~QueueView()
{
    Destroy();
}

bool QueueView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Register a window class that installs QueueView::WindowProc so the Sync /
    // Clear buttons and list-view selection notifications reach this instance
    // (the built-in STATIC class would drop them).
    static const TCHAR* kClassName = TEXT("HBXQueueView");
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = QueueView::WindowProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClass(&wc); // harmless if already registered by a prior Create()

    // Create main window using our class; pass 'this' so WM_CREATE can stash
    // the instance pointer (see WindowProc).
    m_hwnd = CreateWindow(
        kClassName,
        TEXT("Queue View"),
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
    m_selectedIndex = -1;

    // Pull the actual pending (unsynced) transactions from the sync engine and
    // list each one. GetQueuedTransactions hands back a heap TCHAR*[] of heap
    // strings that we own and must free.
    TCHAR** transactions = NULL;
    int count = 0;
    if (m_syncEngine->GetQueuedTransactions(&transactions, &count) && transactions) {
        for (int i = 0; i < count; i++) {
            if (transactions[i]) {
                AddQueuedItem(transactions[i]);   // ListView copies the text
                delete[] transactions[i];
            }
        }
        delete[] transactions;
    }

    // Keep the count label in sync with the engine's authoritative count.
    SetItemCount(m_syncEngine->GetQueuedTransactionCount());
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

    LVITEM item = {0};
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

        // Also clear the backing store (journal) so the transactions are truly
        // removed, not just hidden from the list view.
        if (m_syncEngine) {
            if (!m_syncEngine->ClearQueue()) {
                MessageBox(m_hwnd,
                    TEXT("Failed to clear the queued transactions from storage."),
                    TEXT("Error"),
                    MB_OK | MB_ICONERROR);
            }
        }
    }
}

void QueueView::OnItemSelected(int index)
{
    // Remember which queued transaction the user picked so actions (e.g. a
    // future per-row retry/remove) can operate on it. A negative index means
    // "no selection".
    if (index < 0) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = index;
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
    LVCOLUMN column = {0};
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
