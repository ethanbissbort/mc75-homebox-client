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
 *
 * With more than one backend registered the queue holds entries for servers
 * that are not currently active, so every row names the backend it belongs to.
 * That is not decoration: an entry tagged for an instance that is no longer
 * configured can never be sent, and the operator can only decide to remove it
 * if the screen says which server it was waiting for.
 */
class QueueView {
public:
    enum {
        /**
         * Longest backend instance id shown in the row tag. Ids are bounded by
         * SyncEngine::MAX_INSTANCE_ID_CHARS; this is the display width, and a
         * longer id is truncated rather than pushed into the next column.
         */
        BACKEND_TAG_CHARS = 16,

        /** Row text: the transaction type plus as much payload as fits. */
        ROW_TEXT_CHARS = 96,

        /** Distinct backends named in the clear-confirmation breakdown. */
        MAX_TALLIED_BACKENDS = 4
    };

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
    void AddQueuedItem(const TCHAR* backend, const TCHAR* description, const TCHAR* status);
    void RemoveQueuedItem(int index);
    void ClearQueue();
    void SetItemCount(int count);

    /**
     * Splits a queued payload -- "[tick] <instanceId>.<TYPE>: DATA" -- into the
     * backend tag and a one-line description for the row. An untagged record
     * predates backend tagging and replays against HomeBox, so it is reported
     * as the legacy HomeBox kind rather than as unknown. A payload that does
     * not parse at all yields an empty tag and the raw text, which is what the
     * operator needs in order to remove it by hand.
     *
     * Static and free of window handles so the formatting can be exercised
     * without a UI.
     */
    static void SplitPayload(const TCHAR* payload,
                             TCHAR* backend, int backendCap,
                             TCHAR* description, int descriptionCap);

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
        ID_REMOVE_BUTTON = 3007,
        ID_SKIPPED_LABEL = 3008
    };

    HWND m_hwnd;
    HWND m_listView;
    HWND m_syncButton;
    HWND m_clearButton;
    HWND m_retryButton;
    HWND m_removeButton;
    HWND m_statusLabel;
    HWND m_countLabel;

    // Skipped entries get a line of their own rather than being folded into the
    // status line: they are not failures, and showing them next to "Failed"
    // teaches the operator that a working queue is broken.
    HWND m_skippedLabel;

    HINSTANCE m_hInstance;
    SyncEngine* m_syncEngine;
    SyncCallback m_syncCallback;
    void* m_callbackUserData;
    QueueChangedCallback m_changedCallback;
    void* m_changedUserData;
    int m_selectedIndex;   // index of the currently selected queue row, or -1

    // Skipped entries seen by the last sync. Cached because the layout reserves
    // a line for the indicator only when there is something to say.
    int m_skippedCount;

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

    void UpdateSkippedIndicator();

    /**
     * Composes the clear confirmation: how many entries are about to be
     * destroyed and which backends they were waiting for. Returns false when
     * there is nothing queued, in which case no confirmation should be shown.
     */
    bool BuildClearPrompt(TCHAR* out, int cap) const;
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_QUEUEVIEW_HPP
