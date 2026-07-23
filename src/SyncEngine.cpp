#include "../include/SyncEngine.hpp"

namespace HBX {

SyncEngine::SyncEngine(HbClient* hbClient, Journal* journal)
    : m_hbClient(hbClient)
    , m_journal(journal)
    , m_syncStatus(SYNC_IDLE)
    , m_lastSyncError(NULL)
    , m_lastSyncTime(0)
    , m_autoSyncEnabled(false)
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
        return false;
    }

    // Build transaction entry with timestamp
    TCHAR transactionEntry[1024];
    DWORD timestamp = GetTickCount();

    wsprintf(transactionEntry, TEXT("[%lu] %s: %s"), timestamp, transactionType, data);

    // Log to journal (which maintains the queue)
    return m_journal->LogTransaction(transactionType, TEXT(""), transactionEntry);
}

int SyncEngine::GetQueuedTransactionCount() const
{
    return m_journal->GetTransactionCount();
}

bool SyncEngine::GetQueuedTransactions(TCHAR*** transactions, int* count) const
{
    if (!transactions || !count) {
        return false;
    }

    // Default to an empty result so callers can safely inspect the outputs
    // even on early failure.
    *transactions = NULL;
    *count = 0;

    if (!m_journal) {
        return false;
    }

    // Delegate to the journal, which owns the persisted queue. Note that
    // GetPendingTransactions is non-const, but m_journal is a plain pointer
    // (Journal* const inside this const method), so calling a non-const method
    // through it is well-formed. The journal allocates the TCHAR*[] and each
    // TCHAR* entry; ownership passes to the caller as documented in the header.
    return m_journal->GetPendingTransactions(transactions, count);
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
        m_syncStatus = SYNC_OFFLINE;

        m_lastSyncError = new TCHAR[64];
        lstrcpy(m_lastSyncError, TEXT("No network connectivity"));

        return false;
    }

    // Get pending transactions from journal
    TCHAR** transactions = NULL;
    int count = 0;

    if (!m_journal->GetPendingTransactions(&transactions, &count)) {
        m_syncStatus = SYNC_FAILED;

        m_lastSyncError = new TCHAR[64];
        lstrcpy(m_lastSyncError, TEXT("Failed to retrieve pending transactions"));

        return false;
    }

    // If no transactions, we're done
    if (count == 0) {
        m_syncStatus = SYNC_SUCCESS;
        m_lastSyncTime = GetTickCount();
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
        return true;
    } else if (successCount > 0) {
        // Partial success
        m_syncStatus = SYNC_PARTIAL;
        m_lastSyncTime = GetTickCount();

        TCHAR errorMsg[128];
        wsprintf(errorMsg, TEXT("Synced %d of %d transactions"), successCount, count);
        m_lastSyncError = new TCHAR[lstrlen(errorMsg) + 1];
        lstrcpy(m_lastSyncError, errorMsg);

        return true;
    } else {
        m_syncStatus = SYNC_FAILED;

        m_lastSyncError = new TCHAR[64];
        lstrcpy(m_lastSyncError, TEXT("All transactions failed to sync"));

        return false;
    }
}

bool SyncEngine::SyncItem(const TCHAR* transactionId)
{
    if (!transactionId || lstrlen(transactionId) == 0) {
        return false;
    }

    // Check connectivity
    if (!CheckConnectivity()) {
        return false;
    }

    // Process the single transaction
    if (ProcessQueuedTransaction(transactionId)) {
        // Mark as synced
        m_journal->MarkTransactionSynced(transactionId);
        return true;
    }

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
    for (int j = 0; j < 255 && host[j] != '\0'; j++) {
        asciiHost[j] = (char)host[j];
    }
    asciiHost[255] = '\0';

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

    // A queued entry has the shape "[timestamp] TYPE: DATA" as built by
    // QueueTransaction, but the Journal wraps that a second time when it
    // persists the line, so what we usually receive here is:
    //
    //   "[2026-07-22 12:00:00] TRANS: [12345] ITEM_SCAN: SCAN:123456789"
    //    \___ journal timestamp _/ \__/  \___ inner payload built here ___/
    //                             wrapper label
    //
    // Parse defensively by drilling through each "[...] LABEL: REST" layer.
    // A layer whose REST starts with '[' is a wrapper, so we descend into it;
    // otherwise REST is the real DATA and LABEL is the real transaction TYPE.
    // This also handles the un-wrapped single-layer form gracefully.
    TCHAR transactionType[64];
    transactionType[0] = '\0';
    const TCHAR* dataStart = NULL;

    const TCHAR* cursor = transaction;
    for (int guard = 0; guard < 8; guard++) {
        // Locate the "] " that closes this layer's bracketed prefix.
        const TCHAR* bracketEnd = wcschr(cursor, ']');
        if (!bracketEnd) {
            break;
        }

        const TCHAR* labelStart = bracketEnd + 1;
        while (*labelStart == ' ') {
            labelStart++;
        }

        // Locate the ':' that separates the label from the remainder.
        const TCHAR* colon = wcschr(labelStart, ':');
        if (!colon) {
            break;
        }

        // Copy out the label as the (current best guess of the) type.
        int typeLen = (int)(colon - labelStart);
        if (typeLen < 0) {
            typeLen = 0;
        }
        if (typeLen > 63) {
            typeLen = 63;
        }
        for (int i = 0; i < typeLen; i++) {
            transactionType[i] = labelStart[i];
        }
        transactionType[typeLen] = '\0';

        const TCHAR* rest = colon + 1;
        while (*rest == ' ') {
            rest++;
        }

        if (*rest == '[') {
            // Another wrapper layer; drill deeper.
            cursor = rest;
            continue;
        }

        // Innermost layer reached: rest is the real DATA.
        dataStart = rest;
        break;
    }

    if (!dataStart) {
        return false;
    }

    // Process based on transaction type
    if (wcscmp(transactionType, TEXT("ITEM_SCAN")) == 0) {
        // DATA format: "SCAN:<barcode>" or "SCAN:<barcode>@<locationId>".
        if (wcsncmp(dataStart, TEXT("SCAN:"), 5) != 0) {
            return false;
        }

        const TCHAR* payload = dataStart + 5;

        // Split the payload into <barcode> and optional <locationId> at '@'.
        const TCHAR* at = wcschr(payload, '@');
        int barcodeLen = at ? (int)(at - payload) : lstrlen(payload);
        if (barcodeLen <= 0) {
            return false; // no barcode -> nothing to sync
        }
        if (barcodeLen > 255) {
            barcodeLen = 255;
        }

        TCHAR barcode[256];
        for (int i = 0; i < barcodeLen; i++) {
            barcode[i] = payload[i];
        }
        barcode[barcodeLen] = '\0';

        TCHAR locationId[256];
        locationId[0] = '\0';
        if (at) {
            const TCHAR* loc = at + 1;
            int locLen = lstrlen(loc);
            if (locLen > 255) {
                locLen = 255;
            }
            for (int i = 0; i < locLen; i++) {
                locationId[i] = loc[i];
            }
            locationId[locLen] = '\0';
        }

        // Look the item up on the server. Only a real success clears the entry
        // from the queue; any failure returns false so it stays queued and is
        // retried on the next Sync().
        Models::Item item;
        if (!m_hbClient->GetItem(barcode, &item)) {
            return false;
        }

        // If a location was supplied with the scan, push the move too.
        if (locationId[0] != '\0') {
            if (!m_hbClient->UpdateItemLocation(barcode, locationId)) {
                return false;
            }
        }

        return true;
    } else if (wcscmp(transactionType, TEXT("ITEM_UPDATE")) == 0) {
        // DATA format: "UPDATE:<json>" where <json> is a complete Item JSON
        // object, e.g.
        //   UPDATE:{"id":"42","barcode":"123","name":"Widget","quantity":3}
        // The JSON is parsed with Models::Item::FromJson and pushed with
        // HbClient::UpdateItem; its result is returned so a failed update stays
        // queued for retry.
        if (wcsncmp(dataStart, TEXT("UPDATE:"), 7) != 0) {
            return false;
        }

        const TCHAR* json = dataStart + 7;
        if (*json == '\0') {
            return false;
        }

        Models::Item item;
        if (!item.FromJson(json)) {
            return false;
        }

        return m_hbClient->UpdateItem(&item);
    }

    // Unknown or unsupported transaction type
    return false;
}

} // namespace HBX
