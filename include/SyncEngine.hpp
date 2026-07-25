#ifndef SYNCENGINE_HPP
#define SYNCENGINE_HPP

#include <windows.h>
#include "HbClient.hpp"
#include "Journal.hpp"

namespace HBX {

/**
 * Offline/online synchronization engine
 * Manages queued operations and sync state
 *
 * Queue payload format
 * --------------------
 * QueueTransaction stores "[tick] TYPE: DATA" as the journal payload; the
 * journal wraps that in its own record header, so a line handed back by
 * GetQueuedTransactions looks like
 *
 *   [2026-07-25 09:14:09] TRANS 00000042: [51234] ITEM_SCAN: SCAN:123456789
 *
 * Replay strips the record header with Journal::PayloadOf and dispatches on
 * TYPE. DATA is one of:
 *
 *   ITEM_SCAN    "SCAN:<barcode>"
 *                "SCANLOC:<barcodeLength>:<barcode><locationId>"
 *   ITEM_UPDATE  "UPDATE:<item json>"
 *
 * A barcode can legally contain ':' and '@' (Code 128 and QR both encode them),
 * so the location variant carries an explicit barcode length instead of a
 * delimiter that barcode content could collide with.
 */
class SyncEngine {
public:
    SyncEngine(HbClient* hbClient, Journal* journal);
    ~SyncEngine();

    // Queue management
    bool QueueTransaction(const TCHAR* transactionType, const TCHAR* data);

    /**
     * Queues a scan for later replay. `locationId` may be NULL or empty, in
     * which case only the item lookup is replayed; otherwise the scan also
     * pushes a location move once connectivity returns.
     */
    bool QueueScan(const TCHAR* barcode, const TCHAR* locationId);

    int GetQueuedTransactionCount() const;
    // Returns the pending (unsynced) transaction strings. On success *transactions
    // is a heap TCHAR*[] of *count heap TCHAR* entries; the caller must delete[]
    // each entry and then delete[] the array. Returns true (with count 0) when empty.
    bool GetQueuedTransactions(TCHAR*** transactions, int* count) const;

    /**
     * Drops a single queued entry without sending it, given a record line as
     * returned by GetQueuedTransactions. Returns false if the line is not a
     * queued record.
     */
    bool RemoveQueuedTransaction(const TCHAR* transactionLine);

    bool ClearQueue();

    // Sync operations
    bool Sync();
    /** Replays one entry; `transactionLine` is a record line from GetQueuedTransactions. */
    bool SyncItem(const TCHAR* transactionLine);
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

    /**
     * Minimum spacing between automatic syncs. Values below a few seconds are
     * clamped: the caller polls this from the UI timer, and a zero interval
     * would start a sync on every tick.
     */
    void SetAutoSyncIntervalSeconds(int seconds);
    int GetAutoSyncIntervalSeconds() const;

    /**
     * True when auto-sync is enabled, no sync is running, work is queued and
     * either no sync has been attempted yet (a queue recovered from disk at
     * startup drains promptly) or the interval has elapsed since the last
     * attempt. `nowTick` is a GetTickCount() value; the comparison is wrap-safe.
     */
    bool ShouldAutoSync(DWORD nowTick) const;

private:
    HbClient* m_hbClient;
    Journal* m_journal;
    SyncStatus m_syncStatus;
    TCHAR* m_lastSyncError;
    DWORD m_lastSyncTime;
    DWORD m_lastSyncAttemptTick;
    bool m_syncAttempted;
    bool m_autoSyncEnabled;
    int m_autoSyncIntervalSeconds;

    // Connectivity is probed with a blocking DNS lookup, which costs seconds on
    // a GPRS link, and Sync()/IsOnline()/the UI status line all ask for it. The
    // answer is cached for a few seconds; these are mutable because the query
    // itself is logically const.
    mutable DWORD m_connectivityTick;
    mutable bool m_connectivityValid;
    mutable bool m_connectivityOnline;

    // Helper methods
    bool CheckConnectivity() const;
    bool ProcessQueuedTransaction(const TCHAR* transaction);
    void SetLastSyncError(const TCHAR* message);

    // Not copyable: owns the last-error allocation.
    SyncEngine(const SyncEngine&);
    SyncEngine& operator=(const SyncEngine&);
};

} // namespace HBX

#endif // SYNCENGINE_HPP
