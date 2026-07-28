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
// File reading
// ---------------------------------------------------------------------------

enum ReadResult {
    READ_OK,      // *out holds the decoded document; caller delete[]s it
    READ_ABSENT,  // nothing deployed at that path, or an empty file
    READ_FAILED   // a file is there but could not be read or decoded
};

/**
 * Reads `path` and decodes it from UTF-8 into TCHAR text. `maxBytes` of 0
 * accepts any size; a larger file is reported as READ_FAILED rather than
 * allocated.
 */
ReadResult ReadConfigFile(const TCHAR* path, DWORD maxBytes, TCHAR** out)
{
    *out = NULL;

    HANDLE hFile = CreateFile(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return READ_ABSENT;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return READ_ABSENT;
    }

    if (maxBytes != 0 && fileSize > maxBytes) {
        CloseHandle(hFile);
        return READ_FAILED;
    }

    char* buffer = new char[fileSize + 1];
    if (!buffer) {
        CloseHandle(hFile);
        return READ_FAILED;
    }

    DWORD bytesRead = 0;
    BOOL success = ReadFile(hFile, buffer, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    if (!success || bytesRead == 0) {
        delete[] buffer;
        return READ_FAILED;
    }

    buffer[bytesRead] = '\0';

    // The file is UTF-8 (docs/API_NOTES.md: "Character Encoding: UTF-8"), so it
    // is decoded rather than byte-truncated into TCHARs.
    TCHAR* text = Str::FromUtf8Alloc(buffer);
    delete[] buffer;

    if (!text) {
        return READ_FAILED;
    }

    *out = text;
    return READ_OK;
}

// ---------------------------------------------------------------------------
// Foreign keys
//
// Save() rewrites the document from the fields this class owns, so any other
// top-level key - a site marker an installer drops in, the annotated _readme
// block shipped with the template - would be deleted the first time the file is
// written. Everything not named below is carried through instead.
//
// "offlineMode" is listed even though Save() never writes it: it is the older
// spelling of offlineModeEnabled and Load() still honours it, so carrying it
// through would leave two keys claiming one setting, with the stale one winning
// on any reader that looks at it first.
// ---------------------------------------------------------------------------

const TCHAR* const kKnownKeys[] = {
    TEXT("activeBackend"),
    TEXT("homeboxInstanceId"),
    TEXT("apiBaseUrl"),
    TEXT("deviceId"),
    TEXT("apiKey"),
    TEXT("authToken"),
    TEXT("netboxInstanceId"),
    TEXT("netboxBaseUrl"),
    TEXT("netboxToken"),
    TEXT("netboxAuthScheme"),
    TEXT("allowInsecureTls"),
    TEXT("syncIntervalSeconds"),
    TEXT("journalPath"),
    TEXT("logLevel"),
    TEXT("scannerBeepEnabled"),
    TEXT("scannerVibrateEnabled"),
    TEXT("offlineModeEnabled"),
    TEXT("offlineMode")
};

// A file this large was not written by an operator, and Save() runs on the
// authentication path - re-reading and re-parsing it there is not worth an
// allocation the device may not be able to make.
const DWORD kMaxPreservedFileBytes = 256UL * 1024UL;

bool IsKnownKey(const TCHAR* key)
{
    if (!key) {
        return true; // nothing addressable, so nothing to carry through
    }

    const int count = (int)(sizeof(kKnownKeys) / sizeof(kKnownKeys[0]));
    for (int i = 0; i < count; i++) {
        if (lstrcmp(key, kKnownKeys[i]) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * The keys of an existing document that Config does not own, held as name plus
 * already-serialized value until the replacement document is assembled. The
 * values are serialized up front because how many of them survive decides where
 * the last comma goes, and a half-written list would be invalid JSON.
 */
class ForeignKeys {
public:
    ForeignKeys() : m_names(NULL), m_values(NULL), m_count(0) {}
    ~ForeignKeys() { Release(); }

    /**
     * `parser` must outlive this collection: the names point into its tree
     * rather than being copied, which keeps a large _readme block from being
     * duplicated in memory on a device that has little of it.
     */
    void Collect(const JsonLite& parser);

    int Count() const { return m_count; }
    const TCHAR* Name(int index) const { return m_names[index]; }
    const TCHAR* Value(int index) const { return m_values[index]; }

private:
    void Release();

    const TCHAR** m_names;
    TCHAR** m_values;
    int m_count;

    // Not copyable: the class owns raw allocations.
    ForeignKeys(const ForeignKeys&);
    ForeignKeys& operator=(const ForeignKeys&);
};

void ForeignKeys::Collect(const JsonLite& parser)
{
    Release();

    int members = parser.GetMemberCount();
    if (members <= 0) {
        return;
    }

    m_names = new const TCHAR*[members];
    m_values = new TCHAR*[members];
    if (!m_names || !m_values) {
        Release();
        return;
    }

    for (int i = 0; i < members; i++) {
        const TCHAR* name = parser.GetMemberName(i);
        if (IsKnownKey(name)) {
            continue;
        }

        TCHAR* value = parser.GetMemberJson(i);
        if (!value) {
            // One value that will not serialize costs that key, not the file.
            continue;
        }

        m_names[m_count] = name;
        m_values[m_count] = value;
        m_count++;
    }
}

void ForeignKeys::Release()
{
    for (int i = 0; i < m_count; i++) {
        delete[] m_values[i];
    }

    delete[] m_names;
    delete[] m_values;
    m_names = NULL;
    m_values = NULL;
    m_count = 0;
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

// `valueJson` is already JSON text and is emitted exactly as it was read.
void AppendRawLine(Str::Buffer& out, const TCHAR* key, const TCHAR* valueJson, bool last)
{
    out.Append(TEXT("  "));
    out.AppendJsonString(key);
    out.Append(TEXT(": "));
    out.Append(valueJson);
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

    // No size limit here: whatever is deployed has to be readable, and a key
    // this class needs may sit past any bound worth guessing.
    TCHAR* jsonContent = NULL;
    ReadResult status = ReadConfigFile(configPath, 0, &jsonContent);
    if (status == READ_ABSENT) {
        // File doesn't exist, use defaults
        return true;
    }
    if (status != READ_OK) {
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

    // Save() rewrites the whole document from the fields it knows, and
    // Controller::PersistAuthToken calls it on the first successful
    // authentication of every run - so anything the new document omits is off
    // the device on day one, without anyone having opened a settings screen.
    // Two things follow from that: every key Load() understands must be written
    // back, and every key it does not understand must be carried through.
    //
    // Carrying keys through is best-effort by design. A file that is absent,
    // larger than kMaxPreservedFileBytes, or too broken to parse (one
    // hand-edited trailing comma is enough, and the tolerant scanner Load()
    // falls back to can only find keys it already knows the names of) leaves
    // nothing to carry, and the save then goes ahead with the known fields
    // alone: losing the operator's settings because a foreign key could not be
    // recovered would be the worse failure of the two.
    //
    // `parser` is declared before `foreign` so it is destroyed after it - the
    // collected names point into its tree.
    TCHAR* existing = NULL;
    JsonLite parser;
    ForeignKeys foreign;
    if (ReadConfigFile(configPath, kMaxPreservedFileBytes, &existing) == READ_OK) {
        // Parse() keeps its own copy of the text, so the decode buffer goes
        // back straight away rather than being held for the whole save.
        bool parsed = parser.Parse(existing);
        delete[] existing;

        if (parsed && parser.IsObject()) {
            foreign.Collect(parser);
        }
    }

    // Values are escaped and the buffer grows. Both matter here: a Windows path
    // is full of backslashes, and a JWT auth token runs well past a thousand
    // characters, so a fixed buffer with raw values makes the file unreadable.
    //
    // Known keys keep their fixed order and foreign ones follow, rather than
    // being put back where they were found: with both blocks in a stable order,
    // the file a device writes differs from the previous one only where a
    // setting actually changed.
    const bool haveForeign = (foreign.Count() > 0);

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
    AppendBoolLine(json, TEXT("offlineModeEnabled"), m_offlineModeEnabled, !haveForeign);

    for (int i = 0; i < foreign.Count(); i++) {
        AppendRawLine(json, foreign.Name(i), foreign.Value(i),
                      i == foreign.Count() - 1);
    }

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
