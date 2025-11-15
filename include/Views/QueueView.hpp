#ifndef VIEWS_QUEUEVIEW_HPP
#define VIEWS_QUEUEVIEW_HPP

#include <windows.h>
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
    void AddQueuedItem(const TCHAR* description);
    void RemoveQueuedItem(int index);
    void ClearQueue();
    void SetItemCount(int count);

    // Callback for sync events
    typedef void (*SyncCallback)(void* userData);
    void SetSyncCallback(SyncCallback callback, void* userData);

private:
    HWND m_hwnd;
    HWND m_listView;
    HWND m_syncButton;
    HWND m_clearButton;
    HWND m_statusLabel;
    HWND m_countLabel;
    HINSTANCE m_hInstance;
    SyncEngine* m_syncEngine;
    SyncCallback m_syncCallback;
    void* m_callbackUserData;

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Event handlers
    void OnSyncClick();
    void OnClearClick();
    void OnItemSelected(int index);

    // UI layout
    void LayoutControls();
    void InitializeListView();
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_QUEUEVIEW_HPP
