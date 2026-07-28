#ifndef SYNCENGINE_HPP
#define SYNCENGINE_HPP

#include <windows.h>
#include "InventoryBackend.hpp"
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
 *   [2026-07-25 09:14:09] TRANS 00000042: [51234] hb.ITEM_SCAN: SCAN:123456789
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
 *
 * Backend tagging
 * ---------------
 * TYPE is namespaced with the *instance id* of the backend the entry belongs
 * to -- "hb.ITEM_SCAN", "nb-prod.DEVICE_MOVE" -- and replay routes on it. The
 * instance matters, not just the kind: asset tags are unique per NetBox
 * instance rather than globally, so replaying a queued move against the wrong
 * instance would silently move whatever device happens to hold that tag there.
 * A wrong-instance replay looks like a success, which is worse than a failure.
 *
 * The tag is optional on the way in. A record written before this existed has a
 * bare "ITEM_SCAN" and still replays against HomeBox, so upgrading a device in
 * the field does not strand the queue it is carrying.
 */
class SyncEngine {
public:
    enum {
        /**
         * Backend registry size. Two backends ship (HomeBox and NetBox); the
         * spare slots cover a second instance of either without turning a
         * fixed array into a linked structure on a memory-constrained device.
         */
        MAX_BACKENDS = 4,

        /**
         * Instance ids are written into every queue record and copied into the
         * connectivity cache, so they are bounded here rather than wherever
         * they came from.
         */
        MAX_INSTANCE_ID_CHARS = 32
    };

    /**
     * Registers `backend` and makes it active, which is what a single-backend
     * caller wants. Either argument may be NULL.
     */
    SyncEngine(InventoryBackend* backend, Journal* journal);
    ~SyncEngine();

    // ---- backend registry -----------------------------------------------
    //
    // The engine holds every *configured* backend, not just the active one:
    // switching backends does not drain the queue, so entries for the backend
    // the operator just left still have to find their way home.

    /**
     * Adds a backend to the registry. Ignored when the registry is full, when
     * the backend is already registered, or when its instance id is not usable
     * in a queue record (see IsValidInstanceId) -- an entry tagged with an
     * unparseable id could never be routed back. The first backend registered
     * becomes the active one.
     *
     * The engine does not take ownership; backends must outlive it.
     */
    void RegisterBackend(InventoryBackend* backend);

    /** Selects the backend new work is queued against. False if unknown. */
    bool SetActiveBackend(const TCHAR* instanceId);

    InventoryBackend* GetActiveBackend() const;
    InventoryBackend* FindBackend(const TCHAR* instanceId) const;
    int GetBackendCount() const;

    /**
     * Whether `instanceId` can be used as a queue-record tag. The type token is
     * delimited by the first ':' and split on the first '.', so an id must be
     * non-empty, no longer than MAX_INSTANCE_ID_CHARS-1, and free of ':', '.',
     * ' ' and ']'.
     */
    static bool IsValidInstanceId(const TCHAR* instanceId);

    // Queue management

    /**
     * Queues "<activeInstanceId>.<transactionType>: <data>". A
     * `transactionType` that already carries a '.' is taken to be qualified
     * and is stored unchanged, which is how a caller queues work for a backend
     * other than the active one.
     */
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

    /** Connectivity to the active backend's host. */
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

    /**
     * Entries the last sync could not address, because they are tagged for a
     * backend that is not registered or because their payload is malformed.
     * They stay queued and are deliberately kept out of the success/failure
     * ratio: counting them as failures would pin the status at "failed" for the
     * life of the device and make a working queue look broken, while counting
     * them as successes would discard the operator's work. The queue view shows
     * this separately so the entries can be removed on purpose.
     */
    int GetSkippedCount() const;

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
    /**
     * One cached connectivity answer, keyed by instance id. Two backends can sit
     * on different networks -- NetBox on the warehouse LAN, HomeBox on the
     * internet over GPRS -- so a single cached boolean is wrong for at least one
     * of them, and would report the LAN backend unreachable every time the WAN
     * link is down.
     *
     * The key is a copy rather than the backend's own pointer: a backend may be
     * re-pointed at a different server (and renamed) while the engine is alive,
     * and a stale entry keyed by a pointer would answer for the wrong host.
     */
    struct HostState {
        TCHAR instanceId[MAX_INSTANCE_ID_CHARS];
        DWORD tick;
        bool online;
    };

    InventoryBackend* m_backends[MAX_BACKENDS];
    int m_backendCount;
    int m_activeIndex;          // -1 when nothing is registered

    Journal* m_journal;
    SyncStatus m_syncStatus;
    TCHAR* m_lastSyncError;
    DWORD m_lastSyncTime;
    DWORD m_lastSyncAttemptTick;
    bool m_syncAttempted;
    bool m_autoSyncEnabled;
    int m_autoSyncIntervalSeconds;
    int m_skippedCount;

    // Connectivity is probed with a blocking DNS lookup, which costs seconds on
    // a GPRS link, and Sync()/IsOnline()/the UI status line all ask for it. The
    // answer is cached for a few seconds; these are mutable because the query
    // itself is logically const.
    mutable HostState m_connectivity[MAX_BACKENDS];
    mutable int m_connectivityCount;

    // Helper methods
    bool CheckConnectivity(const InventoryBackend* backend) const;

    /** True when at least one registered backend answers a name lookup. */
    bool AnyBackendOnline() const;

    void InvalidateConnectivity() const;

    /** Routes one queued record to the backend named in its type token. */
    ReplayResult ProcessQueuedTransaction(const TCHAR* transaction);

    void SetLastSyncError(const TCHAR* message);

    // Not copyable: owns the last-error allocation.
    SyncEngine(const SyncEngine&);
    SyncEngine& operator=(const SyncEngine&);
};

} // namespace HBX

#endif // SYNCENGINE_HPP
