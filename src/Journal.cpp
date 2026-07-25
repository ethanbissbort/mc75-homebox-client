#include "../include/Journal.hpp"
#include "../include/StrUtil.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace HBX {

// ---------------------------------------------------------------------------
// Record labels and layout
// ---------------------------------------------------------------------------

static const char* const kTransLabel  = "] TRANS ";
static const char* const kSyncedLabel = "] SYNCED ";
static const char* const kErrorLabel  = "] ERROR: ";

// Sequence numbers are written zero-padded to a fixed width so a record's
// layout is completely predictable and parsing never has to search.
static const int kSequenceDigits = 8;

// A transaction payload is capped so a single record cannot be unbounded. The
// largest legitimate payload is an ITEM_UPDATE carrying a serialised Item.
static const int kMaxPayloadChars = 4096;

// Room for "[YYYY-MM-DD HH:MM:SS] TRANS nnnnnnnn: " plus the payload and CRLF.
static const int kMaxRecordChars = kMaxPayloadChars + 128;

/** Formats `value` zero-padded to kSequenceDigits. */
static void FormatSequence(TCHAR* buffer, int cap, DWORD value)
{
    if (cap < kSequenceDigits + 1) {
        if (cap > 0) {
            buffer[0] = 0;
        }
        return;
    }

    for (int i = kSequenceDigits - 1; i >= 0; i--) {
        buffer[i] = (TCHAR)('0' + (int)(value % 10UL));
        value /= 10UL;
    }
    buffer[kSequenceDigits] = 0;
}

/**
 * Reads a fixed-width decimal sequence number. Templated over the character
 * type so the same logic serves the TCHAR (in-memory) and char (on-disk)
 * parsers without either copy drifting from the other.
 */
template <typename CharT>
static bool ReadSequence(const CharT* text, DWORD* sequence)
{
    DWORD value = 0;
    for (int i = 0; i < kSequenceDigits; i++) {
        if (text[i] < (CharT)'0' || text[i] > (CharT)'9') {
            return false;
        }
        value = value * 10UL + (DWORD)(text[i] - (CharT)'0');
    }
    *sequence = value;
    return true;
}

/** Locates `needle` in the ASCII line `line` and returns the text after it. */
static const char* After(const char* line, const char* needle)
{
    const char* hit = strstr(line, needle);
    return hit ? (hit + strlen(needle)) : NULL;
}

static int CompareDword(const void* a, const void* b)
{
    DWORD lhs = *(const DWORD*)a;
    DWORD rhs = *(const DWORD*)b;
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
}

static bool SortedContains(const DWORD* sorted, int count, DWORD value)
{
    int lo = 0;
    int hi = count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (sorted[mid] == value) {
            return true;
        }
        if (sorted[mid] < value) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return false;
}

/**
 * Growable DWORD list; the number of acknowledgements in a journal is not known
 * before it is read.
 */
class DwordList {
public:
    DwordList() : m_data(NULL), m_count(0), m_cap(0), m_failed(false) {}
    ~DwordList() { delete[] m_data; }

    bool Add(DWORD value)
    {
        if (m_failed) {
            return false;
        }
        if (m_count == m_cap) {
            int newCap = m_cap ? m_cap * 2 : 64;
            DWORD* grown = new DWORD[newCap];
            if (!grown) {
                m_failed = true;
                return false;
            }
            for (int i = 0; i < m_count; i++) {
                grown[i] = m_data[i];
            }
            delete[] m_data;
            m_data = grown;
            m_cap = newCap;
        }
        m_data[m_count++] = value;
        return true;
    }

    void Sort()
    {
        if (m_count > 1) {
            qsort(m_data, (size_t)m_count, sizeof(DWORD), CompareDword);
        }
    }

    bool Contains(DWORD value) const { return SortedContains(m_data, m_count, value); }
    int Count() const { return m_count; }
    bool Failed() const { return m_failed; }

private:
    DWORD* m_data;
    int m_count;
    int m_cap;
    bool m_failed;

    DwordList(const DwordList&);
    DwordList& operator=(const DwordList&);
};

/**
 * Splits a NUL-terminated buffer into lines in place. Returns a heap array of
 * pointers into `content`; the caller delete[]s the array (not the strings).
 */
static char** SplitLines(char* content, DWORD size, int* lineCount)
{
    *lineCount = 0;

    // One pointer per byte is a safe upper bound and avoids a counting pass.
    char** lines = new char*[size + 1];
    if (!lines) {
        return NULL;
    }

    int count = 0;
    char* start = content;
    for (DWORD i = 0; i <= size; i++) {
        if (content[i] == '\n' || content[i] == '\r' || content[i] == '\0') {
            content[i] = '\0';
            if (start[0] != '\0') {
                lines[count++] = start;
            }
            start = &content[i + 1];
        }
    }

    *lineCount = count;
    return lines;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Journal::Journal()
    : m_fileHandle(INVALID_HANDLE_VALUE)
    , m_journalPath(NULL)
    , m_transactionCount(0)
    , m_nextSequence(1)
    , m_syncedMarkerCount(0)
    , m_maxFileBytes(256UL * 1024UL)
    , m_maxRetainedErrors(200)
{
    InitializeCriticalSection(&m_lock);
}

Journal::~Journal()
{
    EnterCriticalSection(&m_lock);
    CloseFileLocked();
    delete[] m_journalPath;
    m_journalPath = NULL;
    LeaveCriticalSection(&m_lock);

    DeleteCriticalSection(&m_lock);
}

void Journal::CloseFileLocked()
{
    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
    }
}

bool Journal::OpenFileLocked(DWORD disposition)
{
    if (!m_journalPath) {
        return false;
    }

    // FILE_SHARE_READ|FILE_SHARE_WRITE: the previous share mode of 0 made a
    // second open of the same path fail outright, which broke re-initialising
    // the journal and blocked any external reader on the device.
    m_fileHandle = CreateFile(
        m_journalPath,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        disposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    return (m_fileHandle != INVALID_HANDLE_VALUE);
}

bool Journal::Initialize(const TCHAR* journalPath)
{
    if (!journalPath || journalPath[0] == 0) {
        return false;
    }

    EnterCriticalSection(&m_lock);

    // Re-initialising previously leaked the open handle.
    CloseFileLocked();

    delete[] m_journalPath;
    m_journalPath = Str::Dup(journalPath);
    if (!m_journalPath) {
        LeaveCriticalSection(&m_lock);
        return false;
    }

    m_transactionCount = 0;
    m_nextSequence = 1;
    m_syncedMarkerCount = 0;

    bool ok = OpenFileLocked(OPEN_ALWAYS);
    if (ok) {
        // Recover the queue that survived the last shutdown. Without this the
        // pending count stayed at zero after a restart and Sync() cheerfully
        // reported success while queued work sat unsent on disk.
        RebuildStateLocked();
    }

    LeaveCriticalSection(&m_lock);
    return ok;
}

void Journal::SetMaxFileBytes(DWORD maxBytes)
{
    EnterCriticalSection(&m_lock);
    m_maxFileBytes = maxBytes;
    LeaveCriticalSection(&m_lock);
}

void Journal::SetMaxRetainedErrors(int maxErrors)
{
    EnterCriticalSection(&m_lock);
    m_maxRetainedErrors = (maxErrors < 0) ? 0 : maxErrors;
    LeaveCriticalSection(&m_lock);
}

// ---------------------------------------------------------------------------
// Reading the file
// ---------------------------------------------------------------------------

bool Journal::ReadAllLocked(char** contentOut, DWORD* sizeOut)
{
    *contentOut = NULL;
    *sizeOut = 0;

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD fileSize = GetFileSize(m_fileHandle, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        return false;
    }
    if (fileSize == 0) {
        char* empty = new char[1];
        if (!empty) {
            return false;
        }
        empty[0] = '\0';
        *contentOut = empty;
        return true;
    }

    char* content = new char[fileSize + 1];
    if (!content) {
        return false;
    }

    SetFilePointer(m_fileHandle, 0, NULL, FILE_BEGIN);

    // A single ReadFile is not guaranteed to return everything, so loop.
    DWORD total = 0;
    while (total < fileSize) {
        DWORD got = 0;
        if (!ReadFile(m_fileHandle, content + total, fileSize - total, &got, NULL) || got == 0) {
            break;
        }
        total += got;
    }
    content[total] = '\0';

    *contentOut = content;
    *sizeOut = total;
    return true;
}

bool Journal::RebuildStateLocked()
{
    char* content = NULL;
    DWORD size = 0;

    if (!ReadAllLocked(&content, &size) || size == 0) {
        delete[] content;
        return true; // empty journal: defaults already correct
    }

    int lineCount = 0;
    char** lines = SplitLines(content, size, &lineCount);
    if (!lines) {
        delete[] content;
        return false;
    }

    // Pass 1: every acknowledgement, and the highest sequence ever issued.
    DwordList synced;
    DWORD highest = 0;

    for (int i = 0; i < lineCount; i++) {
        const char* seqText = After(lines[i], kSyncedLabel);
        if (seqText) {
            DWORD seq;
            if (ReadSequence(seqText, &seq)) {
                synced.Add(seq);
                if (seq > highest) {
                    highest = seq;
                }
            }
            continue;
        }

        seqText = After(lines[i], kTransLabel);
        if (seqText) {
            DWORD seq;
            if (ReadSequence(seqText, &seq) && seq > highest) {
                highest = seq;
            }
        }
    }

    synced.Sort();

    // Pass 2: transactions with no acknowledgement are still pending.
    DWORD pending = 0;
    for (int i = 0; i < lineCount; i++) {
        const char* seqText = After(lines[i], kTransLabel);
        if (!seqText) {
            continue;
        }
        DWORD seq;
        if (ReadSequence(seqText, &seq) && !synced.Contains(seq)) {
            pending++;
        }
    }

    m_transactionCount = pending;
    m_syncedMarkerCount = (DWORD)synced.Count();
    m_nextSequence = highest + 1;

    delete[] lines;
    delete[] content;
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

void Journal::FormatTimestamp(TCHAR* buffer, int cap)
{
    // Caller-supplied buffer: the previous static was shared by the scanner and
    // UI threads, so two concurrent writes could interleave a timestamp.
    if (cap < 20) {
        if (cap > 0) {
            buffer[0] = 0;
        }
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    wsprintf(buffer, TEXT("%04d-%02d-%02d %02d:%02d:%02d"),
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
}

bool Journal::WriteEntryLocked(const TCHAR* label, const TCHAR* message)
{
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    TCHAR timestamp[32];
    FormatTimestamp(timestamp, 32);

    // Assembled with bounded appends rather than wsprintf: `message` is
    // caller-sized (a queued ITEM_UPDATE carries a whole serialised Item) and
    // would otherwise be truncated at wsprintf's 1024-character cap on the
    // device and overrun the buffer on the host.
    TCHAR* entry = new TCHAR[kMaxRecordChars];
    if (!entry) {
        return false;
    }
    entry[0] = 0;

    Str::Append(entry, kMaxRecordChars, TEXT("["));
    Str::Append(entry, kMaxRecordChars, timestamp);
    Str::Append(entry, kMaxRecordChars, TEXT("] "));
    Str::Append(entry, kMaxRecordChars, label);
    Str::Append(entry, kMaxRecordChars, message);
    Str::Append(entry, kMaxRecordChars, TEXT("\r\n"));

    // UTF-8 on disk. The previous code truncated each UTF-16 unit to a byte,
    // which silently corrupted any non-ASCII item name on the device and made
    // the record unreadable on the way back in.
    char* utf8 = Str::ToUtf8Alloc(entry);
    delete[] entry;
    if (!utf8) {
        return false;
    }

    SetFilePointer(m_fileHandle, 0, NULL, FILE_END);

    DWORD bytesWritten = 0;
    DWORD length = (DWORD)strlen(utf8);
    bool success = (WriteFile(m_fileHandle, utf8, length, &bytesWritten, NULL) != 0)
                   && (bytesWritten == length);

    delete[] utf8;

    if (success && m_fileHandle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(m_fileHandle);
    }

    return success;
}

// ---------------------------------------------------------------------------
// Audit logging
// ---------------------------------------------------------------------------

bool Journal::LogTransaction(const TCHAR* transactionType, const TCHAR* itemId, const TCHAR* details)
{
    EnterCriticalSection(&m_lock);

    // An AUDIT record documents that something happened. It deliberately does
    // not enter the sync queue -- see the class comment.
    TCHAR message[1024];
    message[0] = 0;

    Str::Append(message, 1024, transactionType ? transactionType : TEXT("UNKNOWN"));
    if (itemId && itemId[0] != 0) {
        Str::Append(message, 1024, TEXT(" "));
        Str::Append(message, 1024, itemId);
    }
    Str::Append(message, 1024, TEXT(": "));
    Str::Append(message, 1024, details ? details : TEXT(""));

    bool result = WriteEntryLocked(TEXT("AUDIT: "), message);
    MaybeCompactLocked();

    LeaveCriticalSection(&m_lock);
    return result;
}

bool Journal::LogError(const TCHAR* errorCode, const TCHAR* errorMessage)
{
    EnterCriticalSection(&m_lock);

    TCHAR message[1024];
    message[0] = 0;
    Str::Append(message, 1024, errorCode ? errorCode : TEXT("ERROR"));
    Str::Append(message, 1024, TEXT(": "));
    Str::Append(message, 1024, errorMessage ? errorMessage : TEXT(""));

    bool result = WriteEntryLocked(TEXT("ERROR: "), message);
    MaybeCompactLocked();

    LeaveCriticalSection(&m_lock);
    return result;
}

bool Journal::LogInfo(const TCHAR* message)
{
    EnterCriticalSection(&m_lock);
    bool result = WriteEntryLocked(TEXT("INFO: "), message ? message : TEXT(""));
    MaybeCompactLocked();
    LeaveCriticalSection(&m_lock);
    return result;
}

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

bool Journal::QueueTransaction(const TCHAR* payload, DWORD* outSequence)
{
    if (!payload) {
        return false;
    }

    EnterCriticalSection(&m_lock);

    DWORD sequence = m_nextSequence;

    TCHAR label[32];
    label[0] = 0;
    TCHAR digits[kSequenceDigits + 1];
    FormatSequence(digits, kSequenceDigits + 1, sequence);
    Str::Append(label, 32, TEXT("TRANS "));
    Str::Append(label, 32, digits);
    Str::Append(label, 32, TEXT(": "));

    bool result = WriteEntryLocked(label, payload);

    if (result) {
        m_nextSequence++;
        m_transactionCount++;
        if (outSequence) {
            *outSequence = sequence;
        }
        MaybeCompactLocked();
    }

    LeaveCriticalSection(&m_lock);
    return result;
}

bool Journal::ParseSequence(const TCHAR* line, DWORD* sequence)
{
    if (!line || !sequence) {
        return false;
    }

    // Find "] TRANS " without needing a wide strstr: walk to each ']'.
    for (int i = 0; line[i] != 0; i++) {
        if (line[i] != (TCHAR)']') {
            continue;
        }

        static const TCHAR kNeedle[] = { ']', ' ', 'T', 'R', 'A', 'N', 'S', ' ', 0 };
        int k = 0;
        while (kNeedle[k] != 0 && line[i + k] == kNeedle[k]) {
            k++;
        }
        if (kNeedle[k] != 0) {
            continue;
        }

        const TCHAR* digits = line + i + k;
        // Need kSequenceDigits characters followed by ':'.
        int have = 0;
        while (have < kSequenceDigits && digits[have] != 0) {
            have++;
        }
        if (have < kSequenceDigits) {
            return false;
        }
        return ReadSequence(digits, sequence);
    }

    return false;
}

const TCHAR* Journal::PayloadOf(const TCHAR* line)
{
    DWORD ignored;
    if (!line || !ParseSequence(line, &ignored)) {
        return NULL;
    }

    // Skip past "] TRANS nnnnnnnn: ".
    for (int i = 0; line[i] != 0; i++) {
        if (line[i] != (TCHAR)']') {
            continue;
        }
        static const TCHAR kNeedle[] = { ']', ' ', 'T', 'R', 'A', 'N', 'S', ' ', 0 };
        int k = 0;
        while (kNeedle[k] != 0 && line[i + k] == kNeedle[k]) {
            k++;
        }
        if (kNeedle[k] != 0) {
            continue;
        }

        const TCHAR* after = line + i + k + kSequenceDigits;
        if (after[0] == (TCHAR)':' && after[1] == (TCHAR)' ') {
            return after + 2;
        }
        if (after[0] == (TCHAR)':') {
            return after + 1;
        }
        return NULL;
    }

    return NULL;
}

bool Journal::GetPendingTransactions(TCHAR*** transactions, int* count)
{
    if (!transactions || !count) {
        return false;
    }

    *transactions = NULL;
    *count = 0;

    EnterCriticalSection(&m_lock);

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&m_lock);
        return false;
    }

    char* content = NULL;
    DWORD size = 0;
    if (!ReadAllLocked(&content, &size) || size == 0) {
        delete[] content;
        LeaveCriticalSection(&m_lock);
        return true; // nothing queued
    }

    int lineCount = 0;
    char** lines = SplitLines(content, size, &lineCount);
    if (!lines) {
        delete[] content;
        LeaveCriticalSection(&m_lock);
        return false;
    }

    // Acknowledged sequences first, so a transaction is emitted only when no
    // SYNCED marker refers to it. The previous implementation tested the TRANS
    // line itself for the substring "SYNCED" -- a line that never contains it,
    // so every already-synced transaction was replayed on every sync forever.
    DwordList synced;
    for (int i = 0; i < lineCount; i++) {
        const char* seqText = After(lines[i], kSyncedLabel);
        DWORD seq;
        if (seqText && ReadSequence(seqText, &seq)) {
            synced.Add(seq);
        }
    }
    synced.Sort();

    TCHAR** result = new TCHAR*[lineCount + 1];
    if (!result) {
        delete[] lines;
        delete[] content;
        LeaveCriticalSection(&m_lock);
        return false;
    }

    int pending = 0;
    for (int i = 0; i < lineCount; i++) {
        const char* seqText = After(lines[i], kTransLabel);
        if (!seqText) {
            continue;
        }

        DWORD seq;
        if (!ReadSequence(seqText, &seq) || synced.Contains(seq)) {
            continue;
        }

        TCHAR* line = Str::FromUtf8Alloc(lines[i]);
        if (!line) {
            break;
        }
        result[pending++] = line;
    }

    // The in-memory count is authoritative for the UI; keep it honest with what
    // is actually on disk.
    m_transactionCount = (DWORD)pending;

    delete[] lines;
    delete[] content;

    *transactions = result;
    *count = pending;

    LeaveCriticalSection(&m_lock);
    return true;
}

bool Journal::MarkSequenceSynced(DWORD sequence)
{
    EnterCriticalSection(&m_lock);

    TCHAR digits[kSequenceDigits + 1];
    FormatSequence(digits, kSequenceDigits + 1, sequence);

    // The marker carries only the sequence number, so its length is fixed no
    // matter how large the acknowledged payload was.
    bool result = WriteEntryLocked(TEXT("SYNCED "), digits);

    if (result) {
        if (m_transactionCount > 0) {
            m_transactionCount--;
        }
        m_syncedMarkerCount++;
        MaybeCompactLocked();
    }

    LeaveCriticalSection(&m_lock);
    return result;
}

bool Journal::MarkTransactionSynced(const TCHAR* transactionLine)
{
    DWORD sequence = 0;
    if (!ParseSequence(transactionLine, &sequence)) {
        return false;
    }
    return MarkSequenceSynced(sequence);
}

int Journal::GetTransactionCount() const
{
    EnterCriticalSection(&m_lock);
    int count = (int)m_transactionCount;
    LeaveCriticalSection(&m_lock);
    return count;
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

void Journal::MaybeCompactLocked()
{
    if (m_maxFileBytes == 0 || m_fileHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD size = GetFileSize(m_fileHandle, NULL);
    if (size == INVALID_FILE_SIZE || size < m_maxFileBytes) {
        return;
    }

    // Compacting is only worthwhile if there is something to reclaim.
    if (m_syncedMarkerCount == 0 && m_transactionCount > 0) {
        return;
    }

    CompactLocked();
}

bool Journal::Compact()
{
    EnterCriticalSection(&m_lock);
    bool result = CompactLocked();
    LeaveCriticalSection(&m_lock);
    return result;
}

bool Journal::CompactLocked()
{
    if (m_fileHandle == INVALID_HANDLE_VALUE || !m_journalPath) {
        return false;
    }

    char* content = NULL;
    DWORD size = 0;
    if (!ReadAllLocked(&content, &size)) {
        return false;
    }
    if (size == 0) {
        delete[] content;
        return true;
    }

    int lineCount = 0;
    char** lines = SplitLines(content, size, &lineCount);
    if (!lines) {
        delete[] content;
        return false;
    }

    DwordList synced;
    for (int i = 0; i < lineCount; i++) {
        const char* seqText = After(lines[i], kSyncedLabel);
        DWORD seq;
        if (seqText && ReadSequence(seqText, &seq)) {
            synced.Add(seq);
        }
    }
    synced.Sort();

    // Retain only the most recent errors so the file cannot creep back up.
    int errorTotal = 0;
    for (int i = 0; i < lineCount; i++) {
        if (After(lines[i], kErrorLabel)) {
            errorTotal++;
        }
    }
    int errorsToSkip = errorTotal - m_maxRetainedErrors;
    if (errorsToSkip < 0) {
        errorsToSkip = 0;
    }

    TCHAR tempPath[MAX_PATH];
    tempPath[0] = 0;
    Str::Copy(tempPath, MAX_PATH, m_journalPath);
    Str::Append(tempPath, MAX_PATH, TEXT(".tmp"));

    HANDLE tempHandle = CreateFile(
        tempPath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (tempHandle == INVALID_HANDLE_VALUE) {
        delete[] lines;
        delete[] content;
        return false;
    }

    DWORD kept = 0;
    int errorsSeen = 0;
    bool writeFailed = false;

    for (int i = 0; i < lineCount && !writeFailed; i++) {
        char* line = lines[i];
        bool keep = false;

        const char* seqText = After(line, kTransLabel);
        if (seqText) {
            DWORD seq;
            if (ReadSequence(seqText, &seq) && !synced.Contains(seq)) {
                keep = true;
                kept++;
            }
            // Acknowledged transactions and every SYNCED marker are dropped.
        } else if (After(line, kSyncedLabel)) {
            keep = false;
        } else if (After(line, kErrorLabel)) {
            errorsSeen++;
            keep = (errorsSeen > errorsToSkip);
        }
        // INFO and AUDIT records are pure history and are not retained.

        if (keep) {
            DWORD written = 0;
            DWORD length = (DWORD)strlen(line);
            if (!WriteFile(tempHandle, line, length, &written, NULL) || written != length) {
                writeFailed = true;
                break;
            }
            if (!WriteFile(tempHandle, "\r\n", 2, &written, NULL)) {
                writeFailed = true;
                break;
            }
        }
    }

    CloseHandle(tempHandle);
    delete[] lines;
    delete[] content;

    if (writeFailed) {
        // Leave the original journal untouched rather than replacing it with a
        // partial rewrite -- losing queued transactions is far worse than a
        // journal that stays large.
        DeleteFile(tempPath);
        return false;
    }

    CloseFileLocked();
    DeleteFile(m_journalPath);

    if (!MoveFile(tempPath, m_journalPath)) {
        // The rename failed; reopen whatever is still there so the journal
        // keeps working.
        OpenFileLocked(OPEN_ALWAYS);
        RebuildStateLocked();
        return false;
    }

    if (!OpenFileLocked(OPEN_ALWAYS)) {
        return false;
    }

    m_transactionCount = kept;
    m_syncedMarkerCount = 0;

    return true;
}

bool Journal::Clear()
{
    EnterCriticalSection(&m_lock);

    if (!m_journalPath) {
        LeaveCriticalSection(&m_lock);
        return false;
    }

    CloseFileLocked();

    bool ok = OpenFileLocked(CREATE_ALWAYS);

    m_transactionCount = 0;
    m_syncedMarkerCount = 0;
    // Sequence numbering deliberately continues: a marker written before the
    // clear must never match a transaction queued after it.

    LeaveCriticalSection(&m_lock);
    return ok;
}

} // namespace HBX
