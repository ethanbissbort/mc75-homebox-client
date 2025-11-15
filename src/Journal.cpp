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
    // TODO: Implement
    *count = 0;
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
