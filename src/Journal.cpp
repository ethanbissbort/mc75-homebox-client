#include "../include/Journal.hpp"
#include <stdio.h>

namespace HBX {

Journal::Journal()
    : m_fileHandle(INVALID_HANDLE_VALUE)
    , m_journalPath(NULL)
    , m_transactionCount(0)
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
    if (m_journalPath) {
        delete[] m_journalPath;
    }
    
    int len = lstrlen(journalPath) + 1;
    m_journalPath = new TCHAR[len];
    lstrcpy(m_journalPath, journalPath);
    
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
    
    return (m_fileHandle != INVALID_HANDLE_VALUE);
}

bool Journal::LogTransaction(const TCHAR* transactionType, const TCHAR* itemId, const TCHAR* details)
{
    m_transactionCount++;
    return WriteEntry(TEXT("TRANS"), details);
}

bool Journal::LogError(const TCHAR* errorCode, const TCHAR* errorMessage)
{
    return WriteEntry(TEXT("ERROR"), errorMessage);
}

bool Journal::LogInfo(const TCHAR* message)
{
    return WriteEntry(TEXT("INFO"), message);
}

bool Journal::GetPendingTransactions(TCHAR*** transactions, int* count)
{
    if (!transactions || !count) {
        return false;
    }

    *transactions = NULL;
    *count = 0;

    if (m_fileHandle == INVALID_HANDLE_VALUE || m_transactionCount == 0) {
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
                        for (int j = 0; j < linePos; j++) {
                            trans[j] = (TCHAR)line[j];
                        }
                        trans[linePos] = '\0';

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

    return true;
}

bool Journal::MarkTransactionSynced(const TCHAR* transactionId)
{
    // TODO: Implement
    return false;
}

int Journal::GetTransactionCount() const
{
    return m_transactionCount;
}

bool Journal::Compact()
{
    // TODO: Implement journal compaction
    return false;
}

bool Journal::Clear()
{
    m_transactionCount = 0;
    // TODO: Clear journal file
    return true;
}

bool Journal::WriteEntry(const TCHAR* level, const TCHAR* message)
{
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    // TODO: Format and write entry with timestamp
    DWORD bytesWritten;
    WriteFile(m_fileHandle, message, lstrlen(message) * sizeof(TCHAR), &bytesWritten, NULL);
    
    return FlushToDisk();
}

TCHAR* Journal::FormatTimestamp()
{
    // TODO: Format timestamp
    static TCHAR buffer[32];
    wsprintf(buffer, TEXT("%d"), GetTickCount());
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

} // namespace HBX
