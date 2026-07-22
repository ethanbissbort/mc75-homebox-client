#ifndef SYNCENGINE_HPP
#define SYNCENGINE_HPP

#include <windows.h>
#include "HbClient.hpp"
#include "Journal.hpp"

namespace HBX {

/**
 * Offline/online synchronization engine
 * Manages queued operations and sync state
 */
class SyncEngine {
public:
    SyncEngine(HbClient* hbClient, Journal* journal);
    ~SyncEngine();

    // Queue management
    bool QueueTransaction(const TCHAR* transactionType, const TCHAR* data);
    int GetQueuedTransactionCount() const;
    // Returns the pending (unsynced) transaction strings. On success *transactions
    // is a heap TCHAR*[] of *count heap TCHAR* entries; the caller must delete[]
    // each entry and then delete[] the array. Returns true (with count 0) when empty.
    bool GetQueuedTransactions(TCHAR*** transactions, int* count) const;
    bool ClearQueue();

    // Sync operations
    bool Sync();
    bool SyncItem(const TCHAR* transactionId);
    bool IsOnline() const;

    // Sync status
    enum SyncStatus {
        SYNC_IDLE,
        SYNC_IN_PROGRESS,
        SYNC_SUCCESS,
        SYNC_PARTIAL,   // some queued transactions synced, some failed
        SYNC_FAILED,
        SYNC_OFFLINE    // no network connectivity
    };

    SyncStatus GetSyncStatus() const;
    const TCHAR* GetLastSyncError() const;
    DWORD GetLastSyncTime() const;

    // Configuration
    void SetAutoSyncEnabled(bool enabled);
    bool IsAutoSyncEnabled() const;

private:
    HbClient* m_hbClient;
    Journal* m_journal;
    SyncStatus m_syncStatus;
    TCHAR* m_lastSyncError;
    DWORD m_lastSyncTime;
    bool m_autoSyncEnabled;

    // Helper methods
    bool CheckConnectivity() const;
    bool ProcessQueuedTransaction(const TCHAR* transaction);
};

} // namespace HBX

#endif // SYNCENGINE_HPP
