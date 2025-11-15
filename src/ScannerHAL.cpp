#include "../include/ScannerHAL.hpp"

namespace HBX {

ScannerHAL::ScannerHAL()
    : m_scannerHandle(NULL)
    , m_initialized(false)
    , m_enabled(false)
    , m_lastBarcode(NULL)
    , m_callback(NULL)
    , m_callbackUserData(NULL)
    , m_scanThread(NULL)
    , m_scanThreadRunning(false)
{
}

ScannerHAL::~ScannerHAL()
{
    Shutdown();
    if (m_lastBarcode) {
        delete[] m_lastBarcode;
    }
}

bool ScannerHAL::Initialize()
{
    if (m_initialized) {
        return true;
    }
    
    // TODO: Initialize Zebra EMDK
    m_initialized = OpenScanner();
    return m_initialized;
}

bool ScannerHAL::Shutdown()
{
    if (!m_initialized) {
        return true;
    }
    
    DisableScanner();
    CloseScanner();
    m_initialized = false;
    return true;
}

bool ScannerHAL::IsInitialized() const
{
    return m_initialized;
}

bool ScannerHAL::EnableScanner()
{
    if (!m_initialized) {
        return false;
    }
    
    m_enabled = true;
    // TODO: Enable scanner via EMDK
    return true;
}

bool ScannerHAL::DisableScanner()
{
    m_enabled = false;
    // TODO: Disable scanner via EMDK
    return true;
}

bool ScannerHAL::TriggerScan()
{
    if (!m_enabled) {
        return false;
    }
    
    // TODO: Trigger scan via EMDK
    return false;
}

bool ScannerHAL::GetLastScan(TCHAR* barcode, DWORD maxLen)
{
    if (!m_lastBarcode || !barcode) {
        return false;
    }
    
    lstrcpyn(barcode, m_lastBarcode, maxLen);
    return true;
}

const TCHAR* ScannerHAL::GetScannerStatus() const
{
    if (!m_initialized) {
        return TEXT("Not initialized");
    }
    if (!m_enabled) {
        return TEXT("Disabled");
    }
    return TEXT("Ready");
}

bool ScannerHAL::SetScanMode(int mode)
{
    // TODO: Set scan mode via EMDK
    return true;
}

bool ScannerHAL::SetBeepEnabled(bool enabled)
{
    // TODO: Configure beep via EMDK
    return true;
}

bool ScannerHAL::SetVibrateEnabled(bool enabled)
{
    // TODO: Configure vibrate via EMDK
    return true;
}

void ScannerHAL::SetScanCallback(ScanCallback callback, void* userData)
{
    m_callback = callback;
    m_callbackUserData = userData;
}

bool ScannerHAL::OpenScanner()
{
    // TODO: Open scanner device via EMDK
    return true;
}

void ScannerHAL::CloseScanner()
{
    // TODO: Close scanner device via EMDK
}

DWORD WINAPI ScannerHAL::ScanThread(LPVOID param)
{
    // TODO: Implement scan thread
    return 0;
}

} // namespace HBX
