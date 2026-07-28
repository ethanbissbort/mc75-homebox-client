#include "../include/NbClient.hpp"
#include "../include/StrUtil.hpp"
#include "../include/Models/JsonLite.hpp"
#include <string.h>
#include <wchar.h>

namespace HBX {

namespace {

// The one HTTP status this client acts on itself. DRF answers 401 when a token
// was presented and rejected; NetBox answers 403 both for "no credentials" and
// for "this token may not do that", which a read-only token collects on writes
// while its reads keep working -- so only the 401 ends the session.
const int kHttpUnauthorized = 401;

// Stable token written into queue records and matched when they are routed
// back. Distinct from the instance id, which the operator sets per server.
const TCHAR* const kBackendKind = TEXT("nb");

// Shown in the title bar and against queued entries in the queue view.
const TCHAR* const kBackendDisplayName = TEXT("NetBox");

// The id a client carries until it is configured.
const TCHAR* const kDefaultInstanceId = TEXT("nb");

// NetBox v1 tokens. The operator overrides this with "Bearer" for the v2
// tokens introduced in 4.5; see NbClient::SetAuth.
const TCHAR* const kDefaultAuthScheme = TEXT("Token");

const TCHAR* const kTypeDeviceStatus = TEXT("DEVICE_STATUS");
const TCHAR* const kTypeDeviceMove   = TEXT("DEVICE_MOVE");

const TCHAR* const kDevicesPath = TEXT("/api/dcim/devices/");

/**
 * Query fragment carried by every device request.
 *
 * `exclude=config_context` is a requirement, not a tuning knob. The device
 * viewset uses DeviceWithConfigContextSerializer, which renders the operator's
 * full merged configuration context into every device object -- arbitrary
 * nested JSON that routinely runs to tens or hundreds of kilobytes per device
 * on a real deployment, against a transport that caps a response at 512 KB.
 *
 * `format=json` is the belt to the Accept header's braces. NetBox registers a
 * browsable-API HTML renderer alongside the JSON one, and an HTML document
 * reaching JsonLite produces a failure that says nothing about what went wrong.
 */
const TCHAR* const kDeviceQuery = TEXT("?format=json&exclude=config_context");

/**
 * DeviceStatusChoices, ordered the way a receive-to-service workflow walks
 * them: gear arrives as inventory, is racked as staged, goes live as active.
 * The remainder are the exceptions, least destructive first.
 */
const TCHAR* const kStatusChoices[] = {
    TEXT("inventory"),
    TEXT("staged"),
    TEXT("active"),
    TEXT("planned"),
    TEXT("offline"),
    TEXT("failed"),
    TEXT("decommissioning")
};

const int kStatusChoiceCount = (int)(sizeof(kStatusChoices) / sizeof(kStatusChoices[0]));

bool IsDigit(TCHAR c)
{
    return c >= (TCHAR)'0' && c <= (TCHAR)'9';
}

/**
 * True when `s` is a non-empty run of digits.
 *
 * Foreign keys go into a move body unquoted, so a value that is not a number
 * would not merely be rejected by the server -- it would break the shape of the
 * document, and a scanned string reaching that slot is an injection point.
 */
bool IsAllDigits(const TCHAR* s)
{
    if (!s || s[0] == 0) {
        return false;
    }
    for (int i = 0; s[i] != 0; i++) {
        if (!IsDigit(s[i])) {
            return false;
        }
    }
    return true;
}

/** True for a decimal NetBox would accept as a rack position. Also unquoted. */
bool IsDecimal(const TCHAR* s)
{
    if (!s) {
        return false;
    }

    int i = 0;
    if (s[i] == (TCHAR)'-' || s[i] == (TCHAR)'+') {
        i++;
    }

    int digits = 0;
    while (IsDigit(s[i])) {
        i++;
        digits++;
    }
    if (s[i] == (TCHAR)'.') {
        i++;
        while (IsDigit(s[i])) {
            i++;
            digits++;
        }
    }

    return digits > 0 && s[i] == 0;
}

TCHAR LowerAscii(TCHAR c)
{
    if (c >= (TCHAR)'A' && c <= (TCHAR)'Z') {
        return (TCHAR)(c + ((TCHAR)'a' - (TCHAR)'A'));
    }
    return c;
}

/**
 * Case-insensitive comparison over ASCII only.
 *
 * Written out rather than reached for through lstrcmpi for two reasons. The
 * host shim maps lstrcmpi to a case-sensitive strcmp, so a test would not
 * exercise what the device does. And folding only ASCII matches the identifiers
 * a barcode actually carries, while leaving accented text to compare exactly --
 * which is the safe direction to err, because the alternative is auto-selecting
 * a device the operator did not scan.
 */
bool EqualsIgnoreCaseAscii(const TCHAR* a, const TCHAR* b)
{
    if (!a || !b) {
        return false;
    }

    int i = 0;
    while (a[i] != 0 && b[i] != 0) {
        if (LowerAscii(a[i]) != LowerAscii(b[i])) {
            return false;
        }
        i++;
    }

    return a[i] == 0 && b[i] == 0;
}

/** True when every character is whitespace, or there are none. */
bool IsBlank(const TCHAR* s)
{
    if (!s) {
        return true;
    }
    for (int i = 0; s[i] != 0; i++) {
        if (s[i] != (TCHAR)' ' && s[i] != (TCHAR)'\t' &&
            s[i] != (TCHAR)'\r' && s[i] != (TCHAR)'\n') {
            return false;
        }
    }
    return true;
}

/**
 * True when `body` is plausibly a JSON document.
 *
 * HttpResponse carries the status code and the body but no headers, so the
 * Content-Type cannot be inspected directly; the first non-whitespace character
 * is the part of it that matters here. NetBox's browsable-API renderer emits an
 * HTML document starting with '<', and feeding that to JsonLite means chewing
 * through kilobytes of markup to reach a parse error that names no cause. The
 * Accept header and `?format=json` are what stop that renderer being selected;
 * this is what stops it mattering if they ever are.
 */
bool LooksLikeJson(const TCHAR* body)
{
    if (!body) {
        return false;
    }

    const TCHAR* p = body;
    while (*p == (TCHAR)' ' || *p == (TCHAR)'\t' ||
           *p == (TCHAR)'\r' || *p == (TCHAR)'\n') {
        p++;
    }

    return *p == (TCHAR)'{' || *p == (TCHAR)'[';
}

/** Adds a summary row, skipping the ones with nothing to say. */
void AddRow(Models::AssetSummary* out, const TCHAR* label, const TCHAR* value)
{
    if (!value || value[0] == 0) {
        return;
    }
    out->AddField(label, value);
}

/**
 * Appends one foreign key to a move body: `null` to clear it, a bare integer to
 * set it, nothing at all when the caller passed NULL. Returns false when a
 * value is present but is not an id.
 */
bool AppendRelation(Str::Buffer* out, bool* first, const TCHAR* key, const TCHAR* value)
{
    if (!value) {
        return true; // omitted: the server leaves this field as it is
    }
    if (value[0] != 0 && !IsAllDigits(value)) {
        return false;
    }

    if (!*first) {
        out->AppendChar((TCHAR)',');
    }
    *first = false;

    out->AppendJsonString(key);
    out->AppendChar((TCHAR)':');

    // Bare integers, not the {"name": ...} form NetBox also accepts: the dict
    // form is less consistently supported across 3.x point releases, and the
    // handheld already learned the ids when it read the device.
    out->Append(value[0] == 0 ? TEXT("null") : value);

    return true;
}

/**
 * Appends `,"<key>":"<escaped value>"` to a queue payload when the caller
 * supplied the field at all. An empty value is kept, not dropped: "" is what a
 * clear looks like, and dropping it would silently turn an unrack into a no-op.
 */
void AppendQueuedField(Str::Buffer* out, const TCHAR* key, const TCHAR* value)
{
    if (!value) {
        return;
    }
    out->AppendChar((TCHAR)',');
    out->AppendJsonPair(key, value);
}

/** Outcome of reading one field out of a queued move payload. */
enum QueuedField {
    FIELD_ABSENT,     // not in the payload: leave that part of the device alone
    FIELD_PRESENT,    // read; an empty result means "clear this field"
    FIELD_UNREADABLE  // present but not a value this client can act on
};

/**
 * Reads one field of a queued move payload as text.
 *
 * The three outcomes exist because absence and emptiness mean different things
 * here, and confusing them moves the wrong hardware. A number is accepted as
 * well as a string: the payloads this client writes quote every value, but a
 * hand-edited entry carrying {"rack":7} would otherwise fall through to the
 * null case and *unrack* the device -- the exact opposite of what it says.
 * Anything else present under the key is refused rather than guessed at.
 */
QueuedField ReadQueuedField(const Models::JsonLite& parser, const TCHAR* key,
                            TCHAR* out, int cap)
{
    if (!out || cap <= 0) {
        return FIELD_UNREADABLE;
    }
    out[0] = 0;

    if (!parser.HasKey(key)) {
        return FIELD_ABSENT;
    }

    // Allocated rather than copied straight into `out`, so that a value which
    // does not fit is reported as unreadable instead of being truncated into a
    // different id -- or, worse, into the empty string that clears the field.
    TCHAR* value = parser.GetStringAlloc(key);
    if (value) {
        bool fitted = Str::Copy(out, cap, value);
        delete[] value;
        if (!fitted) {
            out[0] = 0;
            return FIELD_UNREADABLE;
        }
        return FIELD_PRESENT;
    }

    double number = 0.0;
    if (parser.GetDouble(key, &number)) {
        if (!Models::Device::FormatPosition(number, out, cap)) {
            out[0] = 0;
            return FIELD_UNREADABLE;
        }
        return FIELD_PRESENT;
    }

    bool flag = false;
    if (parser.GetBool(key, &flag)) {
        return FIELD_UNREADABLE;
    }

    Models::JsonLite view;
    if (parser.GetObject(key, &view) || parser.GetArray(key, &view)) {
        return FIELD_UNREADABLE;
    }

    // Present, and none of the above: a JSON null, which is an explicit clear.
    return FIELD_PRESENT;
}

/**
 * True when the scanned code is the whole of the device's name, serial or asset
 * tag, compared case-insensitively the way NetBox's own iexact would.
 *
 * This is what makes the `?q=` fallback safe. `q` is a substring search, so
 * without it scanning "01" would resolve straight to "sw-core-01" and the
 * operator would move a device they never looked at.
 */
bool MatchesCode(const Models::Device& device, const TCHAR* code)
{
    if (!code || code[0] == 0) {
        return false;
    }

    return EqualsIgnoreCaseAscii(code, device.GetAssetTag()) ||
           EqualsIgnoreCaseAscii(code, device.GetSerial()) ||
           EqualsIgnoreCaseAscii(code, device.GetName());
}

} // namespace

NbClient::NbClient()
    : m_httpClient(NULL)
    , m_baseUrl(NULL)
    , m_token(NULL)
    , m_authenticated(false)
    , m_lastStatusCode(0)
    , m_matchCount(0)
    , m_totalMatchCount(0)
{
    Str::Copy(m_instanceId, INSTANCE_ID_MAX, kDefaultInstanceId);
    Str::Copy(m_authScheme, AUTH_SCHEME_MAX, kDefaultAuthScheme);
    m_serverVersion[0] = 0;

    m_httpClient = new HttpClient();
}

NbClient::~NbClient()
{
    if (m_httpClient) {
        delete m_httpClient;
    }
    if (m_baseUrl) {
        delete[] m_baseUrl;
    }
    if (m_token) {
        delete[] m_token;
    }
}

// ---- Backend identity -----------------------------------------------------

const TCHAR* NbClient::GetKind() const
{
    return kBackendKind;
}

const TCHAR* NbClient::GetDisplayName() const
{
    return kBackendDisplayName;
}

const TCHAR* NbClient::GetInstanceId() const
{
    return m_instanceId;
}

void NbClient::SetInstanceId(const TCHAR* instanceId)
{
    if (!instanceId || instanceId[0] == (TCHAR)'\0') {
        // Never leave the id empty: an empty tag would make every queue record
        // this client writes unroutable.
        Str::Copy(m_instanceId, INSTANCE_ID_MAX, kDefaultInstanceId);
        return;
    }

    Str::Copy(m_instanceId, INSTANCE_ID_MAX, instanceId);
}

// ---- Configuration --------------------------------------------------------

void NbClient::SetBaseUrl(const TCHAR* baseUrl)
{
    if (m_baseUrl) {
        delete[] m_baseUrl;
        m_baseUrl = NULL;
    }
    if (!baseUrl) {
        return;
    }

    TCHAR* copy = Str::Dup(baseUrl);
    if (!copy) {
        return;
    }

    // Trailing slashes are trimmed because every path this client builds starts
    // with one. "http://netbox.lan/" + "/api/dcim/devices/" would request
    // "//api/dcim/devices/", which NetBox routes nowhere and answers with a 404
    // that reads on screen exactly like a missing device.
    int len = Str::Length(copy);
    while (len > 0 && copy[len - 1] == (TCHAR)'/') {
        copy[len - 1] = 0;
        len--;
    }

    m_baseUrl = copy;
}

const TCHAR* NbClient::GetBaseUrl() const
{
    return m_baseUrl;
}

void NbClient::SetRequestTimeout(DWORD timeoutMs)
{
    if (m_httpClient) {
        m_httpClient->SetTimeout(timeoutMs);
    }
}

void NbClient::SetIgnoreCertificateErrors(bool ignore)
{
    if (m_httpClient) {
        m_httpClient->SetIgnoreCertificateErrors(ignore);
    }
}

void NbClient::SetAuth(const TCHAR* scheme, const TCHAR* token)
{
    if (!scheme || scheme[0] == (TCHAR)'\0') {
        Str::Copy(m_authScheme, AUTH_SCHEME_MAX, kDefaultAuthScheme);
    } else {
        Str::Copy(m_authScheme, AUTH_SCHEME_MAX, scheme);
    }

    if (m_token) {
        delete[] m_token;
        m_token = NULL;
    }

    // Any change of credential ends the session. A client that kept reporting
    // itself authenticated after being handed a different token would skip the
    // /api/status/ probe and discover the new token is wrong one scan at a
    // time, with nothing on screen to connect the failures to the change.
    m_authenticated = false;

    if (!token || token[0] == (TCHAR)'\0') {
        // A client that believes in an empty token sends a bare "Token " and
        // collects a 401 on every request for the rest of the shift, which
        // looks exactly like a network problem.
        return;
    }

    // Tokens are server-issued and have no length this client can assume -- a
    // v2 token is "nbt_<key>.<secret>" -- so they live on the heap.
    m_token = Str::Dup(token);
}

const TCHAR* NbClient::GetAuthScheme() const
{
    return m_authScheme;
}

// ---- Session --------------------------------------------------------------

bool NbClient::Authenticate()
{
    m_authenticated = false;
    m_serverVersion[0] = 0;

    if (!m_token || m_token[0] == (TCHAR)'\0') {
        // Nothing to present. Reporting this as a network failure would send
        // the operator to look at the radio instead of at hb_conf.json.
        return false;
    }

    // /api/status/ is the cheapest authenticated endpoint NetBox exposes, so
    // the token is verified once at startup rather than one scan at a time.
    TCHAR* response = NULL;
    if (!MakeApiRequest(TEXT("GET"), TEXT("/api/status/?format=json"), NULL, &response)) {
        return false;
    }

    // The reported version is recorded for diagnostics -- "talking to NetBox
    // 3.7.8" is a very different support call from "the scan failed". Nothing
    // branches on it: Models::Device reads `role` with a `device_role`
    // fallback, which covers 3.6 through 4.6 in a single path, so a server that
    // omits the field costs nothing.
    if (LooksLikeJson(response)) {
        Models::JsonLite parser;
        if (parser.Parse(response)) {
            parser.GetString(TEXT("netbox-version"), m_serverVersion, (DWORD)VERSION_MAX);
        }
    }

    delete[] response;

    m_authenticated = true;
    return true;
}

bool NbClient::IsAuthenticated() const
{
    return m_authenticated;
}

bool NbClient::SessionIsRenewable() const
{
    // A NetBox API token is static configuration provisioned by an
    // administrator, so a 401 means it is wrong or has been revoked. Retrying
    // would spend an extra round trip per scan to collect the same 401.
    return false;
}

int NbClient::GetLastStatusCode() const
{
    return m_lastStatusCode;
}

const TCHAR* NbClient::GetServerVersion() const
{
    return m_serverVersion;
}

// ---- Capabilities ---------------------------------------------------------

bool NbClient::SupportsStatus() const
{
    return true;
}

int NbClient::GetStatusChoiceCount() const
{
    return kStatusChoiceCount;
}

const TCHAR* NbClient::GetStatusChoice(int index) const
{
    if (index < 0 || index >= kStatusChoiceCount) {
        return NULL;
    }
    return kStatusChoices[index];
}

// ---- Lookup ---------------------------------------------------------------

bool NbClient::LookupByCode(const TCHAR* code, Models::AssetSummary* out)
{
    // Cleared first so a caller that reads GetMatchCount() after a failed
    // lookup cannot pick up the count from the previous scan.
    m_matchCount = 0;
    m_totalMatchCount = 0;

    if (!code || !out) {
        return false;
    }
    if (!m_authenticated) {
        return false;
    }

    TCHAR deviceId[Models::Device::ID_MAX];
    CodeKind kind = ClassifyCode(code, deviceId, Models::Device::ID_MAX);

    switch (kind) {
    case CODE_EMPTY:
    case CODE_CARTON:
        // Answered without touching the network. A carton label cannot name a
        // device, and spending two blocking round trips to discover that -- on
        // a link where each one freezes the screen -- teaches the operator that
        // the scanner is slow rather than that the label is wrong.
        return false;

    case CODE_DEVICE_ID:
        if (!FetchDeviceById(deviceId)) {
            return false;
        }
        break;

    case CODE_OPAQUE:
    default:
        // The asset tag is the site's own identifier and the intended primary
        // key of a handheld workflow, so it goes first and normally ends here.
        // The __ie suffix is mandatory: NetBox declares `serial` with iexact
        // but leaves `asset_tag` on the default case-sensitive `exact`, so a
        // decoder that differs in case from what was typed into NetBox finds
        // nothing and reports it as "no such device".
        if (!FetchDeviceList(TEXT("asset_tag__ie"), code, false)) {
            return false;
        }

        // Nothing carries that tag. One more request covers name, serial and
        // asset_tag together -- one round trip instead of three exact lookups.
        // A transport failure here is reported as a failure rather than as a
        // miss, because "no such device" is a conclusion only a device that
        // reached the server is entitled to draw.
        if (m_matchCount == 0 && !FetchDeviceList(TEXT("q"), code, true)) {
            return false;
        }
        break;
    }

    // Zero is a miss; more than one is the operator's decision. NetBox does not
    // enforce uniqueness on a serial, so several devices legitimately answer to
    // one scan, and picking the first would move the wrong one.
    if (m_matchCount != 1) {
        return false;
    }

    SummarizeDevice(&m_matches[0], out);
    return true;
}

int NbClient::GetMatchCount() const
{
    return m_matchCount;
}

int NbClient::GetTotalMatchCount() const
{
    return m_totalMatchCount;
}

bool NbClient::GetMatch(int index, Models::AssetSummary* out)
{
    if (!out || index < 0 || index >= m_matchCount) {
        return false;
    }

    SummarizeDevice(&m_matches[index], out);
    return true;
}

const Models::Device* NbClient::GetMatchedDevice(int index) const
{
    if (index < 0 || index >= m_matchCount) {
        return NULL;
    }
    return &m_matches[index];
}

bool NbClient::FetchDeviceList(const TCHAR* filterKey, const TCHAR* code, bool requireExactMatch)
{
    Str::Buffer path;
    if (!BuildLookupPath(filterKey, code, &path)) {
        return false;
    }

    TCHAR* response = NULL;
    if (!MakeApiRequest(TEXT("GET"), path.Get(), NULL, &response)) {
        return false;
    }

    if (!LooksLikeJson(response)) {
        delete[] response;
        return false;
    }

    Models::JsonLite parser;
    bool parsed = parser.Parse(response);
    delete[] response;
    if (!parsed) {
        return false;
    }

    // A zero-result lookup is 200 OK with count 0 and an empty results array,
    // never a 404, so "not found" has to be read out of the envelope.
    int serverCount = 0;
    parser.GetInt(TEXT("count"), &serverCount);

    Models::JsonLite results;
    if (!parser.GetArray(TEXT("results"), &results)) {
        // A list endpoint always returns the envelope. Anything else is a proxy
        // or an error page, not a result set.
        return false;
    }

    int available = results.GetArrayLength();
    int stored = 0;

    for (int i = 0; i < available && stored < MAX_MATCHES; i++) {
        Models::JsonLite element;
        if (!results.GetArrayElement(i, &element)) {
            continue;
        }

        // Parsed straight into the slot it will occupy: a Device is a couple of
        // kilobytes, and a temporary per result would put four of them on the
        // stack of a thread that has very little of it.
        if (!m_matches[stored].FromJson(element)) {
            continue;
        }

        if (requireExactMatch && !MatchesCode(m_matches[stored], code)) {
            continue;
        }

        stored++;
    }

    m_matchCount = stored;

    if (requireExactMatch) {
        // The server's count includes the substring hits the filter above
        // rejected, so reporting it would promise the operator matches this
        // client has already decided are not matches.
        m_totalMatchCount = stored;
    } else {
        m_totalMatchCount = (serverCount > stored) ? serverCount : stored;
    }

    return true;
}

bool NbClient::FetchDeviceById(const TCHAR* deviceId)
{
    Str::Buffer path;
    if (!BuildDevicePath(deviceId, &path)) {
        return false;
    }

    TCHAR* response = NULL;
    if (!MakeApiRequest(TEXT("GET"), path.Get(), NULL, &response)) {
        return false;
    }

    if (!LooksLikeJson(response)) {
        delete[] response;
        return false;
    }

    // A detail endpoint returns the bare object with no envelope, and 404s when
    // the id does not exist -- which MakeApiRequest has already turned into a
    // failure above.
    bool parsed = m_matches[0].FromJson(response);
    delete[] response;
    if (!parsed) {
        return false;
    }

    m_matchCount = 1;
    m_totalMatchCount = 1;
    return true;
}

// ---- Writes ---------------------------------------------------------------

bool NbClient::SetDeviceStatus(const TCHAR* deviceId, const TCHAR* statusValue)
{
    if (!deviceId || deviceId[0] == 0 || !statusValue || statusValue[0] == 0) {
        return false;
    }
    if (!m_authenticated) {
        return false;
    }

    Str::Buffer path;
    if (!BuildDevicePath(deviceId, &path)) {
        return false;
    }

    // The bare string, not the {"value","label"} object the same field is read
    // back as. Sending the shape you just received fails validation, which is
    // the read/write asymmetry that catches everyone once.
    //
    // The value is not checked against GetStatusChoice(): DeviceStatusChoices
    // is extensible through the FIELD_CHOICES configuration parameter, so a
    // deployment can define values this build has never heard of, and refusing
    // them here would make the client wrong about its own server.
    Str::Buffer body;
    body.AppendChar((TCHAR)'{');
    body.AppendJsonPair(TEXT("status"), statusValue);
    body.AppendChar((TCHAR)'}');
    if (body.Failed()) {
        return false;
    }

    return MakeApiRequest(TEXT("PATCH"), path.Get(), body.Get(), NULL);
}

bool NbClient::MoveDevice(const TCHAR* deviceId, const TCHAR* siteId, const TCHAR* locationId,
                          const TCHAR* rackId, const TCHAR* position, const TCHAR* face)
{
    if (!deviceId || deviceId[0] == 0) {
        return false;
    }
    if (!m_authenticated) {
        return false;
    }

    // Built before the path so an unusable move costs nothing at all.
    Str::Buffer body;
    if (!BuildMoveBody(siteId, locationId, rackId, position, face, &body)) {
        return false;
    }

    Str::Buffer path;
    if (!BuildDevicePath(deviceId, &path)) {
        return false;
    }

    // One request carrying the whole positional set. NetBox validates site,
    // location, rack, position and face against each other, so a move split
    // into parts is rejected part way through and leaves the record describing
    // a device that is in two places at once.
    return MakeApiRequest(TEXT("PATCH"), path.Get(), body.Get(), NULL);
}

// ---- Queue replay ---------------------------------------------------------

const TCHAR* NbClient::GetStatusTransactionType()
{
    return kTypeDeviceStatus;
}

const TCHAR* NbClient::GetMoveTransactionType()
{
    return kTypeDeviceMove;
}

ReplayResult NbClient::Replay(const TCHAR* type, const TCHAR* data)
{
    if (!type) {
        return REPLAY_SKIPPED;
    }
    if (!data) {
        data = TEXT("");
    }

    if (wcscmp(type, kTypeDeviceStatus) == 0) {
        return ReplayStatus(data);
    }
    if (wcscmp(type, kTypeDeviceMove) == 0) {
        return ReplayMove(data);
    }

    // Addressed to some other inventory system. Reporting it as a failure would
    // pin the sync status at "failed" for as long as the entry sits there.
    return REPLAY_SKIPPED;
}

ReplayResult NbClient::ReplayStatus(const TCHAR* data)
{
    // DATA is "STATUS:<deviceId>:<statusValue>". A NetBox primary key is an
    // integer and cannot contain ':', so the first separator after the prefix
    // splits it unambiguously, and the status value is the whole remainder.
    if (wcsncmp(data, TEXT("STATUS:"), 7) != 0) {
        return REPLAY_RETRY;
    }

    const TCHAR* idStart = data + 7;
    const TCHAR* separator = wcschr(idStart, (TCHAR)':');
    if (!separator) {
        return REPLAY_RETRY;
    }

    TCHAR deviceId[Models::Device::ID_MAX];
    if (!Str::CopyN(deviceId, Models::Device::ID_MAX, idStart, (int)(separator - idStart))) {
        return REPLAY_RETRY;
    }

    const TCHAR* statusValue = separator + 1;
    if (deviceId[0] == 0 || statusValue[0] == 0) {
        return REPLAY_RETRY;
    }

    // Only a write the server accepted clears the entry. Everything else --
    // offline, a rejection, or the malformed payloads above -- leaves it queued
    // and visible, because discarding an operator's work silently is worse than
    // showing them an entry they can inspect and remove.
    return SetDeviceStatus(deviceId, statusValue) ? REPLAY_SENT : REPLAY_RETRY;
}

ReplayResult NbClient::ReplayMove(const TCHAR* data)
{
    // DATA is "MOVE:<json>". JSON rather than a delimited string because a move
    // carries six optional fields, and the difference between "leave this
    // alone" (key absent) and "clear it" (empty value) has to survive the queue
    // intact -- collapsing the two would unrack devices nobody asked to unrack.
    if (wcsncmp(data, TEXT("MOVE:"), 5) != 0) {
        return REPLAY_RETRY;
    }

    const TCHAR* json = data + 5;
    if (json[0] == 0) {
        return REPLAY_RETRY;
    }

    Models::JsonLite parser;
    if (!parser.Parse(json)) {
        return REPLAY_RETRY;
    }

    TCHAR deviceId[Models::Device::ID_MAX];
    if (ReadQueuedField(parser, TEXT("id"), deviceId, Models::Device::ID_MAX) != FIELD_PRESENT ||
        deviceId[0] == 0) {
        return REPLAY_RETRY;
    }

    TCHAR site[Models::Device::ID_MAX];
    TCHAR location[Models::Device::ID_MAX];
    TCHAR rack[Models::Device::ID_MAX];
    TCHAR position[Models::Device::POSITION_MAX];
    TCHAR face[Models::Device::FACE_MAX];

    QueuedField siteState     = ReadQueuedField(parser, TEXT("site"), site, Models::Device::ID_MAX);
    QueuedField locationState = ReadQueuedField(parser, TEXT("location"), location, Models::Device::ID_MAX);
    QueuedField rackState     = ReadQueuedField(parser, TEXT("rack"), rack, Models::Device::ID_MAX);
    QueuedField positionState = ReadQueuedField(parser, TEXT("position"), position, Models::Device::POSITION_MAX);
    QueuedField faceState     = ReadQueuedField(parser, TEXT("face"), face, Models::Device::FACE_MAX);

    if (siteState == FIELD_UNREADABLE || locationState == FIELD_UNREADABLE ||
        rackState == FIELD_UNREADABLE || positionState == FIELD_UNREADABLE ||
        faceState == FIELD_UNREADABLE) {
        return REPLAY_RETRY;
    }

    bool sent = MoveDevice(deviceId,
                           (siteState == FIELD_PRESENT) ? site : NULL,
                           (locationState == FIELD_PRESENT) ? location : NULL,
                           (rackState == FIELD_PRESENT) ? rack : NULL,
                           (positionState == FIELD_PRESENT) ? position : NULL,
                           (faceState == FIELD_PRESENT) ? face : NULL);

    return sent ? REPLAY_SENT : REPLAY_RETRY;
}

// ---- Pure helpers ---------------------------------------------------------

void NbClient::SummarizeDevice(const Models::Device* device, Models::AssetSummary* out)
{
    if (!out) {
        return;
    }

    out->Clear();
    if (!device) {
        return;
    }

    out->SetSource(kBackendDisplayName);
    out->SetId(device->GetId());

    // NetBox allows an unnamed device, and unnamed is the normal state for gear
    // that has only just been received, so the header falls back to whatever
    // else identifies the unit rather than rendering an empty line.
    const TCHAR* title = device->GetName();
    if (title[0] == 0) {
        title = device->GetAssetTag();
    }
    if (title[0] == 0) {
        title = device->GetSerial();
    }
    out->SetTitle(title);

    // "Cisco Catalyst 9300-48P": the two fields the operator uses to confirm
    // they are holding the right kind of box.
    const int kSubtitleCap = Models::AssetSummary::VALUE_MAX;
    TCHAR subtitle[kSubtitleCap];
    subtitle[0] = 0;
    if (device->GetManufacturer()[0] != 0) {
        Str::Append(subtitle, kSubtitleCap, device->GetManufacturer());
    }
    if (device->GetDeviceTypeModel()[0] != 0) {
        if (subtitle[0] != 0) {
            Str::AppendChar(subtitle, kSubtitleCap, (TCHAR)' ');
        }
        Str::Append(subtitle, kSubtitleCap, device->GetDeviceTypeModel());
    }
    out->SetSubtitle(subtitle);

    // The label, not the value: "Decommissioning" is what the operator reads.
    // The machine value is the fallback, because a status line that says
    // "active" is still better than one that says nothing.
    const TCHAR* status = device->GetStatusLabel();
    if (status[0] == 0) {
        status = device->GetStatus();
    }
    out->SetStatus(status);

    // The asset tag is the site's own identifier and the one printed on the
    // label, so it is what this line should echo; the serial covers gear that
    // was never tagged.
    const TCHAR* code = device->GetAssetTag();
    if (code[0] == 0) {
        code = device->GetSerial();
    }
    out->SetCode(code);

    // Rows in the order that answers "is this the right box, and where does it
    // live". Empty values are skipped rather than shown blank: an unracked
    // device would otherwise spend three of the seven visible rows on nothing
    // and push the fields that matter off the bottom of the screen.
    AddRow(out, TEXT("Asset Tag"), device->GetAssetTag());
    AddRow(out, TEXT("Serial"), device->GetSerial());
    AddRow(out, TEXT("Site"), device->GetSiteName());
    AddRow(out, TEXT("Location"), device->GetLocationName());
    AddRow(out, TEXT("Rack"), device->GetRackName());

    // Position and face share a row. They are one physical fact -- "U12,
    // front" -- and NetBox will not accept either without the other anyway.
    const int kPositionCap = Models::Device::POSITION_MAX + Models::Device::FACE_MAX + 4;
    TCHAR positionText[kPositionCap];
    positionText[0] = 0;
    if (device->HasPosition()) {
        TCHAR unit[Models::Device::POSITION_MAX];
        if (device->GetPositionText(unit, Models::Device::POSITION_MAX)) {
            Str::AppendChar(positionText, kPositionCap, (TCHAR)'U');
            Str::Append(positionText, kPositionCap, unit);
            if (device->GetFace()[0] != 0) {
                Str::AppendChar(positionText, kPositionCap, (TCHAR)' ');
                Str::Append(positionText, kPositionCap, device->GetFace());
            }
        }
    }
    AddRow(out, TEXT("Position"), positionText);

    AddRow(out, TEXT("Role"), device->GetDeviceRole());
    AddRow(out, TEXT("Primary IP"), device->GetPrimaryIp());
}

NbClient::CodeKind NbClient::ClassifyCode(const TCHAR* code, TCHAR* deviceIdOut, int deviceIdCap)
{
    if (deviceIdOut && deviceIdCap > 0) {
        deviceIdOut[0] = 0;
    }

    if (IsBlank(code)) {
        return CODE_EMPTY;
    }

    // A GS1-128 shipping label. Only the explicit "(00)" application-identifier
    // form is refused: it cannot be an asset tag, whereas a bare 18-digit run
    // could be, and turning a real tag into "not a device" would cost more than
    // the two round trips it saves.
    if (wcsncmp(code, TEXT("(00)"), 4) == 0) {
        return CODE_CARTON;
    }

    // Printed location labels carry self-describing tokens -- NBSITE:, NBLOC:,
    // NBRACK:, NBDEV: -- holding the NetBox primary key rather than the name,
    // because names get renamed and ids do not. Only the device form resolves
    // to a device here; the others belong to the move screen.
    const TCHAR* digits = NULL;
    if (wcsncmp(code, TEXT("NBDEV:"), 6) == 0) {
        digits = code + 6;
    } else {
        // A NetBox device URL, which several label plugins emit as a QR code.
        const TCHAR* marker = wcsstr(code, TEXT("/dcim/devices/"));
        if (marker) {
            digits = marker + 14;
        }
    }

    if (digits) {
        int length = 0;
        while (IsDigit(digits[length])) {
            length++;
        }
        // A token that does not carry a usable key falls back to an ordinary
        // lookup rather than addressing device 0.
        if (length > 0 && deviceIdOut &&
            Str::CopyN(deviceIdOut, deviceIdCap, digits, length)) {
            return CODE_DEVICE_ID;
        }
        if (deviceIdOut && deviceIdCap > 0) {
            deviceIdOut[0] = 0;
        }
    }

    return CODE_OPAQUE;
}

bool NbClient::BuildLookupPath(const TCHAR* filterKey, const TCHAR* code, Str::Buffer* out)
{
    if (!out) {
        return false;
    }
    out->Clear();

    if (!filterKey || filterKey[0] == 0 || !code || code[0] == 0) {
        return false;
    }

    // NetBox caps name at 64 and both serial and asset_tag at 50, so a longer
    // code cannot match any device. Refusing it here saves a round trip that
    // freezes the screen for its whole timeout.
    if (Str::Length(code) > MAX_CODE_CHARS) {
        return false;
    }

    // Only the value is encoded. Str::UrlEncode escapes '/' as %2F, so it is
    // valid for exactly one query value or one path segment and never for a
    // whole URL -- the structure around it stays literal.
    TCHAR encoded[MAX_ENCODED_CHARS];
    if (!Str::UrlEncode(encoded, MAX_ENCODED_CHARS, code)) {
        return false;
    }

    out->Append(kDevicesPath);
    out->Append(kDeviceQuery);

    // An explicit limit sized to the screen. The server's default page is 50
    // full device objects, which is a couple of hundred kilobytes nobody will
    // read on a 240x320 display.
    out->Append(TEXT("&limit="));
    out->AppendInt((long)MAX_MATCHES);

    out->AppendChar((TCHAR)'&');
    out->Append(filterKey);
    out->AppendChar((TCHAR)'=');
    out->Append(encoded);

    return !out->Failed();
}

bool NbClient::BuildDevicePath(const TCHAR* deviceId, Str::Buffer* out)
{
    if (!out) {
        return false;
    }
    out->Clear();

    if (!deviceId || deviceId[0] == 0) {
        return false;
    }

    TCHAR encoded[MAX_ENCODED_CHARS];
    if (!Str::UrlEncode(encoded, MAX_ENCODED_CHARS, deviceId)) {
        return false;
    }

    out->Append(kDevicesPath);
    out->Append(encoded);

    // The trailing slash is mandatory. Without it NetBox answers 301 on a GET,
    // and Django cannot redirect a body-carrying PATCH at all -- it raises
    // instead, so the write fails outright. This transport never follows
    // redirects, which is what keeps a 3xx a visible error rather than a PATCH
    // silently rewritten into a GET that reports success and changes nothing.
    out->AppendChar((TCHAR)'/');
    out->Append(kDeviceQuery);

    return !out->Failed();
}

bool NbClient::BuildMoveBody(const TCHAR* siteId, const TCHAR* locationId,
                             const TCHAR* rackId, const TCHAR* position,
                             const TCHAR* face, Str::Buffer* out)
{
    if (!out) {
        return false;
    }
    out->Clear();

    // A rack only means anything alongside the site and location it belongs to.
    // NetBox checks that relationship, so a rack sent on its own is a 400
    // waiting to happen -- and one that surfaces from the queue hours after the
    // operator walked away from the rack.
    bool settingRack = (rackId && rackId[0] != 0);
    if (settingRack && (!siteId || !locationId)) {
        return false;
    }

    out->AppendChar((TCHAR)'{');
    bool first = true;

    if (!AppendRelation(out, &first, TEXT("site"), siteId) ||
        !AppendRelation(out, &first, TEXT("location"), locationId) ||
        !AppendRelation(out, &first, TEXT("rack"), rackId)) {
        out->Clear();
        return false;
    }

    if (position) {
        if (position[0] != 0 && !IsDecimal(position)) {
            out->Clear();
            return false;
        }
        if (!first) {
            out->AppendChar((TCHAR)',');
        }
        first = false;
        out->AppendJsonString(TEXT("position"));
        out->AppendChar((TCHAR)':');
        // Unquoted, and a decimal: racks take half-U slots, so 42.5 is a real
        // position and an integer here would silently drop the half.
        out->Append(position[0] == 0 ? TEXT("null") : position);
    }

    if (face) {
        if (!first) {
            out->AppendChar((TCHAR)',');
        }
        first = false;
        // Cleared with "" rather than null: the serializer is allow_blank with
        // an empty default and rejects a null face. Quoted and escaped, so
        // unlike the ids above it cannot break the document's shape.
        out->AppendJsonPair(TEXT("face"), face);
    }

    if (first) {
        // Nothing was supplied. An empty PATCH body is not worth a round trip,
        // and sending one would leave the caller unable to tell a successful
        // move from a move that never carried anything.
        out->Clear();
        return false;
    }

    out->AppendChar((TCHAR)'}');
    return !out->Failed();
}

bool NbClient::BuildStatusPayload(const TCHAR* deviceId, const TCHAR* statusValue,
                                  Str::Buffer* out)
{
    if (!out) {
        return false;
    }
    out->Clear();

    if (!deviceId || deviceId[0] == 0 || !statusValue || statusValue[0] == 0) {
        return false;
    }

    // Delimited rather than JSON because both halves are short tokens with no
    // optional parts. A NetBox primary key cannot contain ':' and the status
    // value is the remainder of the line, so nothing can collide -- but the id
    // is checked anyway, since a payload that will not parse back is one the
    // operator's work disappears into.
    if (wcschr(deviceId, (TCHAR)':')) {
        return false;
    }

    // Neither half is JSON-escaped here, and the journal is line-oriented: a
    // newline anywhere in the payload would split one queued record into two
    // that can never be replayed. The status value is normally one of
    // GetStatusChoice's tokens, but DeviceStatusChoices is extensible through
    // the server's FIELD_CHOICES, so its contents are not this build's to
    // assume.
    if (wcschr(deviceId, (TCHAR)'\r') || wcschr(deviceId, (TCHAR)'\n') ||
        wcschr(statusValue, (TCHAR)'\r') || wcschr(statusValue, (TCHAR)'\n')) {
        return false;
    }

    out->Append(TEXT("STATUS:"));
    out->Append(deviceId);
    out->AppendChar((TCHAR)':');
    out->Append(statusValue);

    return !out->Failed();
}

bool NbClient::BuildMovePayload(const TCHAR* deviceId, const TCHAR* siteId,
                                const TCHAR* locationId, const TCHAR* rackId,
                                const TCHAR* position, const TCHAR* face,
                                Str::Buffer* out)
{
    if (!out) {
        return false;
    }
    out->Clear();

    if (!deviceId || deviceId[0] == 0) {
        return false;
    }

    // Validated with exactly the code that will build the PATCH at replay time,
    // so a move that could never be applied is refused at the rack -- where the
    // operator can still fix it -- rather than hours later out of a queue that
    // nobody is watching.
    Str::Buffer probe;
    if (!BuildMoveBody(siteId, locationId, rackId, position, face, &probe)) {
        return false;
    }

    out->Append(TEXT("MOVE:{"));
    out->AppendJsonPair(TEXT("id"), deviceId);

    AppendQueuedField(out, TEXT("site"), siteId);
    AppendQueuedField(out, TEXT("location"), locationId);
    AppendQueuedField(out, TEXT("rack"), rackId);
    AppendQueuedField(out, TEXT("position"), position);
    AppendQueuedField(out, TEXT("face"), face);

    out->AppendChar((TCHAR)'}');

    return !out->Failed();
}

// ---- Transport ------------------------------------------------------------

bool NbClient::MakeApiRequest(const TCHAR* method, const TCHAR* path, const TCHAR* body,
                              TCHAR** response)
{
    if (response) {
        *response = NULL;
    }
    if (!m_httpClient || !m_baseUrl || !method || !path) {
        return false;
    }

    // Base URL (configuration) and path (a scanned, percent-encoded code) are
    // both variable length, so the URL grows to fit rather than being formatted
    // into a fixed buffer.
    Str::Buffer fullUrl;
    fullUrl.Append(m_baseUrl);
    fullUrl.Append(path);
    if (fullUrl.Failed()) {
        return false;
    }

    SetAuthHeaders();

    // Start from "no answer" so a caller inspecting GetLastStatusCode() after a
    // failed request cannot read the status of the previous one.
    m_lastStatusCode = 0;

    HttpClient::HttpResponse httpResponse;
    bool success = false;

    if (lstrcmp(method, TEXT("GET")) == 0) {
        success = m_httpClient->Get(fullUrl.Get(), &httpResponse);
    } else if (lstrcmp(method, TEXT("PATCH")) == 0) {
        // PATCH is the only write verb this client uses. A PUT would be a full
        // object representation, so every field the handheld never loaded --
        // tenant, platform, comments, custom fields -- would be sent as absent
        // and blanked on a device the operator only meant to move.
        success = m_httpClient->Patch(fullUrl.Get(), body, &httpResponse);
    }

    m_lastStatusCode = httpResponse.statusCode;

    // The token was presented and rejected. Dropping the session here is what
    // lets the controller report a configuration problem instead of turning
    // every later lookup into an unexplained "not found". Done before the
    // transport check, because a 401 whose body arrived truncated is still a
    // 401.
    if (m_lastStatusCode == kHttpUnauthorized) {
        m_authenticated = false;
    }

    if (!success) {
        if (httpResponse.body) {
            delete[] httpResponse.body;
        }
        return false;
    }

    // 200-299 only. A 3xx reaches here untouched because neither transport
    // follows redirects, and for NetBox a 3xx means the URL's trailing slash is
    // wrong -- a bug in this client, not a state to recover from.
    if (httpResponse.statusCode < 200 || httpResponse.statusCode >= 300) {
        if (httpResponse.body) {
            delete[] httpResponse.body;
        }
        return false;
    }

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

void NbClient::SetAuthHeaders()
{
    if (!m_httpClient) {
        return;
    }

    m_httpClient->ClearHeaders();

    // Bodies go on the wire as UTF-8 on both transports, so the charset is
    // stated explicitly. NetBox leaves DRF's UNICODE_JSON on, so responses come
    // back as raw UTF-8 too.
    m_httpClient->AddHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));

    // No `version=` parameter, ever. NetBox's ALLOWED_VERSIONS contains only
    // the version that is installed, so pinning any value returns 406 -- and
    // pinning the current one guarantees this client breaks the next time the
    // server is upgraded. There is no compatibility to gain: NetBox does not
    // serve older API versions.
    m_httpClient->AddHeader(TEXT("Accept"), TEXT("application/json"));

    // WinInet on Windows CE does not reliably inflate a gzip response, and a
    // compressed body reaching the UTF-8 decode is unrecoverable. On a
    // warehouse LAN bandwidth is not the constraint; heap and code size are.
    m_httpClient->AddHeader(TEXT("Accept-Encoding"), TEXT("identity"));

    if (m_token && m_token[0] != 0) {
        // "<scheme> <token>" covers both token generations from one build:
        // "Token <40 hex>" for v1, "Bearer nbt_<key>.<secret>" for the v2
        // tokens NetBox 4.5 introduced. The header is built in a growable
        // buffer because the token is server-issued and has no assumable length.
        Str::Buffer authHeader;
        authHeader.Append(m_authScheme);
        authHeader.AppendChar((TCHAR)' ');
        authHeader.Append(m_token);
        if (!authHeader.Failed()) {
            m_httpClient->AddHeader(TEXT("Authorization"), authHeader.Get());
        }
    }
}

} // namespace HBX
