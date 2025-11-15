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
    // TODO: Queue transaction
    return m_journal->LogTransaction(transactionType, TEXT(""), data);
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
    
    // TODO: Implement sync logic
    
    m_syncStatus = SYNC_SUCCESS;
    m_lastSyncTime = GetTickCount();
    return true;
}

bool SyncEngine::SyncItem(const TCHAR* transactionId)
{
    // TODO: Sync single transaction
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
    // TODO: Implement connectivity check
    return true;
}

bool SyncEngine::ProcessQueuedTransaction(const TCHAR* transaction)
{
    // TODO: Process transaction
    return false;
}

} // namespace HBX
