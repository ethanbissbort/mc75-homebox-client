#ifndef JOURNAL_HPP
#define JOURNAL_HPP

#include <windows.h>

namespace HBX {

/**
 * Append-only transaction journal: audit trail plus the durable offline queue.
 *
 * On-disk format (UTF-8, one CRLF-terminated record per line):
 *
 *   [2026-07-25 09:14:02] INFO: application started
 *   [2026-07-25 09:14:07] ERROR: HTTP_500: server rejected update
 *   [2026-07-25 09:14:09] AUDIT: SCAN 123456789: Barcode scanned
 *   [2026-07-25 09:14:09] TRANS 00000042: [51234] ITEM_SCAN: SCAN:123456789
 *   [2026-07-25 09:15:11] SYNCED 00000042
 *
 * Two properties of that format matter:
 *
 * 1. Only TRANS records are queue entries. AUDIT/INFO/ERROR records are pure
 *    logging and never become work for the SyncEngine. The previous format used
 *    the same "TRANS" label for both, so every logged scan turned into a queue
 *    entry that could never be parsed or replayed and permanently degraded the
 *    sync status.
 *
 * 2. A SYNCED marker names the *sequence number* of the record it acknowledges,
 *    not its text. Correlating by sequence is exact and bounded: the previous
 *    scheme embedded the full original line in the marker, which both overflowed
 *    a fixed 512-character buffer and mis-classified any payload that happened
 *    to contain the word "SYNCED".
 *
 * Sequence numbers are recovered by scanning the file on Initialize(), so a
 * queue survives an app restart (a routine event on the MC75 -- a battery swap
 * ends the process). All public methods are serialised with a critical section
 * because the scanner thread and the UI thread both journal.
 */
class Journal {
public:
    Journal();
    ~Journal();

    /**
     * Opens (creating if needed) the journal at `journalPath` and rebuilds the
     * pending-transaction state from its contents. Safe to call more than once;
     * a previously open handle is closed first.
     */
    bool Initialize(const TCHAR* journalPath);

    // -----------------------------------------------------------------------
    // Logging (audit trail only -- none of these create queue work)
    // -----------------------------------------------------------------------
    bool LogTransaction(const TCHAR* transactionType, const TCHAR* itemId, const TCHAR* details);
    bool LogError(const TCHAR* errorCode, const TCHAR* errorMessage);
    bool LogInfo(const TCHAR* message);

    // -----------------------------------------------------------------------
    // Durable offline queue
    // -----------------------------------------------------------------------

    /**
     * Appends `payload` as a pending transaction. On success `*outSequence`,
     * when non-NULL, receives the assigned sequence number.
     */
    bool QueueTransaction(const TCHAR* payload, DWORD* outSequence);

    /**
     * Returns every pending (queued but not yet acknowledged) transaction as
     * full record lines, oldest first. The caller owns both the array and each
     * string and must delete[] each entry and then the array.
     */
    bool GetPendingTransactions(TCHAR*** transactions, int* count);

    /**
     * Acknowledges a transaction previously handed out by
     * GetPendingTransactions. Accepts the full record line; the sequence number
     * is parsed out of it.
     */
    bool MarkTransactionSynced(const TCHAR* transactionLine);

    /** Acknowledges by sequence number directly. */
    bool MarkSequenceSynced(DWORD sequence);

    /** Number of transactions queued and not yet acknowledged. */
    int GetTransactionCount() const;

    /**
     * Extracts the sequence number from a record line produced by
     * GetPendingTransactions. Returns false if the line is not a TRANS record.
     */
    static bool ParseSequence(const TCHAR* line, DWORD* sequence);

    /**
     * Returns a pointer into `line` at the queued payload (past
     * "[timestamp] TRANS nnnnnnnn: "), or NULL if `line` is not a TRANS record.
     */
    static const TCHAR* PayloadOf(const TCHAR* line);

    // -----------------------------------------------------------------------
    // Maintenance
    // -----------------------------------------------------------------------

    /**
     * Rewrites the file keeping only unacknowledged transactions and the most
     * recent error records, dropping acknowledged transactions and their
     * markers. Sequence numbering continues from where it left off.
     */
    bool Compact();

    /** Discards the entire journal, including any pending transactions. */
    bool Clear();

    /**
     * Size at which an append triggers an automatic Compact(). The MC75 has a
     * small persistent store and the journal previously grew without bound.
     * Default 256 KB; 0 disables automatic compaction.
     */
    void SetMaxFileBytes(DWORD maxBytes);

    /** Maximum ERROR records retained by Compact(). Default 200. */
    void SetMaxRetainedErrors(int maxErrors);

private:
    HANDLE m_fileHandle;
    TCHAR* m_journalPath;
    DWORD m_transactionCount;
    DWORD m_nextSequence;
    DWORD m_syncedMarkerCount;
    DWORD m_maxFileBytes;
    int m_maxRetainedErrors;

    // Serialises every public entry point: the EMDK scanner thread and the UI
    // thread share one Journal and one file pointer.
    mutable CRITICAL_SECTION m_lock;

    // All of these assume the lock is already held.
    bool WriteEntryLocked(const TCHAR* label, const TCHAR* message);
    bool RebuildStateLocked();
    bool CompactLocked();
    bool ReadAllLocked(char** contentOut, DWORD* sizeOut);
    void MaybeCompactLocked();
    bool OpenFileLocked(DWORD disposition);
    void CloseFileLocked();

    static void FormatTimestamp(TCHAR* buffer, int cap);

    // Not copyable: owns a file handle, an allocation and a lock.
    Journal(const Journal&);
    Journal& operator=(const Journal&);
};

} // namespace HBX

#endif // JOURNAL_HPP
