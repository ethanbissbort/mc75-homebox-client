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
    // Try to open config file
    HANDLE hFile = CreateFile(
        configPath,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        // File doesn't exist, use defaults
        InitDefaults();
        return true;
    }

    // Get file size
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        InitDefaults();
        return true;
    }

    // Read file content
    char* buffer = new char[fileSize + 1];
    DWORD bytesRead = 0;
    bool success = ReadFile(hFile, buffer, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    if (!success || bytesRead == 0) {
        delete[] buffer;
        InitDefaults();
        return false;
    }

    buffer[bytesRead] = '\0';

    // Convert to TCHAR if needed
    TCHAR* jsonContent = new TCHAR[bytesRead + 1];
    for (DWORD i = 0; i <= bytesRead; i++) {
        jsonContent[i] = (TCHAR)buffer[i];
    }
    delete[] buffer;

    // Parse simple JSON manually (lightweight for embedded)
    // Look for key-value pairs
    TCHAR tempValue[512];

    // Parse apiBaseUrl
    if (ExtractJsonString(jsonContent, TEXT("apiBaseUrl"), tempValue, 512)) {
        SetApiBaseUrl(tempValue);
    }

    // Parse deviceId
    if (ExtractJsonString(jsonContent, TEXT("deviceId"), tempValue, 512)) {
        SetDeviceId(tempValue);
    }

    // Parse authToken
    if (ExtractJsonString(jsonContent, TEXT("authToken"), tempValue, 512)) {
        SetAuthToken(tempValue);
    }

    // Parse syncIntervalSeconds
    int intValue;
    if (ExtractJsonInt(jsonContent, TEXT("syncIntervalSeconds"), &intValue)) {
        m_syncIntervalSeconds = intValue;
    }

    // Parse offlineModeEnabled
    bool boolValue;
    if (ExtractJsonBool(jsonContent, TEXT("offlineModeEnabled"), &boolValue)) {
        m_offlineModeEnabled = boolValue;
    }

    delete[] jsonContent;
    return true;
}

// Helper function to extract string value from simple JSON
bool Config::ExtractJsonString(const TCHAR* json, const TCHAR* key, TCHAR* value, int maxLen)
{
    // Find the key
    TCHAR searchKey[256];
    wsprintf(searchKey, TEXT("\"%s\""), key);

    const TCHAR* keyPos = wcsstr(json, searchKey);
    if (!keyPos) {
        return false;
    }

    // Find the colon after the key
    const TCHAR* colonPos = wcschr(keyPos, ':');
    if (!colonPos) {
        return false;
    }

    // Skip whitespace and find opening quote
    const TCHAR* startQuote = colonPos + 1;
    while (*startQuote && (*startQuote == ' ' || *startQuote == '\t' || *startQuote == '\n' || *startQuote == '\r')) {
        startQuote++;
    }

    if (*startQuote != '"') {
        return false;
    }
    startQuote++;

    // Find closing quote
    const TCHAR* endQuote = wcschr(startQuote, '"');
    if (!endQuote) {
        return false;
    }

    // Copy value
    int len = (int)(endQuote - startQuote);
    if (len >= maxLen) {
        len = maxLen - 1;
    }

    wcsncpy(value, startQuote, len);
    value[len] = '\0';

    return true;
}

// Helper function to extract integer value from simple JSON
bool Config::ExtractJsonInt(const TCHAR* json, const TCHAR* key, int* value)
{
    TCHAR searchKey[256];
    wsprintf(searchKey, TEXT("\"%s\""), key);

    const TCHAR* keyPos = wcsstr(json, searchKey);
    if (!keyPos) {
        return false;
    }

    const TCHAR* colonPos = wcschr(keyPos, ':');
    if (!colonPos) {
        return false;
    }

    // Skip whitespace
    const TCHAR* numStart = colonPos + 1;
    while (*numStart && (*numStart == ' ' || *numStart == '\t' || *numStart == '\n' || *numStart == '\r')) {
        numStart++;
    }

    *value = _wtoi(numStart);
    return true;
}

// Helper function to extract boolean value from simple JSON
bool Config::ExtractJsonBool(const TCHAR* json, const TCHAR* key, bool* value)
{
    TCHAR searchKey[256];
    wsprintf(searchKey, TEXT("\"%s\""), key);

    const TCHAR* keyPos = wcsstr(json, searchKey);
    if (!keyPos) {
        return false;
    }

    const TCHAR* colonPos = wcschr(keyPos, ':');
    if (!colonPos) {
        return false;
    }

    // Skip whitespace
    const TCHAR* boolStart = colonPos + 1;
    while (*boolStart && (*boolStart == ' ' || *boolStart == '\t' || *boolStart == '\n' || *boolStart == '\r')) {
        boolStart++;
    }

    if (wcsncmp(boolStart, TEXT("true"), 4) == 0) {
        *value = true;
        return true;
    } else if (wcsncmp(boolStart, TEXT("false"), 5) == 0) {
        *value = false;
        return true;
    }

    return false;
}

bool Config::Save(const TCHAR* configPath)
{
    // Build JSON string
    TCHAR jsonBuffer[2048];
    int pos = 0;

    // Start JSON object
    pos += wsprintf(jsonBuffer + pos, TEXT("{\n"));

    // Write apiBaseUrl
    pos += wsprintf(jsonBuffer + pos, TEXT("  \"apiBaseUrl\": \"%s\",\n"),
                    m_apiBaseUrl ? m_apiBaseUrl : TEXT(""));

    // Write deviceId
    pos += wsprintf(jsonBuffer + pos, TEXT("  \"deviceId\": \"%s\",\n"),
                    m_deviceId ? m_deviceId : TEXT(""));

    // Write authToken
    pos += wsprintf(jsonBuffer + pos, TEXT("  \"authToken\": \"%s\",\n"),
                    m_authToken ? m_authToken : TEXT(""));

    // Write syncIntervalSeconds
    pos += wsprintf(jsonBuffer + pos, TEXT("  \"syncIntervalSeconds\": %d,\n"),
                    m_syncIntervalSeconds);

    // Write offlineModeEnabled (last item, no comma)
    pos += wsprintf(jsonBuffer + pos, TEXT("  \"offlineModeEnabled\": %s\n"),
                    m_offlineModeEnabled ? TEXT("true") : TEXT("false"));

    // End JSON object
    pos += wsprintf(jsonBuffer + pos, TEXT("}\n"));

    // Open/create file for writing
    HANDLE hFile = CreateFile(
        configPath,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Convert TCHAR to char for file writing
    char* asciiBuffer = new char[pos + 1];
    for (int i = 0; i < pos; i++) {
        asciiBuffer[i] = (char)jsonBuffer[i];
    }
    asciiBuffer[pos] = '\0';

    // Write to file
    DWORD bytesWritten = 0;
    bool success = WriteFile(hFile, asciiBuffer, pos, &bytesWritten, NULL);

    delete[] asciiBuffer;
    CloseHandle(hFile);

    return success && (bytesWritten == (DWORD)pos);
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
