#include "../include/Journal.hpp"
#include "../include/Common.hpp"
#include <stdio.h>

namespace HBX {

Journal::Journal()
    : m_fileHandle(INVALID_HANDLE_VALUE)
    , m_journalPath(NULL)
    , m_transactionCount(0)
    , m_lastError(RESULT_SUCCESS)
{
}

Journal::~Journal()
{
    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_fileHandle);
    }
    if (m_journalPath) {
        delete[] m_journalPath;
    }
}

bool Journal::Initialize(const TCHAR* journalPath)
{
    if (!journalPath) {
        m_lastError = RESULT_INVALID_PARAM;
        return false;
    }

    if (m_journalPath) {
        delete[] m_journalPath;
    }

    m_journalPath = Common::StrDuplicate(journalPath);
    if (!m_journalPath) {
        m_lastError = RESULT_OUT_OF_MEMORY;
        return false;
    }

    // Open or create journal file
    m_fileHandle = CreateFile(
        journalPath,
        GENERIC_WRITE | GENERIC_READ,
        0,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    m_lastError = RESULT_SUCCESS;
    return true;
}

bool Journal::LogTransaction(const TCHAR* transactionType, const TCHAR* itemId, const TCHAR* details)
{
    if (!details) {
        m_lastError = RESULT_INVALID_PARAM;
        return false;
    }

    if (!WriteEntry(TEXT("TRANS"), details)) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    m_transactionCount++;
    m_lastError = RESULT_SUCCESS;
    return true;
}

bool Journal::LogError(const TCHAR* errorCode, const TCHAR* errorMessage)
{
    if (!errorMessage) {
        m_lastError = RESULT_INVALID_PARAM;
        return false;
    }

    if (!WriteEntry(TEXT("ERROR"), errorMessage)) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    m_lastError = RESULT_SUCCESS;
    return true;
}

bool Journal::LogInfo(const TCHAR* message)
{
    if (!message) {
        m_lastError = RESULT_INVALID_PARAM;
        return false;
    }

    if (!WriteEntry(TEXT("INFO"), message)) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    m_lastError = RESULT_SUCCESS;
    return true;
}

bool Journal::GetPendingTransactions(TCHAR*** transactions, int* count)
{
    if (!transactions || !count) {
        m_lastError = RESULT_INVALID_PARAM;
        return false;
    }

    *transactions = NULL;
    *count = 0;

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        m_lastError = RESULT_NOT_INITIALIZED;
        return false;
    }

    if (m_transactionCount == 0) {
        m_lastError = RESULT_SUCCESS;
        return true; // No transactions
    }

    // Allocate array for transaction pointers
    int maxTransactions = m_transactionCount;
    TCHAR** transArray = new TCHAR*[maxTransactions];
    int transCount = 0;

    // Read file from beginning
    SetFilePointer(m_fileHandle, 0, NULL, FILE_BEGIN);

    char buffer[1024];
    DWORD bytesRead;
    char line[1024];
    int linePos = 0;

    while (ReadFile(m_fileHandle, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; i++) {
            if (buffer[i] == '\n' || buffer[i] == '\r') {
                if (linePos > 0) {
                    line[linePos] = '\0';

                    // Check if line contains transaction (simple check for "TRANS")
                    if (strstr(line, "TRANS") && !strstr(line, "SYNCED")) {
                        // Convert to TCHAR and store
                        int len = linePos + 1;
                        TCHAR* trans = new TCHAR[len];
                        Common::CharToTChar(line, trans, len);

                        transArray[transCount++] = trans;

                        if (transCount >= maxTransactions) {
                            break;
                        }
                    }

                    linePos = 0;
                }
            } else {
                if (linePos < sizeof(line) - 1) {
                    line[linePos++] = buffer[i];
                }
            }
        }

        if (transCount >= maxTransactions) {
            break;
        }
    }

    *transactions = transArray;
    *count = transCount;

    m_lastError = RESULT_SUCCESS;
    return true;
}

bool Journal::MarkTransactionSynced(const TCHAR* transactionId)
{
    if (!transactionId) {
        m_lastError = RESULT_INVALID_PARAM;
        return false;
    }

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        m_lastError = RESULT_NOT_INITIALIZED;
        return false;
    }

    // Mark transaction as synced by writing a SYNCED entry
    // Format: [TIMESTAMP] SYNCED: transactionId
    TCHAR syncEntry[512];
    wsprintf(syncEntry, TEXT("%s"), transactionId);

    if (!WriteEntry(TEXT("SYNCED"), syncEntry)) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    // Decrement transaction count since it's now synced
    if (m_transactionCount > 0) {
        m_transactionCount--;
    }

    m_lastError = RESULT_SUCCESS;
    return true;
}

int Journal::GetTransactionCount() const
{
    return m_transactionCount;
}

bool Journal::Compact()
{
    if (m_fileHandle == INVALID_HANDLE_VALUE || !m_journalPath) {
        m_lastError = RESULT_NOT_INITIALIZED;
        return false;
    }

    // Read all entries
    SetFilePointer(m_fileHandle, 0, NULL, FILE_BEGIN);

    char* fileContent = NULL;
    DWORD fileSize = GetFileSize(m_fileHandle, NULL);

    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    fileContent = new char[fileSize + 1];
    DWORD bytesRead;

    if (!ReadFile(m_fileHandle, fileContent, fileSize, &bytesRead, NULL)) {
        delete[] fileContent;
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }
    fileContent[bytesRead] = '\0';

    // Create temporary file for compacted journal
    TCHAR tempPath[MAX_PATH];
    wsprintf(tempPath, TEXT("%s.tmp"), m_journalPath);

    HANDLE tempHandle = CreateFile(
        tempPath,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (tempHandle == INVALID_HANDLE_VALUE) {
        delete[] fileContent;
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    // Write only unsynced transactions and recent INFO/ERROR entries
    char line[2048];
    int linePos = 0;
    int newTransactionCount = 0;

    for (DWORD i = 0; i < bytesRead; i++) {
        if (fileContent[i] == '\n' || fileContent[i] == '\r') {
            if (linePos > 0) {
                line[linePos] = '\0';

                // Keep unsynced transactions
                bool keepLine = false;
                if (strstr(line, "TRANS") && !strstr(line, "SYNCED")) {
                    keepLine = true;
                    newTransactionCount++;
                }
                // Keep recent errors (for debugging)
                else if (strstr(line, "ERROR")) {
                    keepLine = true;
                }

                if (keepLine) {
                    DWORD written;
                    WriteFile(tempHandle, line, linePos, &written, NULL);
                    WriteFile(tempHandle, "\r\n", 2, &written, NULL);
                }

                linePos = 0;
            }
        } else {
            if (linePos < sizeof(line) - 1) {
                line[linePos++] = fileContent[i];
            }
        }
    }

    CloseHandle(tempHandle);
    delete[] fileContent;

    // Replace original file with compacted version
    CloseHandle(m_fileHandle);
    DeleteFile(m_journalPath);
    MoveFile(tempPath, m_journalPath);

    // Reopen journal file
    m_fileHandle = CreateFile(
        m_journalPath,
        GENERIC_WRITE | GENERIC_READ,
        0,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    m_transactionCount = newTransactionCount;

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    m_lastError = RESULT_SUCCESS;
    return true;
}

bool Journal::Clear()
{
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        m_lastError = RESULT_NOT_INITIALIZED;
        return false;
    }

    // Close and reopen file to truncate it
    CloseHandle(m_fileHandle);

    m_fileHandle = CreateFile(
        m_journalPath,
        GENERIC_WRITE | GENERIC_READ,
        0,
        NULL,
        CREATE_ALWAYS, // Truncate existing file
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    m_transactionCount = 0;

    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        m_lastError = RESULT_FILE_IO_ERROR;
        return false;
    }

    m_lastError = RESULT_SUCCESS;
    return true;
}

bool Journal::WriteEntry(const TCHAR* level, const TCHAR* message)
{
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Format and write entry with timestamp
    // Format: [TIMESTAMP] LEVEL: message\r\n
    TCHAR* timestamp = FormatTimestamp();

    // Build entry string
    TCHAR entry[2048];
    wsprintf(entry, TEXT("[%s] %s: %s\r\n"), timestamp, level, message);

    // Convert to ANSI for file writing (journal file is ANSI)
    char ansiEntry[2048];
    Common::TCharToChar(entry, ansiEntry, 2048);

    // Seek to end of file
    SetFilePointer(m_fileHandle, 0, NULL, FILE_END);

    // Write entry
    DWORD bytesWritten;
    bool success = WriteFile(m_fileHandle, ansiEntry, strlen(ansiEntry), &bytesWritten, NULL);

    if (success) {
        FlushToDisk();
    }

    return success;
}

TCHAR* Journal::FormatTimestamp()
{
    // Format timestamp as YYYY-MM-DD HH:MM:SS
    static TCHAR buffer[32];

    SYSTEMTIME st;
    GetLocalTime(&st);

    wsprintf(buffer, TEXT("%04d-%02d-%02d %02d:%02d:%02d"),
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    return buffer;
}

bool Journal::FlushToDisk()
{
    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(m_fileHandle);
        return true;
    }
    return false;
}

ResultCode Journal::GetLastError() const
{
    return m_lastError;
}

} // namespace HBX
