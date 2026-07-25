#include "../../include/Views/QueueView.hpp"
#include "../../include/Views/ViewHelpers.hpp"
#include "../../include/Journal.hpp"
#include <commctrl.h>

// The host shim carries only the list-view surface the original code used; the
// real <commctrl.h> defines this macro, so the guard is a no-op on device.
#ifndef LVM_SETITEMTEXT
#define LVM_SETITEMTEXT (LVM_FIRST + 116)
#endif
#ifndef ListView_SetItemText
#define ListView_SetItemText(w, i, iSub, txt) \
    do { LVITEM _lvi = {0}; _lvi.iSubItem = (iSub); _lvi.pszText = (txt); \
         SendMessage((w), LVM_SETITEMTEXT, (WPARAM)(int)(i), (LPARAM)&_lvi); } while (0)
#endif

namespace HBX {
namespace Views {

QueueView::QueueView()
    : m_hwnd(NULL)
    , m_listView(NULL)
    , m_syncButton(NULL)
    , m_clearButton(NULL)
    , m_retryButton(NULL)
    , m_removeButton(NULL)
    , m_statusLabel(NULL)
    , m_countLabel(NULL)
    , m_hInstance(NULL)
    , m_syncEngine(NULL)
    , m_syncCallback(NULL)
    , m_callbackUserData(NULL)
    , m_changedCallback(NULL)
    , m_changedUserData(NULL)
    , m_selectedIndex(-1)
    , m_queueLines(NULL)
    , m_queueLineCount(0)
{
}

QueueView::~QueueView()
{
    Destroy();
    ReleaseQueueLines();
}

bool QueueView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Register a window class that installs QueueView::WindowProc so the
    // buttons and list-view selection notifications reach this instance (the
    // built-in STATIC class would drop them).
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
        (HMENU)ID_STATUS_LABEL,
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
        (HMENU)ID_COUNT_LABEL,
        hInstance,
        NULL
    );

    // Create list view
    m_listView = CreateWindow(
        WC_LISTVIEW,
        TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
        10, 60, 220, 120,
        m_hwnd,
        (HMENU)ID_LISTVIEW,
        hInstance,
        NULL
    );

    // Per-row actions on the selected transaction. Without them the only way
    // out of a stuck entry was the all-or-nothing Clear button, which throws
    // away every queued scan the device has not managed to upload yet.
    m_retryButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Retry"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 190, 90, 30,
        m_hwnd,
        (HMENU)ID_RETRY_BUTTON,
        hInstance,
        NULL
    );

    m_removeButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Remove"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 190, 90, 30,
        m_hwnd,
        (HMENU)ID_REMOVE_BUTTON,
        hInstance,
        NULL
    );

    // Create sync button
    m_syncButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Sync"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 225, 90, 30,
        m_hwnd,
        (HMENU)ID_SYNC_BUTTON,
        hInstance,
        NULL
    );

    // Create clear button
    m_clearButton = CreateWindow(
        TEXT("BUTTON"),
        TEXT("Clear"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 225, 90, 30,
        m_hwnd,
        (HMENU)ID_CLEAR_BUTTON,
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

void QueueView::ReleaseQueueLines()
{
    if (m_queueLines) {
        for (int i = 0; i < m_queueLineCount; i++) {
            delete[] m_queueLines[i];
        }
        delete[] m_queueLines;
        m_queueLines = NULL;
    }
    m_queueLineCount = 0;
}

void QueueView::NotifyQueueChanged()
{
    if (m_changedCallback) {
        m_changedCallback(m_changedUserData);
    }
}

const TCHAR* QueueView::PendingRowStatus() const
{
    // Anything still in the queue right after a failed or partial sync is, by
    // definition, one of the entries that did not go through.
    if (!m_syncEngine) {
        return TEXT("Pending");
    }

    switch (m_syncEngine->GetSyncStatus()) {
        case SyncEngine::SYNC_FAILED:
        case SyncEngine::SYNC_PARTIAL:
            return TEXT("Failed");
        case SyncEngine::SYNC_OFFLINE:
            return TEXT("Offline");
        default:
            return TEXT("Pending");
    }
}

void QueueView::RefreshQueue()
{
    if (!m_listView || !m_syncEngine) {
        return;
    }

    // Clear current list
    ListView_DeleteAllItems(m_listView);
    ReleaseQueueLines();
    m_selectedIndex = -1;

    // Pull the actual pending (unsynced) transactions from the sync engine.
    // GetQueuedTransactions hands back a heap TCHAR*[] of heap strings that we
    // take ownership of and keep for as long as the rows are on screen.
    TCHAR** transactions = NULL;
    int count = 0;
    if (m_syncEngine->GetQueuedTransactions(&transactions, &count) && transactions) {
        m_queueLines = transactions;
        m_queueLineCount = count;

        const TCHAR* rowStatus = PendingRowStatus();
        for (int i = 0; i < count; i++) {
            if (!m_queueLines[i]) {
                continue;
            }
            // Show the queued payload, not the whole journal record: the
            // "[timestamp] TRANS nnnnnnnn: " prefix eats the entire visible
            // column width on a QVGA screen.
            const TCHAR* payload = Journal::PayloadOf(m_queueLines[i]);
            AddQueuedItem(payload ? payload : m_queueLines[i], rowStatus);
        }
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

    const TCHAR* statusText = TEXT("Queue Status: Unknown");
    switch (status) {
        case SyncEngine::SYNC_IDLE:
            statusText = TEXT("Queue Status: Idle");
            break;
        case SyncEngine::SYNC_IN_PROGRESS:
            statusText = TEXT("Queue Status: Syncing...");
            break;
        case SyncEngine::SYNC_SUCCESS:
            statusText = TEXT("Queue Status: Success");
            break;
        case SyncEngine::SYNC_PARTIAL:
            statusText = TEXT("Queue Status: Partial Sync");
            break;
        case SyncEngine::SYNC_FAILED:
            statusText = TEXT("Queue Status: Failed");
            break;
        case SyncEngine::SYNC_OFFLINE:
            statusText = TEXT("Queue Status: Offline");
            break;
        default:
            break;
    }

    SetWindowText(m_statusLabel, statusText);
}

void QueueView::AddQueuedItem(const TCHAR* description, const TCHAR* status)
{
    if (!m_listView || !description) {
        return;
    }

    LVITEM item = {0};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(m_listView);
    item.iSubItem = 0;
    item.pszText = (TCHAR*)description;

    int inserted = ListView_InsertItem(m_listView, &item);
    if (inserted >= 0) {
        // The Status column was created but never written, so every row showed
        // an empty cell no matter what had happened to the transaction.
        ListView_SetItemText(m_listView, inserted, 1,
                             (TCHAR*)(status ? status : TEXT("Pending")));
    }

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
    ReleaseQueueLines();
    m_selectedIndex = -1;

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

void QueueView::SetQueueChangedCallback(QueueChangedCallback callback, void* userData)
{
    m_changedCallback = callback;
    m_changedUserData = userData;
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

                if (notifyCode == BN_CLICKED) {
                    switch (ctrlId) {
                        case ID_SYNC_BUTTON:
                            pThis->OnSyncClick();
                            return 0;
                        case ID_CLEAR_BUTTON:
                            pThis->OnClearClick();
                            return 0;
                        case ID_RETRY_BUTTON:
                            pThis->OnRetryClick();
                            return 0;
                        case ID_REMOVE_BUTTON:
                            pThis->OnRemoveClick();
                            return 0;
                        default:
                            break;
                    }
                }
            }
            break;

        case WM_NOTIFY:
            if (pThis) {
                NMHDR* pnmhdr = (NMHDR*)lParam;
                if (pnmhdr->idFrom == ID_LISTVIEW && pnmhdr->code == LVN_ITEMCHANGED) {
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

        NotifyQueueChanged();
    }
}

void QueueView::OnRetryClick()
{
    if (!m_syncEngine || m_selectedIndex < 0 || m_selectedIndex >= m_queueLineCount ||
        !m_queueLines || !m_queueLines[m_selectedIndex]) {
        MessageBox(m_hwnd, TEXT("Select a queued transaction first."),
                   TEXT("Retry"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (m_syncEngine->SyncItem(m_queueLines[m_selectedIndex])) {
        RefreshQueue();
        NotifyQueueChanged();
        return;
    }

    ListView_SetItemText(m_listView, m_selectedIndex, 1, (TCHAR*)TEXT("Failed"));
    MessageBox(m_hwnd, TEXT("Retry failed. The transaction stays queued."),
               TEXT("Retry"), MB_OK | MB_ICONWARNING);
}

void QueueView::OnRemoveClick()
{
    if (!m_syncEngine || m_selectedIndex < 0 || m_selectedIndex >= m_queueLineCount ||
        !m_queueLines || !m_queueLines[m_selectedIndex]) {
        MessageBox(m_hwnd, TEXT("Select a queued transaction first."),
                   TEXT("Remove"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!ViewHelpers::ShowConfirm(m_hwnd, TEXT("Confirm Remove"),
                                  TEXT("Drop this transaction from the queue?"))) {
        return;
    }

    if (!m_syncEngine->RemoveQueuedTransaction(m_queueLines[m_selectedIndex])) {
        MessageBox(m_hwnd, TEXT("Failed to remove the transaction from storage."),
                   TEXT("Error"), MB_OK | MB_ICONERROR);
        return;
    }

    RefreshQueue();
    NotifyQueueChanged();
}

void QueueView::OnItemSelected(int index)
{
    // Remember which queued transaction the user picked so Retry / Remove can
    // operate on it. A negative index means "no selection".
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
    if (width <= 0 || height <= 0) {
        return;
    }

    const int margin = 8;
    const int buttonHeight = 30;
    const int gap = 4;
    const int listTop = 60;

    int contentWidth = width - (2 * margin);
    if (contentWidth < 60) {
        contentWidth = 60;
    }

    if (m_statusLabel) {
        MoveWindow(m_statusLabel, margin, 8, contentWidth, 20, TRUE);
    }

    if (m_countLabel) {
        MoveWindow(m_countLabel, margin, 32, contentWidth, 20, TRUE);
    }

    // Two rows of buttons, anchored to the bottom of whatever client area we
    // actually got. The old fixed 320-px arithmetic pushed them below the
    // visible area once the soft-key menu bar had taken its strip.
    int bottomRowY = height - buttonHeight - margin;
    int topRowY = bottomRowY - buttonHeight - gap;

    int listHeight = topRowY - listTop - gap;
    if (listHeight < 40) {
        listHeight = 40;
    }

    if (m_listView) {
        MoveWindow(m_listView, margin, listTop, contentWidth, listHeight, TRUE);
    }

    int buttonWidth = (contentWidth - margin) / 2;
    if (buttonWidth < 50) {
        buttonWidth = 50;
    }
    int rightX = margin + buttonWidth + margin;

    if (m_retryButton) {
        MoveWindow(m_retryButton, margin, topRowY, buttonWidth, buttonHeight, TRUE);
    }
    if (m_removeButton) {
        MoveWindow(m_removeButton, rightX, topRowY, buttonWidth, buttonHeight, TRUE);
    }
    if (m_syncButton) {
        MoveWindow(m_syncButton, margin, bottomRowY, buttonWidth, buttonHeight, TRUE);
    }
    if (m_clearButton) {
        MoveWindow(m_clearButton, rightX, bottomRowY, buttonWidth, buttonHeight, TRUE);
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
    column.pszText = (TCHAR*)TEXT("Transaction");
    column.cx = 150;
    ListView_InsertColumn(m_listView, 0, &column);

    // Status column
    column.pszText = (TCHAR*)TEXT("Status");
    column.cx = 60;
    ListView_InsertColumn(m_listView, 1, &column);
}

} // namespace Views
} // namespace HBX
