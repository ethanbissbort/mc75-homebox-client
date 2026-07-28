#include "../include/HbClient.hpp"
#include "../include/StrUtil.hpp"
#include <string.h>
#include <wchar.h>

namespace HBX {

namespace {

// The one HTTP status this client acts on itself: the server has retired the
// token, which is not the same failure as "no such item" even though both used
// to reach the caller as a bare false.
const int kHttpUnauthorized = 401;

// Stable token written into queue records and matched when they are routed
// back. Distinct from the instance id, which the operator sets per server.
const TCHAR* const kBackendKind = TEXT("hb");

// Shown in the title bar and against queued entries in the queue view.
const TCHAR* const kBackendDisplayName = TEXT("HomeBox");

// The id a client carries until it is configured. It doubles as the id an
// untagged queue record from an older build resolves to, so it must stay "hb".
const TCHAR* const kDefaultInstanceId = TEXT("hb");

/**
 * Builds "<prefix><id><suffix>" into a bounded buffer.
 *
 * Returns false instead of truncating: ids and barcodes come from the server
 * or from a queued journal payload and are not length-limited, and a shortened
 * id addresses a different record (or none) on the server.
 */
bool BuildEndpoint(TCHAR* dst, int cap, const TCHAR* prefix, const TCHAR* id, const TCHAR* suffix)
{
    if (!Str::Copy(dst, cap, prefix)) {
        return false;
    }
    if (!Str::Append(dst, cap, id)) {
        return false;
    }
    if (suffix && !Str::Append(dst, cap, suffix)) {
        return false;
    }
    return true;
}

/**
 * Extracts the value of the "token" member of an auth response.
 *
 * Returns a heap copy (caller delete[]s) only when the value really is a
 * non-empty JSON string. A null, a number or "" must not be accepted: doing so
 * left the client convinced it was authenticated while carrying a garbage
 * bearer token, and every later call failed with an unhelpful 401.
 */
TCHAR* ExtractAuthToken(const TCHAR* response)
{
    if (!response) {
        return NULL;
    }

    const TCHAR* p = wcsstr(response, TEXT("\"token\""));
    if (!p) {
        return NULL;
    }
    p += 7; // past the quoted key

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':') {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return NULL; // not a string value
    }
    p++;

    const TCHAR* end = p;
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && end[1] != '\0') {
            end++; // an escaped quote does not end the string
        }
        end++;
    }
    if (*end != '"') {
        return NULL; // unterminated string
    }

    int tokenLen = (int)(end - p);
    if (tokenLen == 0) {
        return NULL;
    }
    return Str::DupN(p, tokenLen);
}

} // namespace

HbClient::HbClient()
    : m_httpClient(NULL)
    , m_baseUrl(NULL)
    , m_authToken(NULL)
    , m_deviceId(NULL)
    , m_apiKey(NULL)
    , m_authenticated(false)
    , m_lastStatusCode(0)
    , m_matchCount(0)
{
    Str::Copy(m_instanceId, INSTANCE_ID_MAX, kDefaultInstanceId);
    m_httpClient = new HttpClient();
}

HbClient::~HbClient()
{
    if (m_httpClient) {
        delete m_httpClient;
    }
    if (m_baseUrl) {
        delete[] m_baseUrl;
    }
    if (m_authToken) {
        delete[] m_authToken;
    }
    if (m_deviceId) {
        delete[] m_deviceId;
    }
    if (m_apiKey) {
        delete[] m_apiKey;
    }
}

const TCHAR* HbClient::GetKind() const
{
    return kBackendKind;
}

const TCHAR* HbClient::GetDisplayName() const
{
    return kBackendDisplayName;
}

const TCHAR* HbClient::GetInstanceId() const
{
    return m_instanceId;
}

void HbClient::SetInstanceId(const TCHAR* instanceId)
{
    if (!instanceId || instanceId[0] == (TCHAR)'\0') {
        // Never leave the id empty: an empty tag would make every queue record
        // this client writes unroutable.
        Str::Copy(m_instanceId, INSTANCE_ID_MAX, kDefaultInstanceId);
        return;
    }

    Str::Copy(m_instanceId, INSTANCE_ID_MAX, instanceId);
}

bool HbClient::SetCredentials(const TCHAR* deviceId, const TCHAR* apiKey)
{
    if (!deviceId || !apiKey) {
        return false;
    }

    // Both copies are taken before either old value is released: on a device
    // this short of heap, failing half way through would leave the client
    // holding one half of a credential pair it can never authenticate with.
    TCHAR* deviceCopy = Str::Dup(deviceId);
    if (!deviceCopy) {
        return false;
    }

    TCHAR* keyCopy = Str::Dup(apiKey);
    if (!keyCopy) {
        delete[] deviceCopy;
        return false;
    }

    if (m_deviceId) {
        delete[] m_deviceId;
    }
    m_deviceId = deviceCopy;

    if (m_apiKey) {
        delete[] m_apiKey;
    }
    m_apiKey = keyCopy;

    return true;
}

bool HbClient::Authenticate(const TCHAR* deviceId, const TCHAR* apiKey)
{
    // Copy first, then authenticate from the copies. Handing the caller's
    // pointers straight through would break the moment the caller passed
    // m_deviceId back in, which is exactly what the no-argument form does.
    if (!SetCredentials(deviceId, apiKey)) {
        return false;
    }

    return Authenticate();
}

bool HbClient::Authenticate()
{
    if (!m_deviceId || !m_apiKey) {
        return false;
    }

    // Drop any previous session first, so the request that asks for a new
    // token does not carry the stale bearer header.
    m_authenticated = false;
    if (m_authToken) {
        delete[] m_authToken;
        m_authToken = NULL;
    }

    // Build authentication request body. Both values are JSON-escaped and the
    // buffer grows to fit: hb_conf.json allows credentials several hundred
    // characters long, which no longer has to match a fixed request buffer.
    Str::Buffer requestBody;
    requestBody.AppendChar((TCHAR)'{');
    requestBody.AppendJsonPair(TEXT("deviceId"), m_deviceId);
    requestBody.AppendChar((TCHAR)',');
    requestBody.AppendJsonPair(TEXT("apiKey"), m_apiKey);
    requestBody.AppendChar((TCHAR)'}');
    if (requestBody.Failed()) {
        return false;
    }

    // Make authentication request
    TCHAR* response = NULL;
    if (!MakeApiRequest(TEXT("POST"), TEXT("/api/v1/auth/device"), requestBody.Get(), &response)) {
        return false;
    }

    // Parse token from response; expected format: {"token": "..."}
    TCHAR* token = ExtractAuthToken(response);
    delete[] response;

    if (!token) {
        return false;
    }

    m_authToken = token;
    m_authenticated = true;
    return true;
}

bool HbClient::IsAuthenticated() const
{
    return m_authenticated;
}

bool HbClient::SessionIsRenewable() const
{
    // The token is minted from credentials this client holds, so a 401 means
    // "expired", not "misconfigured" -- one re-authenticate and retry is worth
    // the round trip.
    return true;
}

void HbClient::Logout()
{
    // The recorded credentials are deliberately kept: this ends a session, it
    // does not unconfigure the client, and Authenticate() has to be able to
    // mint a new token afterwards without the caller re-supplying them.
    m_authenticated = false;
    if (m_authToken) {
        delete[] m_authToken;
        m_authToken = NULL;
    }
}

const TCHAR* HbClient::GetAuthToken() const
{
    return m_authToken;
}

void HbClient::SetAuthToken(const TCHAR* deviceId, const TCHAR* token)
{
    if (!token || token[0] == (TCHAR)'\0') {
        Logout();
        return;
    }

    // Both copies are taken before anything is released: on a device this
    // short of heap, a failed allocation half-way through would otherwise leave
    // the client with no token at all instead of the one it already had.
    TCHAR* tokenCopy = Str::Dup(token);
    if (!tokenCopy) {
        return;
    }

    TCHAR* deviceCopy = NULL;
    if (deviceId) {
        deviceCopy = Str::Dup(deviceId);
        if (!deviceCopy) {
            delete[] tokenCopy;
            return;
        }
    }

    if (m_authToken) {
        delete[] m_authToken;
    }
    m_authToken = tokenCopy;

    if (deviceCopy) {
        if (m_deviceId) {
            delete[] m_deviceId;
        }
        m_deviceId = deviceCopy;
    }

    m_authenticated = true;

    // A restored session has never been through MakeApiRequest, so install the
    // bearer header now rather than leaving the header list from whatever the
    // previous session sent.
    SetAuthHeaders();
}

int HbClient::GetLastStatusCode() const
{
    return m_lastStatusCode;
}

void HbClient::SummarizeItem(const Models::Item* item, Models::AssetSummary* out)
{
    if (!out) {
        return;
    }

    out->Clear();
    if (!item) {
        return;
    }

    out->SetSource(kBackendDisplayName);
    out->SetId(item->GetId());
    out->SetTitle(item->GetName());
    out->SetSubtitle(item->GetCategory());
    out->SetCode(item->GetBarcode());

    // Left empty on purpose, and SupportsStatus() reports false to match: a
    // HomeBox item has no status field, so the UI hides the status action
    // rather than offering one that cannot be applied.
    out->SetStatus(TEXT(""));

    TCHAR quantity[24];
    quantity[0] = 0;
    Str::AppendInt(quantity, (int)(sizeof(quantity) / sizeof(TCHAR)), item->GetQuantity());

    // Row order is the order the operator reads them in: the barcode confirms
    // the right record came back from the scan, then the quantity (the field a
    // stock count actually changes), then where it lives, then the prose.
    //
    // Empty fields still get a row. A blank Location means "this item is not
    // placed anywhere", which is a finding the operator is looking for; omitting
    // the row would make it look like the screen had merely run out of detail.
    //
    // Location is the raw HomeBox location id, not a resolved name: turning it
    // into a name costs a second request per scan, and on a GPRS link that is a
    // visible pause on the hot path.
    out->AddField(TEXT("Barcode"), item->GetBarcode());
    out->AddField(TEXT("Quantity"), quantity);
    out->AddField(TEXT("Location"), item->GetLocationId());
    out->AddField(TEXT("Description"), item->GetDescription());
}

bool HbClient::LookupByCode(const TCHAR* code, Models::AssetSummary* out)
{
    // Cleared first so a caller that reads GetMatchCount() after a failed
    // lookup cannot pick up the count from the previous scan.
    m_matchCount = 0;

    if (!code || !out) {
        return false;
    }

    Models::Item item;
    if (!GetItem(code, &item)) {
        return false;
    }

    SummarizeItem(&item, &m_lastMatch);
    m_matchCount = 1;

    *out = m_lastMatch;
    return true;
}

int HbClient::GetMatchCount() const
{
    return m_matchCount;
}

bool HbClient::GetMatch(int index, Models::AssetSummary* out)
{
    if (!out || index < 0 || index >= m_matchCount) {
        return false;
    }

    *out = m_lastMatch;
    return true;
}

bool HbClient::SupportsStatus() const
{
    return false;
}

int HbClient::GetStatusChoiceCount() const
{
    return 0;
}

const TCHAR* HbClient::GetStatusChoice(int index) const
{
    // No choices exist, so every index is out of range.
    (void)index;
    return NULL;
}

ReplayResult HbClient::Replay(const TCHAR* type, const TCHAR* data)
{
    if (!type) {
        return REPLAY_SKIPPED;
    }
    if (!data) {
        data = TEXT("");
    }

    if (wcscmp(type, TEXT("ITEM_SCAN")) == 0) {
        return ReplayScan(data);
    }
    if (wcscmp(type, TEXT("ITEM_UPDATE")) == 0) {
        return ReplayUpdate(data);
    }

    // Addressed to some other inventory system. Reporting it as a failure would
    // pin the sync status at "failed" for as long as the entry sits there.
    return REPLAY_SKIPPED;
}

ReplayResult HbClient::ReplayScan(const TCHAR* data)
{
    // DATA is "SCAN:<barcode>" or "SCANLOC:<barcodeLength>:<barcode><locationId>".
    // A barcode can legally contain ':' (Code 128 and QR both encode it), so the
    // location-carrying form states the barcode length instead of relying on a
    // delimiter the barcode content could collide with.
    const TCHAR* barcode = NULL;
    const TCHAR* locationId = NULL;
    TCHAR* barcodeCopy = NULL; // only the length-prefixed form needs one

    if (wcsncmp(data, TEXT("SCANLOC:"), 8) == 0) {
        const TCHAR* lenStart = data + 8;
        const TCHAR* lenEnd = wcschr(lenStart, (TCHAR)':');
        if (!lenEnd) {
            return REPLAY_RETRY;
        }

        TCHAR lenText[12];
        int barcodeLen = 0;
        if (!Str::CopyN(lenText, 12, lenStart, (int)(lenEnd - lenStart)) ||
            !Str::ParseInt(lenText, &barcodeLen)) {
            return REPLAY_RETRY;
        }

        const TCHAR* body = lenEnd + 1;
        if (barcodeLen <= 0 || barcodeLen > Str::Length(body)) {
            return REPLAY_RETRY;
        }

        barcodeCopy = Str::DupN(body, barcodeLen);
        if (!barcodeCopy) {
            return REPLAY_RETRY;
        }
        barcode = barcodeCopy;
        locationId = body + barcodeLen;
    } else if (wcsncmp(data, TEXT("SCAN:"), 5) == 0) {
        barcode = data + 5;
    } else {
        return REPLAY_RETRY;
    }

    if (barcode[0] == (TCHAR)'\0') {
        delete[] barcodeCopy;
        return REPLAY_RETRY;
    }

    // Only a real success clears the entry from the queue; anything else leaves
    // it queued to be retried on the next Sync().
    Models::Item item;
    bool ok = GetItem(barcode, &item);

    // If a location was captured with the scan, push the move too.
    if (ok && locationId && locationId[0] != (TCHAR)'\0') {
        ok = UpdateItemLocation(barcode, locationId);
    }

    delete[] barcodeCopy;
    return ok ? REPLAY_SENT : REPLAY_RETRY;
}

ReplayResult HbClient::ReplayUpdate(const TCHAR* data)
{
    // DATA format: "UPDATE:<json>" where <json> is a complete Item JSON object,
    // e.g. UPDATE:{"id":"42","barcode":"123","name":"Widget","quantity":3}
    if (wcsncmp(data, TEXT("UPDATE:"), 7) != 0) {
        return REPLAY_RETRY;
    }

    const TCHAR* json = data + 7;
    if (*json == (TCHAR)'\0') {
        return REPLAY_RETRY;
    }

    Models::Item item;
    if (!item.FromJson(json)) {
        return REPLAY_RETRY;
    }

    return UpdateItem(&item) ? REPLAY_SENT : REPLAY_RETRY;
}

bool HbClient::GetItem(const TCHAR* barcode, Models::Item* item)
{
    if (!barcode || !item) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    if (!BuildEndpoint(endpoint, sizeof(endpoint) / sizeof(TCHAR),
                       TEXT("/api/v1/items/"), barcode, NULL)) {
        return false;
    }

    // Make GET request
    TCHAR* response = NULL;
    if (!MakeApiRequest(TEXT("GET"), endpoint, NULL, &response)) {
        return false;
    }

    // Parse JSON response into Item object
    bool parsed = item->FromJson(response);
    delete[] response;

    return parsed;
}

bool HbClient::UpdateItemLocation(const TCHAR* barcode, const TCHAR* locationId)
{
    if (!barcode || !locationId) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    if (!BuildEndpoint(endpoint, sizeof(endpoint) / sizeof(TCHAR),
                       TEXT("/api/v1/items/"), barcode, TEXT("/location"))) {
        return false;
    }

    // Build request body
    Str::Buffer requestBody;
    requestBody.AppendChar((TCHAR)'{');
    requestBody.AppendJsonPair(TEXT("locationId"), locationId);
    requestBody.AppendChar((TCHAR)'}');
    if (requestBody.Failed()) {
        return false;
    }

    // Make PATCH request (using PUT as fallback)
    return MakeApiRequest(TEXT("PUT"), endpoint, requestBody.Get(), NULL);
}

bool HbClient::CreateItem(const Models::Item* item)
{
    if (!item || !item->IsValid()) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Serialize item to JSON
    TCHAR* requestBody = item->ToJson();
    if (!requestBody) {
        return false;
    }

    // Make POST request
    bool success = MakeApiRequest(TEXT("POST"), TEXT("/api/v1/items"), requestBody, NULL);

    // Cleanup
    delete[] requestBody;

    return success;
}

bool HbClient::UpdateItem(const Models::Item* item)
{
    if (!item || !item->IsValid() || !item->GetId()) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    if (!BuildEndpoint(endpoint, sizeof(endpoint) / sizeof(TCHAR),
                       TEXT("/api/v1/items/"), item->GetId(), NULL)) {
        return false;
    }

    // Serialize item to JSON
    TCHAR* requestBody = item->ToJson();
    if (!requestBody) {
        return false;
    }

    // Make PUT request
    bool success = MakeApiRequest(TEXT("PUT"), endpoint, requestBody, NULL);

    // Cleanup
    delete[] requestBody;

    return success;
}

bool HbClient::GetLocation(const TCHAR* locationId, Models::Location* location)
{
    if (!locationId || !location) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    if (!BuildEndpoint(endpoint, sizeof(endpoint) / sizeof(TCHAR),
                       TEXT("/api/v1/locations/"), locationId, NULL)) {
        return false;
    }

    // Make GET request
    TCHAR* response = NULL;
    if (!MakeApiRequest(TEXT("GET"), endpoint, NULL, &response)) {
        return false;
    }

    // Parse JSON response into Location object
    bool parsed = location->FromJson(response);
    delete[] response;

    return parsed;
}

bool HbClient::GetAllLocations(Models::Location** locations, int* count)
{
    if (!locations || !count) {
        return false;
    }

    *locations = NULL;
    *count = 0;

    if (!m_authenticated) {
        return false;
    }

    // Make GET request. The array response is kept on the heap: a location
    // list can be arbitrarily long and must not be sized by a stack buffer.
    TCHAR* response = NULL;
    if (!MakeApiRequest(TEXT("GET"), TEXT("/api/v1/locations"), NULL, &response)) {
        return false;
    }

    // Parse JSON array response
    // Expected format: [{"id":"1",...}, {"id":"2",...}]

    // Count number of locations in array (count opening braces after first '[')
    int locationCount = 0;
    const TCHAR* ptr = response;
    bool inArray = false;
    while (*ptr) {
        if (*ptr == '[') {
            inArray = true;
        } else if (inArray && *ptr == '{') {
            locationCount++;
        }
        ptr++;
    }

    if (locationCount == 0) {
        delete[] response;
        return true; // Empty array is valid
    }

    // Allocate array for locations
    Models::Location* locArray = new Models::Location[locationCount];
    if (!locArray) {
        delete[] response;
        return false;
    }
    int currentLoc = 0;

    // Parse each location object
    ptr = wcsstr(response, TEXT("["));
    if (ptr) {
        ptr++; // Skip '['

        while (*ptr && currentLoc < locationCount) {
            // Find start of object
            const TCHAR* objStart = wcsstr(ptr, TEXT("{"));
            if (!objStart) break;

            // Find end of object (simple approach - find matching '}')
            const TCHAR* objEnd = objStart;
            int braceCount = 0;
            while (*objEnd) {
                if (*objEnd == '{') braceCount++;
                if (*objEnd == '}') {
                    braceCount--;
                    if (braceCount == 0) break;
                }
                objEnd++;
            }

            if (*objEnd == '}') {
                // Extract object JSON
                int objLen = (int)(objEnd - objStart) + 1;
                TCHAR* objJson = Str::DupN(objStart, objLen);
                if (!objJson) {
                    break;
                }

                // Parse into location object; only keep it if it is valid so
                // callers never receive half-parsed / empty Location entries.
                if (locArray[currentLoc].FromJson(objJson)) {
                    currentLoc++;
                }
                delete[] objJson;

                ptr = objEnd + 1;
            } else {
                break;
            }
        }
    }

    delete[] response;

    // If nothing valid was parsed, hand back an empty result rather than an
    // array of default-constructed (invalid) Location objects.
    if (currentLoc == 0) {
        delete[] locArray;
        *locations = NULL;
        *count = 0;
        return true;
    }

    *locations = locArray;
    *count = currentLoc;

    return true;
}

bool HbClient::SyncPendingTransactions()
{
    // Empty batch (a device heartbeat / "nothing pending" sync).
    return SyncPendingTransactions(NULL, 0);
}

bool HbClient::SyncPendingTransactions(const TCHAR* const* transactions, int count)
{
    if (!m_authenticated) {
        return false;
    }
    if (count < 0) {
        count = 0;
    }

    // Build the batch body:
    //   {"deviceId":"<id>","transactions":["<t0>","<t1>",...]}
    // Every value goes through the JSON escaper, including the device id: it
    // comes from hb_conf.json and a quote or backslash in it used to produce a
    // body the server could only reject.
    Str::Buffer body;
    body.AppendChar((TCHAR)'{');
    body.AppendJsonPair(TEXT("deviceId"), m_deviceId ? m_deviceId : TEXT(""));
    body.Append(TEXT(",\"transactions\":["));

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            body.AppendChar((TCHAR)',');
        }
        body.AppendJsonString((transactions && transactions[i]) ? transactions[i] : TEXT(""));
    }

    body.Append(TEXT("]}"));
    if (body.Failed()) {
        return false;
    }

    return MakeApiRequest(TEXT("POST"), TEXT("/api/v1/sync"), body.Get(), NULL);
}

void HbClient::SetBaseUrl(const TCHAR* baseUrl)
{
    if (m_baseUrl) {
        delete[] m_baseUrl;
    }
    if (baseUrl) {
        m_baseUrl = Str::Dup(baseUrl);
    } else {
        m_baseUrl = NULL;
    }
}

const TCHAR* HbClient::GetBaseUrl() const
{
    return m_baseUrl;
}

void HbClient::SetRequestTimeout(DWORD timeoutMs)
{
    if (m_httpClient) {
        m_httpClient->SetTimeout(timeoutMs);
    }
}

bool HbClient::MakeApiRequest(const TCHAR* method, const TCHAR* endpoint, const TCHAR* body, TCHAR** response)
{
    if (response) {
        *response = NULL;
    }
    if (!m_httpClient || !m_baseUrl || !method || !endpoint) {
        return false;
    }

    // Build full URL. Base URL (configuration) and endpoint (item ids) are both
    // variable length, so the URL grows to fit instead of being formatted into
    // a fixed buffer that wsprintf would overrun or silently cut short.
    Str::Buffer fullUrl;
    fullUrl.Append(m_baseUrl);
    fullUrl.Append(endpoint);
    if (fullUrl.Failed()) {
        return false;
    }

    // Set authentication headers
    SetAuthHeaders();

    // Start from "no answer" so a caller inspecting GetLastStatusCode() after a
    // failed request cannot read the status of the previous one.
    m_lastStatusCode = 0;

    // Make HTTP request
    HttpClient::HttpResponse httpResponse;
    bool success = false;

    if (lstrcmp(method, TEXT("GET")) == 0) {
        success = m_httpClient->Get(fullUrl.Get(), &httpResponse);
    } else if (lstrcmp(method, TEXT("POST")) == 0) {
        success = m_httpClient->Post(fullUrl.Get(), body, &httpResponse);
    } else if (lstrcmp(method, TEXT("PUT")) == 0) {
        success = m_httpClient->Put(fullUrl.Get(), body, &httpResponse);
    } else if (lstrcmp(method, TEXT("DELETE")) == 0) {
        success = m_httpClient->Delete(fullUrl.Get(), &httpResponse);
    }

    m_lastStatusCode = httpResponse.statusCode;

    // The session has been retired server-side (an expired token, or one issued
    // to a device that has since been revoked). Dropping it here is what keeps
    // a stale token from turning every later lookup into a permanent "not
    // found": the owner sees IsAuthenticated() go false and can re-authenticate
    // once and retry. Done before the transport check because a 401 whose body
    // arrived truncated is still a 401.
    if (m_lastStatusCode == kHttpUnauthorized) {
        m_authenticated = false;
        if (m_authToken) {
            delete[] m_authToken;
            m_authToken = NULL;
        }
    }

    if (!success) {
        if (httpResponse.body) {
            delete[] httpResponse.body;
        }
        return false;
    }

    // Check status code (200-299 is success)
    if (httpResponse.statusCode < 200 || httpResponse.statusCode >= 300) {
        if (httpResponse.body) {
            delete[] httpResponse.body;
        }
        return false;
    }

    // Hand the complete body to the caller; no truncation, no second copy.
    TCHAR* payload = httpResponse.body;
    httpResponse.body = NULL;
    if (!payload) {
        payload = Str::Dup(TEXT(""));
        if (!payload) {
            return false;
        }
    }

    if (response) {
        *response = payload;
    } else {
        delete[] payload;
    }

    return true;
}

void HbClient::SetAuthHeaders()
{
    if (!m_httpClient) {
        return;
    }

    // Clear existing headers
    m_httpClient->ClearHeaders();

    // Set standard headers. Bodies go on the wire as UTF-8 on both transports,
    // so the charset is stated explicitly.
    m_httpClient->AddHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
    m_httpClient->AddHeader(TEXT("Accept"), TEXT("application/json"));

    // Set authorization header if authenticated. The token is server-issued and
    // has no length limit, so the header is built in a growable buffer.
    if (m_authToken) {
        Str::Buffer authHeader;
        authHeader.Append(TEXT("Bearer "));
        authHeader.Append(m_authToken);
        if (!authHeader.Failed()) {
            m_httpClient->AddHeader(TEXT("Authorization"), authHeader.Get());
        }
    }
}

} // namespace HBX
