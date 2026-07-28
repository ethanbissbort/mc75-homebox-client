#include "../include/SyncEngine.hpp"
#include "../include/StrUtil.hpp"

// The connectivity probe resolves a host name directly rather than through the
// HTTP transport: a name lookup costs one round trip where a request costs the
// whole timeout, and the engine asks this question far more often than it
// sends anything.
#include <winsock.h>

namespace HBX {

namespace {

// How long a connectivity probe stays valid. Long enough that a burst of
// IsOnline() calls (scan -> queue -> UI refresh) costs one DNS lookup, short
// enough that walking out of coverage is noticed on the next user action.
const DWORD kConnectivityCacheMs = 5000;

// Below this the UI timer would effectively sync continuously.
const int kMinAutoSyncSeconds = 5;

// Longest transaction type we dispatch on, with the backend tag already
// stripped; anything longer cannot match.
const int kMaxTypeChars = 64;

// The whole token as it appears in the record: "<instanceId>.<type>".
const int kMaxTokenChars = SyncEngine::MAX_INSTANCE_ID_CHARS + kMaxTypeChars;

// Records written before queue entries carried a backend tag belong to
// HomeBox: it was the only backend that existed. Routing them by kind rather
// than by a fixed id means a site that renamed its HomeBox instance still
// drains the queue its devices were carrying at upgrade time.
const TCHAR* const kLegacyKind = TEXT("hb");

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

/** One queued record, split into the parts replay routes on. */
struct ParsedRecord {
    TCHAR instanceId[SyncEngine::MAX_INSTANCE_ID_CHARS]; // empty for a legacy record
    TCHAR type[kMaxTypeChars];
    const TCHAR* data;                                   // points into the record line
};

/**
 * Splits "[tick] <instanceId>.<TYPE>: DATA" out of a journal record line.
 * The instance id is optional; without it the record predates backend tagging.
 */
bool ParseQueuedPayload(const TCHAR* transaction, ParsedRecord* out)
{
    // GetQueuedTransactions hands back whole journal record lines:
    //
    //   "[2026-07-25 09:14:09] TRANS 00000042: [51234] hb.ITEM_SCAN: SCAN:12345"
    //    \___ journal record header ________/ \___ QueueTransaction payload ___/
    //
    // PayloadOf strips the record header. A caller may also pass the payload on
    // its own, which is accepted as-is.
    const TCHAR* payload = Journal::PayloadOf(transaction);
    if (!payload) {
        payload = transaction;
    }

    if (*payload != (TCHAR)'[') {
        return false;
    }

    const TCHAR* bracketEnd = wcschr(payload, (TCHAR)']');
    if (!bracketEnd) {
        return false;
    }

    const TCHAR* typeStart = SkipSpaces(bracketEnd + 1);
    const TCHAR* colon = wcschr(typeStart, (TCHAR)':');
    if (!colon) {
        return false;
    }

    TCHAR token[kMaxTokenChars];
    if (!Str::CopyN(token, kMaxTokenChars, typeStart, (int)(colon - typeStart))) {
        // Too long to be anything we wrote.
        return false;
    }

    out->instanceId[0] = (TCHAR)'\0';

    // Split on the first '.': an instance id may not contain one (see
    // SyncEngine::IsValidInstanceId), so the first is always the separator.
    const TCHAR* dot = wcschr(token, (TCHAR)'.');
    if (dot) {
        if (!Str::CopyN(out->instanceId, SyncEngine::MAX_INSTANCE_ID_CHARS,
                        token, (int)(dot - token)) ||
            !Str::Copy(out->type, kMaxTypeChars, dot + 1)) {
            return false;
        }
    } else if (!Str::Copy(out->type, kMaxTypeChars, token)) {
        return false;
    }

    if (out->type[0] == (TCHAR)'\0') {
        return false;
    }

    out->data = SkipSpaces(colon + 1);
    return true;
}

/** Copies the host out of "scheme://host[:port][/path]". */
bool HostFromUrl(TCHAR* host, int cap, const TCHAR* baseUrl)
{
    if (!baseUrl || lstrlen(baseUrl) == 0) {
        return false;
    }

    const TCHAR* start = baseUrl;
    if (wcsncmp(baseUrl, TEXT("http://"), 7) == 0) {
        start = baseUrl + 7;
    } else if (wcsncmp(baseUrl, TEXT("https://"), 8) == 0) {
        start = baseUrl + 8;
    }

    int i = 0;
    while (i < cap - 1 && start[i] && start[i] != (TCHAR)'/' && start[i] != (TCHAR)':') {
        host[i] = start[i];
        i++;
    }
    host[i] = (TCHAR)'\0';

    return host[0] != (TCHAR)'\0';
}

} // namespace

SyncEngine::SyncEngine(InventoryBackend* backend, Journal* journal)
    : m_backendCount(0)
    , m_activeIndex(-1)
    , m_journal(journal)
    , m_syncStatus(SYNC_IDLE)
    , m_lastSyncError(NULL)
    , m_lastSyncTime(0)
    , m_lastSyncAttemptTick(GetTickCount())
    , m_syncAttempted(false)
    , m_autoSyncEnabled(false)
    , m_autoSyncIntervalSeconds(300)
    , m_skippedCount(0)
    , m_connectivityCount(0)
{
    for (int i = 0; i < MAX_BACKENDS; i++) {
        m_backends[i] = NULL;
    }

    RegisterBackend(backend);
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

// ---------------------------------------------------------------------------
// Backend registry
// ---------------------------------------------------------------------------

bool SyncEngine::IsValidInstanceId(const TCHAR* instanceId)
{
    if (!instanceId || instanceId[0] == (TCHAR)'\0') {
        return false;
    }

    int n = 0;
    for (const TCHAR* p = instanceId; *p != (TCHAR)'\0'; p++, n++) {
        if (n >= MAX_INSTANCE_ID_CHARS - 1) {
            return false;
        }
        // ':' ends the type token, '.' separates id from type, and the brackets
        // and the space belong to the record header around it.
        if (*p == (TCHAR)':' || *p == (TCHAR)'.' || *p == (TCHAR)' ' ||
            *p == (TCHAR)'[' || *p == (TCHAR)']') {
            return false;
        }
    }

    return true;
}

void SyncEngine::RegisterBackend(InventoryBackend* backend)
{
    if (!backend || m_backendCount >= MAX_BACKENDS) {
        return;
    }

    // An id that cannot survive the queue-record round trip is worse than no
    // backend at all: work would be queued under a tag that never routes back.
    const TCHAR* instanceId = backend->GetInstanceId();
    if (!IsValidInstanceId(instanceId)) {
        return;
    }

    for (int i = 0; i < m_backendCount; i++) {
        if (m_backends[i] == backend ||
            lstrcmp(m_backends[i]->GetInstanceId(), instanceId) == 0) {
            return;
        }
    }

    m_backends[m_backendCount++] = backend;

    if (m_activeIndex < 0) {
        m_activeIndex = 0;
    }
}

bool SyncEngine::SetActiveBackend(const TCHAR* instanceId)
{
    if (!instanceId) {
        return false;
    }

    for (int i = 0; i < m_backendCount; i++) {
        if (lstrcmp(m_backends[i]->GetInstanceId(), instanceId) == 0) {
            m_activeIndex = i;
            return true;
        }
    }

    return false;
}

InventoryBackend* SyncEngine::GetActiveBackend() const
{
    if (m_activeIndex < 0 || m_activeIndex >= m_backendCount) {
        return NULL;
    }
    return m_backends[m_activeIndex];
}

InventoryBackend* SyncEngine::FindBackend(const TCHAR* instanceId) const
{
    if (!instanceId) {
        return NULL;
    }

    for (int i = 0; i < m_backendCount; i++) {
        if (lstrcmp(m_backends[i]->GetInstanceId(), instanceId) == 0) {
            return m_backends[i];
        }
    }

    return NULL;
}

int SyncEngine::GetBackendCount() const
{
    return m_backendCount;
}

// ---------------------------------------------------------------------------
// Queue management
// ---------------------------------------------------------------------------

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

    // Tag the entry with the backend that owns it. A type that already carries
    // a '.' is taken to be qualified already, which is how a caller queues work
    // for a backend other than the active one. Registration guarantees the
    // active backend's id is usable here.
    if (!wcschr(transactionType, (TCHAR)'.')) {
        const InventoryBackend* active = GetActiveBackend();
        if (active) {
            entry.Append(active->GetInstanceId());
            entry.AppendChar((TCHAR)'.');
        }
    }

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

// ---------------------------------------------------------------------------
// Sync
// ---------------------------------------------------------------------------

bool SyncEngine::Sync()
{
    m_syncStatus = SYNC_IN_PROGRESS;
    m_lastSyncAttemptTick = GetTickCount();
    m_syncAttempted = true;
    m_skippedCount = 0;

    SetLastSyncError(NULL);

    if (!m_journal) {
        m_syncStatus = SYNC_FAILED;
        SetLastSyncError(TEXT("No journal"));
        return false;
    }

    // Every configured backend gets a say: the queue can hold work for a
    // backend that is not the active one, and a NetBox on the local LAN is
    // still drainable while the HomeBox server on the far side of a dead GPRS
    // link is not.
    if (!AnyBackendOnline()) {
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

        switch (ProcessQueuedTransaction(transactions[i])) {
        case REPLAY_SENT:
            successCount++;
            // Mark as synced in journal
            m_journal->MarkTransactionSynced(transactions[i]);
            break;

        case REPLAY_SKIPPED:
            // Nobody can replay this - the backend it names is not configured,
            // or the record is malformed. It stays queued and is kept out of
            // the success/failure ratio so one stranded entry cannot pin the
            // status at "failed" on every sync for the life of the device.
            m_skippedCount++;
            break;

        case REPLAY_RETRY:
        default:
            // Left queued so the next Sync() retries it.
            failCount++;
            break;
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

        if (m_skippedCount > 0) {
            // Reported, but not as a failure: the sync itself worked.
            const int kCap = 128;
            TCHAR message[kCap];
            message[0] = (TCHAR)'\0';
            Str::Append(message, kCap, TEXT("Skipped "));
            Str::AppendInt(message, kCap, m_skippedCount);
            Str::Append(message, kCap, TEXT(" for unknown backends"));
            SetLastSyncError(message);
        }

        return true;
    } else if (successCount > 0) {
        // Partial success
        m_syncStatus = SYNC_PARTIAL;
        m_lastSyncTime = GetTickCount();

        const int kCap = 128;
        TCHAR errorMsg[kCap];
        errorMsg[0] = (TCHAR)'\0';
        Str::Append(errorMsg, kCap, TEXT("Synced "));
        Str::AppendInt(errorMsg, kCap, successCount);
        Str::Append(errorMsg, kCap, TEXT(" of "));
        Str::AppendInt(errorMsg, kCap, count);
        Str::Append(errorMsg, kCap, TEXT(" transactions"));
        if (m_skippedCount > 0) {
            Str::Append(errorMsg, kCap, TEXT(", "));
            Str::AppendInt(errorMsg, kCap, m_skippedCount);
            Str::Append(errorMsg, kCap, TEXT(" skipped"));
        }
        SetLastSyncError(errorMsg);

        return true;
    }

    m_syncStatus = SYNC_FAILED;
    SetLastSyncError(TEXT("All transactions failed to sync"));

    // Nothing got through: the most likely explanation is that coverage was
    // lost since the probe, so drop the cached answers instead of reporting
    // "online" for the rest of the cache window.
    InvalidateConnectivity();

    return false;
}

bool SyncEngine::SyncItem(const TCHAR* transactionLine)
{
    if (!transactionLine || Str::Length(transactionLine) == 0 || !m_journal) {
        return false;
    }

    m_skippedCount = 0;

    // Connectivity is checked per backend inside ProcessQueuedTransaction: this
    // entry may belong to a backend other than the active one.
    if (ProcessQueuedTransaction(transactionLine) != REPLAY_SENT) {
        return false;
    }

    // Mark as synced
    m_journal->MarkTransactionSynced(transactionLine);
    return true;
}

ReplayResult SyncEngine::ProcessQueuedTransaction(const TCHAR* transaction)
{
    if (!transaction) {
        return REPLAY_SKIPPED;
    }

    ParsedRecord record;
    if (!ParseQueuedPayload(transaction, &record)) {
        // A record whose shape we cannot read now will not become readable
        // later, so retrying it every five minutes forever only keeps the queue
        // looking broken. It stays queued for the operator to remove.
        return REPLAY_SKIPPED;
    }

    InventoryBackend* backend = NULL;

    if (record.instanceId[0] != (TCHAR)'\0') {
        backend = FindBackend(record.instanceId);
    } else {
        // Untagged: written by a build that only knew about HomeBox.
        for (int i = 0; i < m_backendCount; i++) {
            if (lstrcmp(m_backends[i]->GetKind(), kLegacyKind) == 0) {
                backend = m_backends[i];
                break;
            }
        }
    }

    if (!backend) {
        return REPLAY_SKIPPED;
    }

    // Do not spend a blocking request - and its whole timeout - on a backend
    // whose host does not resolve. With two backends on different networks the
    // batch would otherwise stall on the unreachable one before reaching the
    // entries that could have been sent.
    if (!CheckConnectivity(backend)) {
        return REPLAY_RETRY;
    }

    return backend->Replay(record.type, record.data);
}

bool SyncEngine::IsOnline() const
{
    return CheckConnectivity(GetActiveBackend());
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

int SyncEngine::GetSkippedCount() const
{
    return m_skippedCount;
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

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------

void SyncEngine::InvalidateConnectivity() const
{
    m_connectivityCount = 0;
}

bool SyncEngine::AnyBackendOnline() const
{
    // The active backend answers first: it is the one whose cache entry is
    // warm, so the common case costs no lookup at all.
    if (CheckConnectivity(GetActiveBackend())) {
        return true;
    }

    for (int i = 0; i < m_backendCount; i++) {
        if (i != m_activeIndex && CheckConnectivity(m_backends[i])) {
            return true;
        }
    }

    return false;
}

bool SyncEngine::CheckConnectivity(const InventoryBackend* backend) const
{
    if (!backend) {
        return false;
    }

    const TCHAR* instanceId = backend->GetInstanceId();
    if (!instanceId) {
        return false;
    }

    // Sync(), IsOnline() and the UI status line all land here, and every miss
    // costs a synchronous gethostbyname - several seconds on GPRS, on whichever
    // thread asked. A few seconds of staleness is cheaper than that.
    DWORD now = GetTickCount();
    int slot = -1;
    for (int i = 0; i < m_connectivityCount; i++) {
        if (lstrcmp(m_connectivity[i].instanceId, instanceId) == 0) {
            slot = i;
            break;
        }
    }

    if (slot >= 0 && (DWORD)(now - m_connectivity[slot].tick) < kConnectivityCacheMs) {
        return m_connectivity[slot].online;
    }

    // Try a simple name resolution to test connectivity to this backend's host.
    TCHAR host[256];
    if (!HostFromUrl(host, 256, backend->GetBaseUrl())) {
        // A backend with no usable URL is misconfigured rather than offline;
        // nothing is cached, so fixing the URL takes effect immediately.
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

    if (slot < 0) {
        if (m_connectivityCount < MAX_BACKENDS) {
            slot = m_connectivityCount++;
        } else {
            // Full only if a backend was renamed while the engine was alive;
            // recycle the stalest entry rather than growing the array.
            slot = 0;
            for (int i = 1; i < m_connectivityCount; i++) {
                if ((DWORD)(now - m_connectivity[i].tick) >
                    (DWORD)(now - m_connectivity[slot].tick)) {
                    slot = i;
                }
            }
        }
        Str::Copy(m_connectivity[slot].instanceId, MAX_INSTANCE_ID_CHARS, instanceId);
    }

    m_connectivity[slot].online = (hostInfo != NULL);
    m_connectivity[slot].tick = now;

    return m_connectivity[slot].online;
}

} // namespace HBX
