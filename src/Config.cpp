#include "../include/Config.hpp"
#include "../include/Models/JsonLite.hpp"
#include "../include/StrUtil.hpp"
#include <wchar.h>

namespace HBX {

using Models::JsonLite;

namespace {

// ---------------------------------------------------------------------------
// Tolerant key scanner
//
// hb_conf.json is routinely hand-edited on the device, and one trailing comma
// is enough to fail a strict parse. When that happens we fall back to this
// scanner so the admin keeps the settings that are readable instead of
// silently reverting to every default.
// ---------------------------------------------------------------------------

// Returns the first non-blank character after `"key" :`, or NULL.
const TCHAR* FindValue(const TCHAR* json, const TCHAR* key)
{
    TCHAR searchKey[128];
    searchKey[0] = 0;
    if (!Str::Append(searchKey, 128, TEXT("\"")) ||
        !Str::Append(searchKey, 128, key) ||
        !Str::Append(searchKey, 128, TEXT("\""))) {
        return NULL;
    }

    const TCHAR* keyPos = wcsstr(json, searchKey);
    if (!keyPos) {
        return NULL;
    }

    const TCHAR* colonPos = wcschr(keyPos, ':');
    if (!colonPos) {
        return NULL;
    }

    const TCHAR* p = colonPos + 1;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }

    return p;
}

// Heap copy of a string value with its JSON escapes resolved; caller delete[]s.
TCHAR* ScanString(const TCHAR* json, const TCHAR* key)
{
    const TCHAR* p = FindValue(json, key);
    if (!p || *p != '"') {
        return NULL;
    }

    TCHAR* value = NULL;
    if (!JsonLite::DecodeStringLiteral(&p, &value)) {
        return NULL;
    }

    return value;
}

bool ScanInt(const TCHAR* json, const TCHAR* key, int* value)
{
    const TCHAR* p = FindValue(json, key);
    if (!p) {
        return false;
    }

    int len = 0;
    if (p[len] == '-' || p[len] == '+') {
        len++;
    }
    while (p[len] >= '0' && p[len] <= '9') {
        len++;
    }

    TCHAR token[32];
    if (!Str::CopyN(token, 32, p, len)) {
        return false;
    }

    return Str::ParseInt(token, value);
}

bool ScanBool(const TCHAR* json, const TCHAR* key, bool* value)
{
    const TCHAR* p = FindValue(json, key);
    if (!p) {
        return false;
    }

    if (wcsncmp(p, TEXT("true"), 4) == 0) {
        *value = true;
        return true;
    }
    if (wcsncmp(p, TEXT("false"), 5) == 0) {
        *value = false;
        return true;
    }

    return false;
}

// `parser` is NULL when the document did not parse strictly.
TCHAR* ReadString(const JsonLite* parser, const TCHAR* json, const TCHAR* key)
{
    if (parser) {
        return parser->GetStringAlloc(key);
    }
    return ScanString(json, key);
}

bool ReadInt(const JsonLite* parser, const TCHAR* json, const TCHAR* key, int* value)
{
    if (parser) {
        return parser->GetInt(key, value);
    }
    return ScanInt(json, key, value);
}

bool ReadBool(const JsonLite* parser, const TCHAR* json, const TCHAR* key, bool* value)
{
    if (parser) {
        return parser->GetBool(key, value);
    }
    return ScanBool(json, key, value);
}

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

void AppendStringLine(Str::Buffer& out, const TCHAR* key, const TCHAR* value, bool last)
{
    out.Append(TEXT("  "));
    out.AppendJsonString(key);
    out.Append(TEXT(": "));
    out.AppendJsonString(value ? value : TEXT(""));
    out.Append(last ? TEXT("\n") : TEXT(",\n"));
}

void AppendIntLine(Str::Buffer& out, const TCHAR* key, int value, bool last)
{
    out.Append(TEXT("  "));
    out.AppendJsonString(key);
    out.Append(TEXT(": "));
    out.AppendInt(value);
    out.Append(last ? TEXT("\n") : TEXT(",\n"));
}

void AppendBoolLine(Str::Buffer& out, const TCHAR* key, bool value, bool last)
{
    out.Append(TEXT("  "));
    out.AppendJsonString(key);
    out.Append(TEXT(": "));
    out.Append(value ? TEXT("true") : TEXT("false"));
    out.Append(last ? TEXT("\n") : TEXT(",\n"));
}

// Replaces `*field` with a heap copy of `value`.
void Assign(TCHAR** field, const TCHAR* value)
{
    TCHAR* copy = value ? Str::Dup(value) : NULL;
    if (*field) {
        delete[] *field;
    }
    *field = copy;
}

} // namespace

Config::Config()
    : m_apiBaseUrl(NULL)
    , m_deviceId(NULL)
    , m_authToken(NULL)
    , m_apiKey(NULL)
    , m_journalPath(NULL)
    , m_logLevel(NULL)
    , m_configPath(NULL)
    , m_activeBackendId(NULL)
    , m_homeboxInstanceId(NULL)
    , m_netboxInstanceId(NULL)
    , m_netboxBaseUrl(NULL)
    , m_netboxToken(NULL)
    , m_netboxAuthScheme(NULL)
    , m_syncIntervalSeconds(300) // Default 5 minutes
    , m_offlineModeEnabled(true)
    , m_scannerBeepEnabled(true)
    , m_scannerVibrateEnabled(true)
    , m_allowInsecureTls(false)
{
    InitDefaults();
}

Config::~Config()
{
    Cleanup();
}

void Config::InitDefaults()
{
    // Defaults documented in docs/DEPLOYMENT.md ("Configuration Parameters").
    SetApiBaseUrl(TEXT("http://localhost:8080/api"));
    SetDeviceId(TEXT("MC75-DEVICE-001"));
    SetAuthToken(TEXT(""));
    SetApiKey(TEXT(""));
    SetJournalPath(TEXT("\\My Documents\\hbx_journal.log"));
    SetLogLevel(TEXT("INFO"));

    // Backends. "hb" matches the tag on queue records written before entries
    // carried one, so an upgraded device recognises the work it is already
    // carrying. NetBox starts with an empty URL, which is how the controller
    // tells "no NetBox here" from "NetBox is configured".
    SetHomeboxInstanceId(TEXT("hb"));
    SetNetboxInstanceId(TEXT("nb"));
    SetActiveBackendId(TEXT("hb"));
    SetNetboxBaseUrl(TEXT(""));
    SetNetboxToken(TEXT(""));
    SetNetboxAuthScheme(TEXT("Token"));

    m_syncIntervalSeconds = 300;
    m_offlineModeEnabled = true;
    m_scannerBeepEnabled = true;
    m_scannerVibrateEnabled = true;
    m_allowInsecureTls = false;
}

void Config::Cleanup()
{
    Assign(&m_apiBaseUrl, NULL);
    Assign(&m_deviceId, NULL);
    Assign(&m_authToken, NULL);
    Assign(&m_apiKey, NULL);
    Assign(&m_journalPath, NULL);
    Assign(&m_logLevel, NULL);
    Assign(&m_configPath, NULL);
    Assign(&m_activeBackendId, NULL);
    Assign(&m_homeboxInstanceId, NULL);
    Assign(&m_netboxInstanceId, NULL);
    Assign(&m_netboxBaseUrl, NULL);
    Assign(&m_netboxToken, NULL);
    Assign(&m_netboxAuthScheme, NULL);
}

bool Config::Load(const TCHAR* configPath)
{
    if (!configPath) {
        InitDefaults();
        return false;
    }

    // Every key absent from the file takes its documented default, so a reload
    // reflects the file rather than whatever the previous load left behind.
    InitDefaults();
    Assign(&m_configPath, configPath);

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
        return true;
    }

    // Get file size
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return true;
    }

    // Read file content
    char* buffer = new char[fileSize + 1];
    DWORD bytesRead = 0;
    BOOL success = ReadFile(hFile, buffer, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    if (!success || bytesRead == 0) {
        delete[] buffer;
        return false;
    }

    buffer[bytesRead] = '\0';

    // The file is UTF-8 (docs/API_NOTES.md: "Character Encoding: UTF-8"), so it
    // is decoded rather than byte-truncated into TCHARs.
    TCHAR* jsonContent = Str::FromUtf8Alloc(buffer);
    delete[] buffer;
    if (!jsonContent) {
        return false;
    }

    JsonLite parser;
    const JsonLite* strict = parser.Parse(jsonContent) ? &parser : NULL;

    TCHAR* value = ReadString(strict, jsonContent, TEXT("apiBaseUrl"));
    if (value) {
        SetApiBaseUrl(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("deviceId"));
    if (value) {
        SetDeviceId(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("authToken"));
    if (value) {
        SetAuthToken(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("apiKey"));
    if (value) {
        SetApiKey(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("journalPath"));
    if (value) {
        SetJournalPath(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("logLevel"));
    if (value) {
        SetLogLevel(value);
        delete[] value;
    }

    // Backend instance ids come first: activeBackend names one of them, and its
    // default has to follow whatever HomeBox was actually called.
    value = ReadString(strict, jsonContent, TEXT("homeboxInstanceId"));
    if (value) {
        SetHomeboxInstanceId(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("netboxInstanceId"));
    if (value) {
        SetNetboxInstanceId(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("activeBackend"));
    if (value) {
        SetActiveBackendId(value);
        delete[] value;
    } else {
        // A file that predates the second backend selects HomeBox, whatever the
        // operator named it - never the literal default, which might name
        // nothing that is registered.
        SetActiveBackendId(m_homeboxInstanceId);
    }

    value = ReadString(strict, jsonContent, TEXT("netboxBaseUrl"));
    if (value) {
        SetNetboxBaseUrl(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("netboxToken"));
    if (value) {
        SetNetboxToken(value);
        delete[] value;
    }

    value = ReadString(strict, jsonContent, TEXT("netboxAuthScheme"));
    if (value) {
        SetNetboxAuthScheme(value);
        delete[] value;
    }

    int intValue = 0;
    if (ReadInt(strict, jsonContent, TEXT("syncIntervalSeconds"), &intValue)) {
        SetSyncIntervalSeconds(intValue);
    }

    bool boolValue = false;
    // README/DEPLOYMENT document the key as "offlineMode"; the file this class
    // writes uses "offlineModeEnabled". Both are accepted.
    if (ReadBool(strict, jsonContent, TEXT("offlineModeEnabled"), &boolValue) ||
        ReadBool(strict, jsonContent, TEXT("offlineMode"), &boolValue)) {
        m_offlineModeEnabled = boolValue;
    }

    if (ReadBool(strict, jsonContent, TEXT("scannerBeepEnabled"), &boolValue)) {
        m_scannerBeepEnabled = boolValue;
    }

    if (ReadBool(strict, jsonContent, TEXT("scannerVibrateEnabled"), &boolValue)) {
        m_scannerVibrateEnabled = boolValue;
    }

    if (ReadBool(strict, jsonContent, TEXT("allowInsecureTls"), &boolValue)) {
        m_allowInsecureTls = boolValue;
    }

    delete[] jsonContent;
    return true;
}

bool Config::LoadFromDefaultLocations()
{
    static const TCHAR* const kPaths[] = {
        TEXT("\\Program Files\\HBXClient\\hb_conf.json"),
        TEXT("\\My Documents\\hb_conf.json"),
        TEXT("\\Storage Card\\hb_conf.json")
    };
    const int kPathCount = sizeof(kPaths) / sizeof(kPaths[0]);

    for (int i = 0; i < kPathCount; i++) {
        HANDLE hFile = CreateFile(
            kPaths[i],
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            return Load(kPaths[i]);
        }
    }

    // Nothing deployed yet: run on defaults, and remember the preferred
    // location so a later Save() lands where the install expects it.
    InitDefaults();
    Assign(&m_configPath, kPaths[0]);
    return true;
}

bool Config::Save(const TCHAR* configPath)
{
    if (!configPath) {
        return false;
    }

    // Values are escaped and the buffer grows. Both matter here: a Windows path
    // is full of backslashes, and a JWT auth token runs well past a thousand
    // characters, so a fixed buffer with raw values makes the file unreadable.
    //
    // Every key Load() understands must be written back. Save() rewrites the
    // whole file from the fields it knows, and Controller::PersistAuthToken
    // calls it on the first successful authentication of every run - so a key
    // that is loaded but not saved is wiped off the device the first time the
    // operator authenticates.
    Str::Buffer json;
    json.Append(TEXT("{\n"));
    AppendStringLine(json, TEXT("activeBackend"), m_activeBackendId, false);
    AppendStringLine(json, TEXT("homeboxInstanceId"), m_homeboxInstanceId, false);
    AppendStringLine(json, TEXT("apiBaseUrl"), m_apiBaseUrl, false);
    AppendStringLine(json, TEXT("deviceId"), m_deviceId, false);
    AppendStringLine(json, TEXT("apiKey"), m_apiKey, false);
    AppendStringLine(json, TEXT("authToken"), m_authToken, false);
    AppendStringLine(json, TEXT("netboxInstanceId"), m_netboxInstanceId, false);
    AppendStringLine(json, TEXT("netboxBaseUrl"), m_netboxBaseUrl, false);
    AppendStringLine(json, TEXT("netboxToken"), m_netboxToken, false);
    AppendStringLine(json, TEXT("netboxAuthScheme"), m_netboxAuthScheme, false);
    AppendBoolLine(json, TEXT("allowInsecureTls"), m_allowInsecureTls, false);
    AppendIntLine(json, TEXT("syncIntervalSeconds"), m_syncIntervalSeconds, false);
    AppendStringLine(json, TEXT("journalPath"), m_journalPath, false);
    AppendStringLine(json, TEXT("logLevel"), m_logLevel, false);
    AppendBoolLine(json, TEXT("scannerBeepEnabled"), m_scannerBeepEnabled, false);
    AppendBoolLine(json, TEXT("scannerVibrateEnabled"), m_scannerVibrateEnabled, false);
    AppendBoolLine(json, TEXT("offlineModeEnabled"), m_offlineModeEnabled, true);
    json.Append(TEXT("}\n"));

    if (json.Failed()) {
        return false;
    }

    // The file is UTF-8 on the wire and on disk, so encode rather than
    // truncating each TCHAR to a byte.
    char* utf8 = Str::ToUtf8Alloc(json.Get());
    if (!utf8) {
        return false;
    }
    DWORD byteCount = (DWORD)(Str::Utf8Size(json.Get()) - 1);

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
        delete[] utf8;
        return false;
    }

    DWORD bytesWritten = 0;
    BOOL success = WriteFile(hFile, utf8, byteCount, &bytesWritten, NULL);

    delete[] utf8;
    CloseHandle(hFile);

    return (success != FALSE) && (bytesWritten == byteCount);
}

const TCHAR* Config::GetConfigPath() const
{
    return m_configPath;
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

const TCHAR* Config::GetApiKey() const
{
    return m_apiKey;
}

const TCHAR* Config::GetJournalPath() const
{
    return m_journalPath;
}

const TCHAR* Config::GetLogLevel() const
{
    return m_logLevel;
}

int Config::GetSyncIntervalSeconds() const
{
    return m_syncIntervalSeconds;
}

bool Config::IsOfflineModeEnabled() const
{
    return m_offlineModeEnabled;
}

bool Config::IsScannerBeepEnabled() const
{
    return m_scannerBeepEnabled;
}

bool Config::IsScannerVibrateEnabled() const
{
    return m_scannerVibrateEnabled;
}

const TCHAR* Config::GetActiveBackendId() const
{
    return m_activeBackendId;
}

const TCHAR* Config::GetHomeboxInstanceId() const
{
    return m_homeboxInstanceId;
}

const TCHAR* Config::GetNetboxInstanceId() const
{
    return m_netboxInstanceId;
}

const TCHAR* Config::GetNetboxBaseUrl() const
{
    return m_netboxBaseUrl;
}

const TCHAR* Config::GetNetboxToken() const
{
    return m_netboxToken;
}

const TCHAR* Config::GetNetboxAuthScheme() const
{
    return m_netboxAuthScheme;
}

bool Config::IsInsecureTlsAllowed() const
{
    return m_allowInsecureTls;
}

void Config::SetApiBaseUrl(const TCHAR* url)
{
    Assign(&m_apiBaseUrl, url);
}

void Config::SetDeviceId(const TCHAR* deviceId)
{
    Assign(&m_deviceId, deviceId);
}

void Config::SetAuthToken(const TCHAR* token)
{
    Assign(&m_authToken, token);
}

void Config::SetApiKey(const TCHAR* apiKey)
{
    Assign(&m_apiKey, apiKey);
}

void Config::SetJournalPath(const TCHAR* path)
{
    Assign(&m_journalPath, path);
}

void Config::SetLogLevel(const TCHAR* level)
{
    Assign(&m_logLevel, level);
}

void Config::SetSyncIntervalSeconds(int seconds)
{
    m_syncIntervalSeconds = seconds;
}

void Config::SetOfflineModeEnabled(bool enabled)
{
    m_offlineModeEnabled = enabled;
}

void Config::SetScannerBeepEnabled(bool enabled)
{
    m_scannerBeepEnabled = enabled;
}

void Config::SetScannerVibrateEnabled(bool enabled)
{
    m_scannerVibrateEnabled = enabled;
}

void Config::SetActiveBackendId(const TCHAR* instanceId)
{
    Assign(&m_activeBackendId, instanceId);
}

void Config::SetHomeboxInstanceId(const TCHAR* instanceId)
{
    Assign(&m_homeboxInstanceId, instanceId);
}

void Config::SetNetboxInstanceId(const TCHAR* instanceId)
{
    Assign(&m_netboxInstanceId, instanceId);
}

void Config::SetNetboxBaseUrl(const TCHAR* url)
{
    Assign(&m_netboxBaseUrl, url);
}

void Config::SetNetboxToken(const TCHAR* token)
{
    Assign(&m_netboxToken, token);
}

void Config::SetNetboxAuthScheme(const TCHAR* scheme)
{
    Assign(&m_netboxAuthScheme, scheme);
}

void Config::SetAllowInsecureTls(bool allowed)
{
    m_allowInsecureTls = allowed;
}

} // namespace HBX
