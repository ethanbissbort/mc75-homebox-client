#ifndef JOURNAL_HPP
#define JOURNAL_HPP

#include <windows.h>

namespace HBX {

/**
 * Transaction journal for audit trail and recovery
 * Logs all operations to persistent storage
 */
class Journal {
public:
    Journal();
    ~Journal();

    // Initialize journal with storage path
    bool Initialize(const TCHAR* journalPath);

    // Logging operations
    bool LogTransaction(const TCHAR* transactionType, const TCHAR* itemId, const TCHAR* details);
    bool LogError(const TCHAR* errorCode, const TCHAR* errorMessage);
    bool LogInfo(const TCHAR* message);

    // Query operations
    bool GetPendingTransactions(TCHAR*** transactions, int* count);
    bool MarkTransactionSynced(const TCHAR* transactionId);
    int GetTransactionCount() const;

    // Maintenance
    bool Compact();
    bool Clear();

private:
    HANDLE m_fileHandle;
    TCHAR* m_journalPath;
    DWORD m_transactionCount;

    // Helper methods
    bool WriteEntry(const TCHAR* level, const TCHAR* message);
    TCHAR* FormatTimestamp();
    bool FlushToDisk();
};

} // namespace HBX

#endif // JOURNAL_HPP
