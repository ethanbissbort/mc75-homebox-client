#include "../../include/Views/QueueView.hpp"

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
    // TODO: Create window and controls
    InitializeListView();
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
    // TODO: Refresh queue list
}

void QueueView::SetSyncEngine(SyncEngine* syncEngine)
{
    m_syncEngine = syncEngine;
}

void QueueView::UpdateSyncStatus(SyncEngine::SyncStatus status)
{
    // TODO: Update status display
}

void QueueView::AddQueuedItem(const TCHAR* description)
{
    // TODO: Add item to list view
}

void QueueView::RemoveQueuedItem(int index)
{
    // TODO: Remove item from list view
}

void QueueView::ClearQueue()
{
    // TODO: Clear list view
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
    // TODO: Implement window procedure
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
    // TODO: Confirm and clear queue
}

void QueueView::OnItemSelected(int index)
{
    // TODO: Handle item selection
}

void QueueView::LayoutControls()
{
    // TODO: Layout controls
}

void QueueView::InitializeListView()
{
    // TODO: Initialize list view columns
}

} // namespace Views
} // namespace HBX
