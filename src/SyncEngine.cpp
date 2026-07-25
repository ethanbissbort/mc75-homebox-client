#include "../include/SyncEngine.hpp"
#include "../include/StrUtil.hpp"

namespace HBX {

namespace {

// How long a connectivity probe stays valid. Long enough that a burst of
// IsOnline() calls (scan -> queue -> UI refresh) costs one DNS lookup, short
// enough that walking out of coverage is noticed on the next user action.
const DWORD kConnectivityCacheMs = 5000;

// Below this the UI timer would effectively sync continuously.
const int kMinAutoSyncSeconds = 5;

// Longest transaction type we dispatch on; anything longer cannot match.
const int kMaxTypeChars = 64;

/**
 * Appends a tick count as unsigned decimal. Str::Buffer::AppendInt takes a
 * signed long, which would render the top half of the 32-bit tick range
 * negative once the device has been up for ~25 days.
 */
void AppendTick(Str::Buffer* out, DWORD tick)
{
    TCHAR digits[16];
    int n = 0;

    if (tick == 0) {
        digits[n++] = (TCHAR)'0';
    }
    while (tick > 0 && n < 15) {
        digits[n++] = (TCHAR)('0' + (int)(tick % 10));
        tick /= 10;
    }
    while (n > 0) {
        out->AppendChar(digits[--n]);
    }
}

/** Skips spaces. */
const TCHAR* SkipSpaces(const TCHAR* s)
{
    while (*s == (TCHAR)' ') {
        s++;
    }
    return s;
}

} // namespace

SyncEngine::SyncEngine(HbClient* hbClient, Journal* journal)
    : m_hbClient(hbClient)
    , m_journal(journal)
    , m_syncStatus(SYNC_IDLE)
    , m_lastSyncError(NULL)
    , m_lastSyncTime(0)
    , m_lastSyncAttemptTick(GetTickCount())
    , m_syncAttempted(false)
    , m_autoSyncEnabled(false)
    , m_autoSyncIntervalSeconds(300)
    , m_connectivityTick(0)
    , m_connectivityValid(false)
    , m_connectivityOnline(false)
{
}

SyncEngine::~SyncEngine()
{
    if (m_lastSyncError) {
        delete[] m_lastSyncError;
    }
}

void SyncEngine::SetLastSyncError(const TCHAR* message)
{
    if (m_lastSyncError) {
        delete[] m_lastSyncError;
        m_lastSyncError = NULL;
    }
    if (message) {
        m_lastSyncError = Str::Dup(message);
    }
}

bool SyncEngine::QueueTransaction(const TCHAR* transactionType, const TCHAR* data)
{
    if (!transactionType || !data || !m_journal) {
        return false;
    }

    // The payload is caller-sized - an ITEM_UPDATE carries a whole serialised
    // item - so it cannot be formatted into a fixed buffer: wsprintf stops at
    // 1024 characters on the device and would silently truncate the JSON,
    // leaving an entry that can never be replayed.
    Str::Buffer entry;
    entry.AppendChar((TCHAR)'[');
    AppendTick(&entry, GetTickCount());
    entry.Append(TEXT("] "));
    entry.Append(transactionType);
    entry.Append(TEXT(": "));
    entry.Append(data);

    if (entry.Failed()) {
        return false;
    }

    return m_journal->QueueTransaction(entry.Get(), NULL);
}

bool SyncEngine::QueueScan(const TCHAR* barcode, const TCHAR* locationId)
{
    if (!barcode || barcode[0] == (TCHAR)'\0') {
        return false;
    }

    Str::Buffer payload;

    if (locationId && locationId[0] != (TCHAR)'\0') {
        // Length-prefixed: a barcode may contain any printable character, so no
        // separator between barcode and location id would be safe.
        payload.Append(TEXT("SCANLOC:"));
        payload.AppendInt(Str::Length(barcode));
        payload.AppendChar((TCHAR)':');
        payload.Append(barcode);
        payload.Append(locationId);
    } else {
        payload.Append(TEXT("SCAN:"));
        payload.Append(barcode);
    }

    if (payload.Failed()) {
        return false;
    }

    return QueueTransaction(TEXT("ITEM_SCAN"), payload.Get());
}

int SyncEngine::GetQueuedTransactionCount() const
{
    if (!m_journal) {
        return 0;
    }
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

bool SyncEngine::RemoveQueuedTransaction(const TCHAR* transactionLine)
{
    if (!transactionLine || !m_journal) {
        return false;
    }

    DWORD sequence = 0;
    if (!Journal::ParseSequence(transactionLine, &sequence)) {
        return false;
    }

    // The journal is append-only and has no "delete one record" primitive:
    // acknowledging the sequence takes the entry out of the pending set, and
    // the record itself disappears at the next Compact().
    return m_journal->MarkSequenceSynced(sequence);
}

bool SyncEngine::ClearQueue()
{
    if (!m_journal) {
        return false;
    }
    return m_journal->Clear();
}

bool SyncEngine::Sync()
{
    m_syncStatus = SYNC_IN_PROGRESS;
    m_lastSyncAttemptTick = GetTickCount();
    m_syncAttempted = true;

    SetLastSyncError(NULL);

    if (!m_journal) {
        m_syncStatus = SYNC_FAILED;
        SetLastSyncError(TEXT("No journal"));
        return false;
    }

    // Check if we're online
    if (!CheckConnectivity()) {
        m_syncStatus = SYNC_OFFLINE;
        SetLastSyncError(TEXT("No network connectivity"));
        return false;
    }

    // Get pending transactions from journal
    TCHAR** transactions = NULL;
    int count = 0;

    if (!m_journal->GetPendingTransactions(&transactions, &count)) {
        // The journal only reports failure before it allocates, but free
        // defensively so no path can leak the array.
        delete[] transactions;

        m_syncStatus = SYNC_FAILED;
        SetLastSyncError(TEXT("Failed to retrieve pending transactions"));
        return false;
    }

    // If no transactions, we're done. The journal still hands back an array
    // when nothing matched, so it has to be released here too.
    if (count == 0) {
        delete[] transactions;

        m_syncStatus = SYNC_SUCCESS;
        m_lastSyncTime = GetTickCount();
        return true;
    }

    // Process each transaction
    int successCount = 0;
    int failCount = 0;

    for (int i = 0; i < count; i++) {
        if (!transactions[i]) {
            continue;
        }

        if (ProcessQueuedTransaction(transactions[i])) {
            successCount++;
            // Mark as synced in journal
            m_journal->MarkTransactionSynced(transactions[i]);
        } else {
            // Left queued so the next Sync() retries it.
            failCount++;
        }

        // Free the transaction string
        delete[] transactions[i];
    }

    // Free the array
    delete[] transactions;
    transactions = NULL;

    // Update status
    if (failCount == 0) {
        m_syncStatus = SYNC_SUCCESS;
        m_lastSyncTime = GetTickCount();
        return true;
    } else if (successCount > 0) {
        // Partial success
        m_syncStatus = SYNC_PARTIAL;
        m_lastSyncTime = GetTickCount();

        TCHAR errorMsg[96];
        errorMsg[0] = (TCHAR)'\0';
        Str::Append(errorMsg, 96, TEXT("Synced "));
        Str::AppendInt(errorMsg, 96, successCount);
        Str::Append(errorMsg, 96, TEXT(" of "));
        Str::AppendInt(errorMsg, 96, count);
        Str::Append(errorMsg, 96, TEXT(" transactions"));
        SetLastSyncError(errorMsg);

        return true;
    }

    m_syncStatus = SYNC_FAILED;
    SetLastSyncError(TEXT("All transactions failed to sync"));

    // Nothing got through: the most likely explanation is that coverage was
    // lost since the probe, so drop the cached answer instead of reporting
    // "online" for the rest of the cache window.
    m_connectivityValid = false;

    return false;
}

bool SyncEngine::SyncItem(const TCHAR* transactionLine)
{
    if (!transactionLine || Str::Length(transactionLine) == 0 || !m_journal) {
        return false;
    }

    // Check connectivity
    if (!CheckConnectivity()) {
        return false;
    }

    // Process the single transaction
    if (ProcessQueuedTransaction(transactionLine)) {
        // Mark as synced
        m_journal->MarkTransactionSynced(transactionLine);
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

void SyncEngine::SetAutoSyncIntervalSeconds(int seconds)
{
    if (seconds < kMinAutoSyncSeconds) {
        seconds = kMinAutoSyncSeconds;
    }
    m_autoSyncIntervalSeconds = seconds;
}

int SyncEngine::GetAutoSyncIntervalSeconds() const
{
    return m_autoSyncIntervalSeconds;
}

bool SyncEngine::ShouldAutoSync(DWORD nowTick) const
{
    if (!m_autoSyncEnabled || m_syncStatus == SYNC_IN_PROGRESS) {
        return false;
    }

    if (GetQueuedTransactionCount() <= 0) {
        return false;
    }

    // Work queued but nothing attempted yet means the app just started with a
    // queue recovered from disk -- typically a battery swap mid-shift. Waiting a
    // full interval there strands exactly the transactions the queue exists to
    // protect, so the first poll is always due. There is no risk of a sync storm
    // because a completed attempt (successful or not) starts the interval.
    if (!m_syncAttempted) {
        return true;
    }

    // Unsigned subtraction so the 49-day GetTickCount wrap does not park the
    // engine for another 49 days.
    DWORD elapsed = nowTick - m_lastSyncAttemptTick;
    return elapsed >= (DWORD)m_autoSyncIntervalSeconds * 1000;
}

bool SyncEngine::CheckConnectivity() const
{
    if (!m_hbClient) {
        return false;
    }

    // Sync(), IsOnline() and the UI status line all land here, and every miss
    // costs a synchronous gethostbyname - several seconds on GPRS, on whichever
    // thread asked. A few seconds of staleness is cheaper than that.
    DWORD now = GetTickCount();
    if (m_connectivityValid && (DWORD)(now - m_connectivityTick) < kConnectivityCacheMs) {
        return m_connectivityOnline;
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
    while (i < 255 && start[i] && start[i] != (TCHAR)'/' && start[i] != (TCHAR)':') {
        host[i] = start[i];
        i++;
    }
    host[i] = (TCHAR)'\0';

    if (host[0] == (TCHAR)'\0') {
        return false;
    }

    // Encode for the resolver. A hand-rolled (char)tchar loop used to leave the
    // bytes past the host name uninitialised, so gethostbyname was handed
    // "myserver<stack garbage>".
    char asciiHost[256];
    if (!Str::ToUtf8(asciiHost, (int)sizeof(asciiHost), host)) {
        return false;
    }

    // HttpClient initialises WinSock for the life of the process, but this call
    // can outlive any particular request, so take our own reference - and only
    // release it if we actually got one. An unconditional WSACleanup after a
    // failed WSAStartup releases somebody else's reference and can tear WinSock
    // down while a request is in flight.
    WSADATA wsaData;
    bool winsockStarted = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);

    struct hostent* hostInfo = gethostbyname(asciiHost);

    if (winsockStarted) {
        WSACleanup();
    }

    m_connectivityOnline = (hostInfo != NULL);
    m_connectivityTick = now;
    m_connectivityValid = true;

    return m_connectivityOnline;
}

bool SyncEngine::ProcessQueuedTransaction(const TCHAR* transaction)
{
    if (!transaction || !m_hbClient) {
        return false;
    }

    // GetQueuedTransactions hands back whole journal record lines:
    //
    //   "[2026-07-25 09:14:09] TRANS 00000042: [51234] ITEM_SCAN: SCAN:12345"
    //    \___ journal record header ________/ \___ QueueTransaction payload _/
    //
    // PayloadOf strips the record header. A caller may also pass the payload on
    // its own, which is accepted as-is; anything else is left queued rather
    // than reported as a sync that never happened.
    const TCHAR* payload = Journal::PayloadOf(transaction);
    if (!payload) {
        payload = transaction;
    }

    if (*payload != (TCHAR)'[') {
        return false;
    }

    // Payload shape: "[tick] TYPE: DATA".
    const TCHAR* bracketEnd = wcschr(payload, (TCHAR)']');
    if (!bracketEnd) {
        return false;
    }

    const TCHAR* typeStart = SkipSpaces(bracketEnd + 1);
    const TCHAR* colon = wcschr(typeStart, (TCHAR)':');
    if (!colon) {
        return false;
    }

    TCHAR transactionType[kMaxTypeChars];
    if (!Str::CopyN(transactionType, kMaxTypeChars, typeStart, (int)(colon - typeStart))) {
        // Too long to be a type we handle.
        return false;
    }

    const TCHAR* data = SkipSpaces(colon + 1);

    if (wcscmp(transactionType, TEXT("ITEM_SCAN")) == 0) {
        // DATA is "SCAN:<barcode>" or "SCANLOC:<len>:<barcode><locationId>".
        const TCHAR* barcode = NULL;
        const TCHAR* locationId = NULL;
        TCHAR* barcodeCopy = NULL; // only the length-prefixed form needs one

        if (wcsncmp(data, TEXT("SCANLOC:"), 8) == 0) {
            const TCHAR* lenStart = data + 8;
            const TCHAR* lenEnd = wcschr(lenStart, (TCHAR)':');
            if (!lenEnd) {
                return false;
            }

            TCHAR lenText[12];
            int barcodeLen = 0;
            if (!Str::CopyN(lenText, 12, lenStart, (int)(lenEnd - lenStart)) ||
                !Str::ParseInt(lenText, &barcodeLen)) {
                return false;
            }

            const TCHAR* body = lenEnd + 1;
            if (barcodeLen <= 0 || barcodeLen > Str::Length(body)) {
                return false;
            }

            barcodeCopy = Str::DupN(body, barcodeLen);
            if (!barcodeCopy) {
                return false;
            }
            barcode = barcodeCopy;
            locationId = body + barcodeLen;
        } else if (wcsncmp(data, TEXT("SCAN:"), 5) == 0) {
            barcode = data + 5;
        } else {
            return false;
        }

        if (barcode[0] == (TCHAR)'\0') {
            delete[] barcodeCopy;
            return false;
        }

        // Only a real success clears the entry from the queue; any failure
        // returns false so it stays queued and is retried on the next Sync().
        Models::Item item;
        bool ok = m_hbClient->GetItem(barcode, &item);

        // If a location was captured with the scan, push the move too.
        if (ok && locationId && locationId[0] != (TCHAR)'\0') {
            ok = m_hbClient->UpdateItemLocation(barcode, locationId);
        }

        delete[] barcodeCopy;
        return ok;
    } else if (wcscmp(transactionType, TEXT("ITEM_UPDATE")) == 0) {
        // DATA format: "UPDATE:<json>" where <json> is a complete Item JSON
        // object, e.g.
        //   UPDATE:{"id":"42","barcode":"123","name":"Widget","quantity":3}
        // The JSON is parsed with Models::Item::FromJson and pushed with
        // HbClient::UpdateItem; its result is returned so a failed update stays
        // queued for retry.
        if (wcsncmp(data, TEXT("UPDATE:"), 7) != 0) {
            return false;
        }

        const TCHAR* json = data + 7;
        if (*json == (TCHAR)'\0') {
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
