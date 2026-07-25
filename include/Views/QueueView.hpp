#ifndef VIEWS_QUEUEVIEW_HPP
#define VIEWS_QUEUEVIEW_HPP

#include <windows.h>
#include "ViewHelpers.hpp"
#include "../SyncEngine.hpp"

namespace HBX {
namespace Views {

/**
 * Offline queue management view
 * Displays pending transactions and sync status
 */
class QueueView {
public:
    QueueView();
    ~QueueView();

    // Window management
    bool Create(HWND parentWnd, HINSTANCE hInstance);
    void Show(bool visible);
    void Destroy();
    HWND GetHandle() const;

    // Data display
    void RefreshQueue();
    void SetSyncEngine(SyncEngine* syncEngine);
    void UpdateSyncStatus(SyncEngine::SyncStatus status);

    // UI updates
    void AddQueuedItem(const TCHAR* description, const TCHAR* status = NULL);
    void RemoveQueuedItem(int index);
    void ClearQueue();
    void SetItemCount(int count);

    // Callback for sync events
    typedef void (*SyncCallback)(void* userData);
    void SetSyncCallback(SyncCallback callback, void* userData);

    // Raised after a per-row retry or removal changed the queue, so the owner
    // can refresh anything of its own that shows the queue depth.
    typedef void (*QueueChangedCallback)(void* userData);
    void SetQueueChangedCallback(QueueChangedCallback callback, void* userData);

private:
    enum {
        ID_STATUS_LABEL = 3001,
        ID_COUNT_LABEL  = 3002,
        ID_LISTVIEW     = 3003,
        ID_SYNC_BUTTON  = 3004,
        ID_CLEAR_BUTTON = 3005,
        ID_RETRY_BUTTON = 3006,
        ID_REMOVE_BUTTON = 3007
    };

    HWND m_hwnd;
    HWND m_listView;
    HWND m_syncButton;
    HWND m_clearButton;
    HWND m_retryButton;
    HWND m_removeButton;
    HWND m_statusLabel;
    HWND m_countLabel;
    HINSTANCE m_hInstance;
    SyncEngine* m_syncEngine;
    SyncCallback m_syncCallback;
    void* m_callbackUserData;
    QueueChangedCallback m_changedCallback;
    void* m_changedUserData;
    int m_selectedIndex;   // index of the currently selected queue row, or -1

    // Full journal record lines backing the visible rows, in row order. The
    // list shows only the payload, but a retry or a removal has to name the
    // exact record, so the originals are kept here.
    TCHAR** m_queueLines;
    int m_queueLineCount;

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Event handlers
    void OnSyncClick();
    void OnClearClick();
    void OnRetryClick();
    void OnRemoveClick();
    void OnItemSelected(int index);

    // UI layout
    void LayoutControls();
    void InitializeListView();

    void ReleaseQueueLines();
    void NotifyQueueChanged();
    const TCHAR* PendingRowStatus() const;
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_QUEUEVIEW_HPP
