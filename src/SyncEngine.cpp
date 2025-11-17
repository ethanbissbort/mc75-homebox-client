#include "../include/SyncEngine.hpp"
#include "../include/Common.hpp"

namespace HBX {

SyncEngine::SyncEngine(HbClient* hbClient, Journal* journal)
    : m_hbClient(hbClient)
    , m_journal(journal)
    , m_syncStatus(SYNC_IDLE)
    , m_lastSyncError(NULL)
    , m_lastSyncTime(0)
    , m_autoSyncEnabled(false)
    , m_lastError(SYNC_ERROR_NONE)
{
}

SyncEngine::~SyncEngine()
{
    if (m_lastSyncError) {
        delete[] m_lastSyncError;
    }
}

bool SyncEngine::QueueTransaction(const TCHAR* transactionType, const TCHAR* data)
{
    if (!transactionType || !data) {
        m_lastError = SYNC_ERROR_INVALID_TRANSACTION;
        return false;
    }

    // Build transaction entry with timestamp
    TCHAR transactionEntry[1024];
    DWORD timestamp = GetTickCount();

    wsprintf(transactionEntry, TEXT("[%lu] %s: %s"), timestamp, transactionType, data);

    // Log to journal (which maintains the queue)
    if (!m_journal->LogTransaction(transactionType, TEXT(""), transactionEntry)) {
        m_lastError = SYNC_ERROR_JOURNAL_ERROR;
        return false;
    }

    m_lastError = SYNC_ERROR_NONE;
    return true;
}

int SyncEngine::GetQueuedTransactionCount() const
{
    return m_journal->GetTransactionCount();
}

bool SyncEngine::ClearQueue()
{
    return m_journal->Clear();
}

bool SyncEngine::Sync()
{
    m_syncStatus = SYNC_IN_PROGRESS;

    // Clear any previous error
    if (m_lastSyncError) {
        delete[] m_lastSyncError;
        m_lastSyncError = NULL;
    }

    // Check if we're online
    if (!CheckConnectivity()) {
        m_syncStatus = SYNC_FAILED;
        m_lastError = SYNC_ERROR_OFFLINE;

        m_lastSyncError = new TCHAR[64];
        lstrcpy(m_lastSyncError, TEXT("No network connectivity"));

        return false;
    }

    // Get pending transactions from journal
    TCHAR** transactions = NULL;
    int count = 0;

    if (!m_journal->GetPendingTransactions(&transactions, &count)) {
        m_syncStatus = SYNC_FAILED;
        m_lastError = SYNC_ERROR_JOURNAL_ERROR;

        m_lastSyncError = new TCHAR[64];
        lstrcpy(m_lastSyncError, TEXT("Failed to retrieve pending transactions"));

        return false;
    }

    // If no transactions, we're done
    if (count == 0) {
        m_syncStatus = SYNC_SUCCESS;
        m_lastSyncTime = GetTickCount();
        m_lastError = SYNC_ERROR_NONE;
        return true;
    }

    // Process each transaction
    int successCount = 0;
    int failCount = 0;

    for (int i = 0; i < count; i++) {
        if (transactions[i]) {
            if (ProcessQueuedTransaction(transactions[i])) {
                successCount++;
                // Mark as synced in journal
                m_journal->MarkTransactionSynced(transactions[i]);
            } else {
                failCount++;
            }

            // Free the transaction string
            delete[] transactions[i];
        }
    }

    // Free the array
    delete[] transactions;

    // Update status
    if (failCount == 0) {
        m_syncStatus = SYNC_SUCCESS;
        m_lastSyncTime = GetTickCount();
        m_lastError = SYNC_ERROR_NONE;
        return true;
    } else if (successCount > 0) {
        // Partial success
        m_syncStatus = SYNC_SUCCESS;
        m_lastSyncTime = GetTickCount();
        m_lastError = SYNC_ERROR_PARTIAL_SYNC;

        TCHAR errorMsg[128];
        wsprintf(errorMsg, TEXT("Synced %d of %d transactions"), successCount, count);
        m_lastSyncError = new TCHAR[lstrlen(errorMsg) + 1];
        lstrcpy(m_lastSyncError, errorMsg);

        return true;
    } else {
        m_syncStatus = SYNC_FAILED;
        m_lastError = SYNC_ERROR_API_ERROR;

        m_lastSyncError = new TCHAR[64];
        lstrcpy(m_lastSyncError, TEXT("All transactions failed to sync"));

        return false;
    }
}

bool SyncEngine::SyncItem(const TCHAR* transactionId)
{
    if (!transactionId || lstrlen(transactionId) == 0) {
        m_lastError = SYNC_ERROR_INVALID_TRANSACTION;
        return false;
    }

    // Check connectivity
    if (!CheckConnectivity()) {
        m_lastError = SYNC_ERROR_OFFLINE;
        return false;
    }

    // Process the single transaction
    if (ProcessQueuedTransaction(transactionId)) {
        // Mark as synced
        m_journal->MarkTransactionSynced(transactionId);
        m_lastError = SYNC_ERROR_NONE;
        return true;
    }

    m_lastError = SYNC_ERROR_API_ERROR;
    return false;
}

bool SyncEngine::IsOnline() const
{
    return CheckConnectivity();
}

SyncEngine::SyncStatus SyncEngine::GetSyncStatus() const
{
    return m_syncStatus;
}

const TCHAR* SyncEngine::GetLastSyncError() const
{
    return m_lastSyncError;
}

DWORD SyncEngine::GetLastSyncTime() const
{
    return m_lastSyncTime;
}

void SyncEngine::SetAutoSyncEnabled(bool enabled)
{
    m_autoSyncEnabled = enabled;
}

bool SyncEngine::IsAutoSyncEnabled() const
{
    return m_autoSyncEnabled;
}

SyncError SyncEngine::GetLastError() const
{
    return m_lastError;
}

bool SyncEngine::CheckConnectivity() const
{
    if (!m_hbClient) {
        return false;
    }

    // Try a simple HTTP connection to test connectivity
    // We'll try to resolve the hostname from the base URL
    const TCHAR* baseUrl = m_hbClient->GetBaseUrl();
    if (!baseUrl || lstrlen(baseUrl) == 0) {
        return false;
    }

    // Extract host from URL
    TCHAR host[256];
    const TCHAR* start = baseUrl;

    // Skip protocol
    if (wcsncmp(baseUrl, TEXT("http://"), 7) == 0) {
        start = baseUrl + 7;
    } else if (wcsncmp(baseUrl, TEXT("https://"), 8) == 0) {
        start = baseUrl + 8;
    }

    // Copy host (up to first slash or colon)
    int i = 0;
    while (start[i] && start[i] != '/' && start[i] != ':' && i < 255) {
        host[i] = start[i];
        i++;
    }
    host[i] = '\0';

    // Try to resolve the host
    char asciiHost[256];
    Common::TCharToChar(host, asciiHost, 256);

    // Initialize WinSock if needed
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    struct hostent* hostInfo = gethostbyname(asciiHost);

    WSACleanup();

    return (hostInfo != NULL);
}

bool SyncEngine::ProcessQueuedTransaction(const TCHAR* transaction)
{
    if (!transaction || !m_hbClient) {
        return false;
    }

    // Parse transaction format: "[timestamp] TYPE: DATA"
    // Example: "[12345] ITEM_SCAN: SCAN:123456789"

    // Find the type separator
    const TCHAR* typeStart = wcschr(transaction, ']');
    if (!typeStart) {
        return false;
    }
    typeStart += 2; // Skip "] "

    const TCHAR* dataStart = wcschr(typeStart, ':');
    if (!dataStart) {
        return false;
    }
    dataStart += 2; // Skip ": "

    // Extract transaction type
    int typeLen = (int)(dataStart - typeStart - 2);
    TCHAR transactionType[64];
    if (typeLen >= 64) typeLen = 63;
    wcsncpy(transactionType, typeStart, typeLen);
    transactionType[typeLen] = '\0';

    // Process based on transaction type
    if (wcscmp(transactionType, TEXT("ITEM_SCAN")) == 0) {
        // Parse SCAN:barcode format
        if (wcsncmp(dataStart, TEXT("SCAN:"), 5) == 0) {
            const TCHAR* barcode = dataStart + 5;

            // Try to sync this scan with the server
            // For now, we'll just attempt to get the item to verify connectivity
            Models::Item item;
            if (m_hbClient->GetItem(barcode, &item)) {
                // Successfully contacted server and retrieved item
                return true;
            }
        }
    } else if (wcscmp(transactionType, TEXT("ITEM_UPDATE")) == 0) {
        // Future: Handle item updates
        // For now, just return true to clear from queue
        return true;
    }

    // Unknown or unsupported transaction type
    return false;
}

} // namespace HBX
