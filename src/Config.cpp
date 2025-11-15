#include "../include/Config.hpp"
#include "../include/Models/JsonLite.hpp"
#include <stdio.h>
#include <stdlib.h>

namespace HBX {

Config::Config()
    : m_apiBaseUrl(NULL)
    , m_deviceId(NULL)
    , m_authToken(NULL)
    , m_syncIntervalSeconds(300) // Default 5 minutes
    , m_offlineModeEnabled(true)
{
    InitDefaults();
}

Config::~Config()
{
    Cleanup();
}

void Config::InitDefaults()
{
    // Set default values
    SetApiBaseUrl(TEXT("http://localhost:8080/api"));
    SetDeviceId(TEXT("MC75-DEVICE-001"));
    SetAuthToken(TEXT(""));
    m_syncIntervalSeconds = 300;
    m_offlineModeEnabled = true;
}

void Config::Cleanup()
{
    if (m_apiBaseUrl) {
        delete[] m_apiBaseUrl;
        m_apiBaseUrl = NULL;
    }
    if (m_deviceId) {
        delete[] m_deviceId;
        m_deviceId = NULL;
    }
    if (m_authToken) {
        delete[] m_authToken;
        m_authToken = NULL;
    }
}

bool Config::Load(const TCHAR* configPath)
{
    // TODO: Implement JSON config file loading
    // For now, use defaults
    return true;
}

bool Config::Save(const TCHAR* configPath)
{
    // TODO: Implement JSON config file saving
    return true;
}

const TCHAR* Config::GetApiBaseUrl() const
{
    return m_apiBaseUrl;
}

const TCHAR* Config::GetDeviceId() const
{
    return m_deviceId;
}

const TCHAR* Config::GetAuthToken() const
{
    return m_authToken;
}

int Config::GetSyncIntervalSeconds() const
{
    return m_syncIntervalSeconds;
}

bool Config::IsOfflineModeEnabled() const
{
    return m_offlineModeEnabled;
}

void Config::SetApiBaseUrl(const TCHAR* url)
{
    if (m_apiBaseUrl) {
        delete[] m_apiBaseUrl;
    }
    if (url) {
        int len = lstrlen(url) + 1;
        m_apiBaseUrl = new TCHAR[len];
        lstrcpy(m_apiBaseUrl, url);
    } else {
        m_apiBaseUrl = NULL;
    }
}

void Config::SetDeviceId(const TCHAR* deviceId)
{
    if (m_deviceId) {
        delete[] m_deviceId;
    }
    if (deviceId) {
        int len = lstrlen(deviceId) + 1;
        m_deviceId = new TCHAR[len];
        lstrcpy(m_deviceId, deviceId);
    } else {
        m_deviceId = NULL;
    }
}

void Config::SetAuthToken(const TCHAR* token)
{
    if (m_authToken) {
        delete[] m_authToken;
    }
    if (token) {
        int len = lstrlen(token) + 1;
        m_authToken = new TCHAR[len];
        lstrcpy(m_authToken, token);
    } else {
        m_authToken = NULL;
    }
}

void Config::SetSyncIntervalSeconds(int seconds)
{
    m_syncIntervalSeconds = seconds;
}

void Config::SetOfflineModeEnabled(bool enabled)
{
    m_offlineModeEnabled = enabled;
}

} // namespace HBX
