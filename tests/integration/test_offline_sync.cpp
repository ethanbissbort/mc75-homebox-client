/*
 * test_offline_sync.cpp  --  Integration tests for the offline-first core
 * -----------------------------------------------------------------------
 * Drives the pieces that make the MC75 client survive a lost network, wired
 * together the way the application wires them:
 *
 *   Scenario A  SyncEngine + Journal  -- queue work while offline, and prove
 *                                        nothing is lost or duplicated across
 *                                        a failed sync and an app restart.
 *   Scenario B  SyncEngine queue ops   -- per-entry removal, clearing, and the
 *                                        auto-sync trigger the UI timer polls.
 *   Scenario C  Config                 -- persistence round-trip, documented
 *                                        keys, keys the app does not own, and
 *                                        the missing-file fallback.
 *   Scenario D  Backend routing        -- an entry for a backend that is not
 *                                        configured is skipped, not failed.
 *   Scenario E  Legacy records         -- an untagged entry written before
 *                                        backend tagging still replays.
 *   Scenario F  Backend isolation      -- switching backends never replays one
 *                                        backend's work against the other.
 *   Scenario G  Replay outcomes        -- a transient failure keeps its entry.
 *
 * The HbClient in scenarios A and B points at an unresolvable host, so the
 * engine is genuinely offline for the whole run -- which is exactly the state
 * those tests care about. Scenarios D-G need replay to actually happen, so they
 * drive the engine through recording backends on a host that resolves; sending
 * anything to a real server needs a server and belongs in a device-side test.
 *
 * Host build notes (see tests/host/shim/windows.h):
 *   - TCHAR is `char`, so TEXT("x") is a narrow "x". All literals handed to the
 *     code under test are still wrapped in TEXT(...) so the same source works on
 *     both the device and the host.
 *   - CreateFile/ReadFile/DeleteFile are mapped onto POSIX open/read/unlink, so
 *     these tests hit real files under /tmp. Each scenario deletes its temp file
 *     up front so re-runs start from a clean slate.
 *   - C++03 only: no auto / lambdas / range-for / STL containers; NULL not nullptr.
 */
#include "test_framework.hpp"

#include <windows.h>       // shim: TCHAR, TEXT, DeleteFile, HANDLE, ...
#include "Journal.hpp"
#include "Config.hpp"
#include "StrUtil.hpp"
#include "Models/JsonLite.hpp"
#include "SyncEngine.hpp"
#include "InventoryBackend.hpp"
#include "HbClient.hpp"

#include <cstring>

using namespace HBX;

// A host name that cannot resolve, so CheckConnectivity() reports offline
// without waiting on a real DNS server.
static const TCHAR* const kUnreachable =
    TEXT("http://mc75-offline.invalid:8080/api");

// A host name that always resolves without touching a network, so the engine
// gets past its connectivity gate and the routing under test actually runs.
static const TCHAR* const kResolvable = TEXT("http://localhost:8080/api");

static void FreePending(TCHAR** arr, int n)
{
    if (!arr) {
        return;
    }
    for (int i = 0; i < n; i++) {
        delete[] arr[i];
    }
    delete[] arr;
}

// Writes a UTF-8 configuration file the way a deployment or a hand edit would,
// so Config::Load is exercised on bytes it did not write itself.
static bool WriteTextFile(const TCHAR* path, const char* utf8)
{
    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD length = (DWORD)std::strlen(utf8);
    DWORD written = 0;
    BOOL ok = WriteFile(h, utf8, length, &written, NULL);
    CloseHandle(h);

    return (ok != FALSE) && (written == length);
}

// Reads a whole file back as the UTF-8 bytes on disk, so a test can inspect
// what Config::Save actually wrote instead of only what Config::Load makes of
// it -- a key that is silently dropped reloads as a default and looks fine.
static bool ReadTextFile(const TCHAR* path, char* out, int cap)
{
    out[0] = '\0';

    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD read = 0;
    BOOL ok = ReadFile(h, out, (DWORD)(cap - 1), &read, NULL);
    CloseHandle(h);

    if (ok == FALSE) {
        return false;
    }

    out[read] = '\0';
    return true;
}

static int QueueDepth(const SyncEngine& engine)
{
    TCHAR** pending = NULL;
    int n = 0;
    if (!engine.GetQueuedTransactions(&pending, &n)) {
        return -1;
    }
    FreePending(pending, n);
    return n;
}

/* ------------------------------------------------------------------------- *
 * Scenario A - queue while offline, survive a failed sync and a restart
 * ------------------------------------------------------------------------- */
TEST_CASE("Offline: queued scans survive a failed sync and an app restart")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_offline.dat");
    DeleteFile(path);

    {
        Journal journal;
        CHECK(journal.Initialize(path));

        HbClient client;
        client.SetBaseUrl(kUnreachable);

        SyncEngine engine(&client, &journal);

        // --- Go "offline": the operator keeps scanning. ---------------------
        CHECK(engine.QueueScan(TEXT("0001234567890"), NULL));
        CHECK(engine.QueueScan(TEXT("0009876543210"), NULL));
        CHECK(engine.QueueScan(TEXT("0005555555555"), TEXT("LOC-A2")));
        CHECK(engine.QueueTransaction(TEXT("ITEM_UPDATE"),
            TEXT("UPDATE:{\"barcode\":\"0001234567890\",\"quantity\":7}")));

        CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 4);
        CHECK_EQ_INT(QueueDepth(engine), 4);

        // Audit logging must not inflate the queue: Controller journals every
        // scan, and those records are history, not work.
        CHECK(journal.LogTransaction(TEXT("SCAN"), TEXT("0001234567890"),
                                     TEXT("Barcode scanned")));
        CHECK(journal.LogInfo(TEXT("operator switched to queue view")));
        CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 4);

        // --- A sync attempt with no network must not consume the queue. -----
        CHECK_FALSE(engine.IsOnline());
        CHECK_FALSE(engine.Sync());
        CHECK(engine.GetSyncStatus() == SyncEngine::SYNC_OFFLINE);
        CHECK(engine.GetLastSyncError() != NULL);

        // Nothing may be dropped by a failed sync -- this is the whole point of
        // the offline queue.
        CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 4);
        CHECK_EQ_INT(QueueDepth(engine), 4);

        // Repeated failed syncs must stay idempotent rather than duplicating or
        // shedding entries.
        CHECK_FALSE(engine.Sync());
        CHECK_FALSE(engine.Sync());
        CHECK_EQ_INT(QueueDepth(engine), 4);
    } // destructors close the journal: simulates the process ending

    // --- Restart the app. A battery swap on an MC75 does exactly this. ------
    Journal journal;
    CHECK(journal.Initialize(path));

    HbClient client;
    client.SetBaseUrl(kUnreachable);
    SyncEngine engine(&client, &journal);

    // The queue depth must be known without anyone enumerating first: the UI
    // reads this straight into the title bar.
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 4);

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(engine.GetQueuedTransactions(&pending, &n));
    CHECK_EQ_INT(n, 4);

    // Entries come back oldest first and still carry their payload, so replay
    // order matches scan order.
    CHECK(pending[0] != NULL);
    const TCHAR* payload = Journal::PayloadOf(pending[0]);
    CHECK(payload != NULL);
    CHECK(wcsstr(payload, TEXT("ITEM_SCAN")) != NULL);
    CHECK(wcsstr(payload, TEXT("0001234567890")) != NULL);

    // The location-carrying scan kept its location through persistence.
    const TCHAR* third = Journal::PayloadOf(pending[2]);
    CHECK(third != NULL);
    CHECK(wcsstr(third, TEXT("LOC-A2")) != NULL);

    // And the item update kept its full JSON payload.
    const TCHAR* fourth = Journal::PayloadOf(pending[3]);
    CHECK(fourth != NULL);
    CHECK(wcsstr(fourth, TEXT("ITEM_UPDATE")) != NULL);
    CHECK(wcsstr(fourth, TEXT("\"quantity\":7")) != NULL);

    FreePending(pending, n);
    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario B - queue maintenance and the auto-sync trigger
 * ------------------------------------------------------------------------- */
TEST_CASE("Offline: per-entry removal, clearing and the auto-sync trigger")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_offline_ops.dat");
    DeleteFile(path);

    Journal journal;
    CHECK(journal.Initialize(path));

    HbClient client;
    client.SetBaseUrl(kUnreachable);
    SyncEngine engine(&client, &journal);

    CHECK(engine.QueueScan(TEXT("AAA111"), NULL));
    CHECK(engine.QueueScan(TEXT("BBB222"), NULL));
    CHECK(engine.QueueScan(TEXT("CCC333"), NULL));
    CHECK_EQ_INT(QueueDepth(engine), 3);

    // --- Drop one entry from the queue view without sending it. -------------
    TCHAR** pending = NULL;
    int n = 0;
    CHECK(engine.GetQueuedTransactions(&pending, &n));
    CHECK_EQ_INT(n, 3);

    CHECK(engine.RemoveQueuedTransaction(pending[1]));
    FreePending(pending, n);

    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 2);
    CHECK_EQ_INT(QueueDepth(engine), 2);

    // Exactly the right one went, and it does not come back.
    CHECK(engine.GetQueuedTransactions(&pending, &n));
    CHECK_EQ_INT(n, 2);
    CHECK(wcsstr(Journal::PayloadOf(pending[0]), TEXT("AAA111")) != NULL);
    CHECK(wcsstr(Journal::PayloadOf(pending[1]), TEXT("CCC333")) != NULL);
    FreePending(pending, n);

    // A line that is not a queued record must be rejected rather than silently
    // decrementing the count.
    CHECK_FALSE(engine.RemoveQueuedTransaction(TEXT("not-a-record")));
    CHECK_FALSE(engine.RemoveQueuedTransaction(TEXT("")));
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 2);

    // --- Auto-sync trigger, as polled from the UI timer. --------------------
    engine.SetAutoSyncEnabled(false);
    CHECK_FALSE(engine.ShouldAutoSync(GetTickCount()));

    engine.SetAutoSyncEnabled(true);
    engine.SetAutoSyncIntervalSeconds(300);
    CHECK_EQ_INT(engine.GetAutoSyncIntervalSeconds(), 300);

    // Work is queued and no sync has run yet, so the first poll should fire.
    CHECK(engine.ShouldAutoSync(GetTickCount()));

    // Immediately after an attempt the interval must suppress the next poll,
    // otherwise the timer would start a sync on every tick.
    engine.Sync(); // fails (offline), but records the attempt
    CHECK_FALSE(engine.ShouldAutoSync(GetTickCount()));

    // Once the interval has elapsed it is due again.
    CHECK(engine.ShouldAutoSync(GetTickCount() + 301UL * 1000UL));

    // --- Clearing empties the queue for good. -------------------------------
    CHECK(engine.ClearQueue());
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 0);
    CHECK_EQ_INT(QueueDepth(engine), 0);

    // With nothing queued there is nothing to auto-sync.
    CHECK_FALSE(engine.ShouldAutoSync(GetTickCount() + 601UL * 1000UL));

    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario C - Config persistence round-trip + default fallback
 * ------------------------------------------------------------------------- */
TEST_CASE("Config: save/load round-trip and missing-file defaults")
{
    const TCHAR* path    = TEXT("/tmp/hbx_it_config.json");
    const TCHAR* missing = TEXT("/tmp/hbx_it_config_missing.json");

    DeleteFile(path);
    DeleteFile(missing);

    // --- Write a fully-populated config to disk. ----------------------------
    Config w;
    w.SetApiBaseUrl(TEXT("http://server:9000/api"));
    w.SetDeviceId(TEXT("MC75-XYZ"));
    w.SetSyncIntervalSeconds(120);
    w.SetOfflineModeEnabled(false);
    w.SetAuthToken(TEXT("tok123"));
    w.SetApiKey(TEXT("key-\"quoted\"-and\\slashed"));
    w.SetJournalPath(TEXT("\\Storage Card\\hbx.journal"));
    w.SetHomeboxInstanceId(TEXT("hb-prod"));
    w.SetNetboxInstanceId(TEXT("nb-prod"));
    w.SetActiveBackendId(TEXT("nb-prod"));
    w.SetNetboxBaseUrl(TEXT("http://netbox.warehouse.lan/api"));
    w.SetNetboxToken(TEXT("0123456789abcdef0123456789abcdef01234567"));
    w.SetNetboxAuthScheme(TEXT("Bearer"));
    w.SetAllowInsecureTls(true);
    CHECK(w.Save(path));

    // --- Load it back into a fresh instance and verify every field. ---------
    Config r;
    CHECK(r.Load(path));
    CHECK_EQ_STR(r.GetApiBaseUrl(), TEXT("http://server:9000/api"));
    CHECK_EQ_STR(r.GetDeviceId(),   TEXT("MC75-XYZ"));
    CHECK_EQ_INT(r.GetSyncIntervalSeconds(), 120);
    CHECK_FALSE(r.IsOfflineModeEnabled());
    CHECK_EQ_STR(r.GetAuthToken(), TEXT("tok123"));

    // Values containing JSON metacharacters must survive the round trip: an
    // unescaped quote used to produce a file that reloaded as defaults.
    CHECK_EQ_STR(r.GetApiKey(), TEXT("key-\"quoted\"-and\\slashed"));
    CHECK_EQ_STR(r.GetJournalPath(), TEXT("\\Storage Card\\hbx.journal"));

    // Second-backend settings have to survive the same trip. Save() rewrites
    // the file from the fields it knows, so a key it does not write is gone.
    CHECK_EQ_STR(r.GetHomeboxInstanceId(), TEXT("hb-prod"));
    CHECK_EQ_STR(r.GetNetboxInstanceId(), TEXT("nb-prod"));
    CHECK_EQ_STR(r.GetActiveBackendId(), TEXT("nb-prod"));
    CHECK_EQ_STR(r.GetNetboxBaseUrl(), TEXT("http://netbox.warehouse.lan/api"));
    CHECK_EQ_STR(r.GetNetboxToken(), TEXT("0123456789abcdef0123456789abcdef01234567"));
    CHECK_EQ_STR(r.GetNetboxAuthScheme(), TEXT("Bearer"));
    CHECK(r.IsInsecureTlsAllowed());

    // --- The token-persist path must not wipe the other backend. ------------
    // Controller::PersistAuthToken saves the whole file on the first successful
    // authentication of every run, so a NetBox deployment would lose its server
    // and token the first time an operator authenticated against HomeBox.
    r.SetAuthToken(TEXT("fresh-token"));
    CHECK(r.Save(path));

    Config after;
    CHECK(after.Load(path));
    CHECK_EQ_STR(after.GetAuthToken(), TEXT("fresh-token"));
    CHECK_EQ_STR(after.GetNetboxBaseUrl(), TEXT("http://netbox.warehouse.lan/api"));
    CHECK_EQ_STR(after.GetNetboxToken(), TEXT("0123456789abcdef0123456789abcdef01234567"));
    CHECK_EQ_STR(after.GetNetboxAuthScheme(), TEXT("Bearer"));
    CHECK_EQ_STR(after.GetActiveBackendId(), TEXT("nb-prod"));
    CHECK(after.IsInsecureTlsAllowed());

    // --- Loading a non-existent path succeeds and yields defaults. ----------
    DeleteFile(missing);
    Config d;
    // Mutate first so we can prove Load() actively resets to defaults.
    d.SetSyncIntervalSeconds(999);
    d.SetOfflineModeEnabled(false);
    CHECK(d.Load(missing)); // missing file -> defaults, returns true
    CHECK_EQ_INT(d.GetSyncIntervalSeconds(), 300);
    CHECK(d.IsOfflineModeEnabled());
    CHECK(d.GetApiBaseUrl() != NULL);
    CHECK_EQ_STR(d.GetApiBaseUrl(), TEXT("http://localhost:8080/api"));
    CHECK(d.GetJournalPath() != NULL);
    CHECK(d.GetJournalPath()[0] != 0);

    // HomeBox is the backend a device gets when the file says nothing, and its
    // default id matches the tag on records written before entries carried one.
    CHECK_EQ_STR(d.GetActiveBackendId(), TEXT("hb"));
    CHECK_EQ_STR(d.GetHomeboxInstanceId(), TEXT("hb"));
    CHECK_EQ_STR(d.GetNetboxInstanceId(), TEXT("nb"));
    CHECK_EQ_STR(d.GetNetboxAuthScheme(), TEXT("Token"));
    CHECK_EQ_STR(d.GetNetboxBaseUrl(), TEXT(""));
    CHECK_FALSE(d.IsInsecureTlsAllowed());

    DeleteFile(path);
    DeleteFile(missing);
}

/* ------------------------------------------------------------------------- *
 * Scenario C2 - an hb_conf.json written before the second backend existed
 * ------------------------------------------------------------------------- */
TEST_CASE("Config: a file without backend keys still selects HomeBox")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_config_legacy.json");
    DeleteFile(path);

    // Exactly what is deployed on a device in the field today: no activeBackend,
    // no NetBox keys, and a HomeBox instance id the operator has renamed.
    static const char* const kLegacyJson =
        "{\n"
        "  \"apiBaseUrl\": \"http://server:9000/api\",\n"
        "  \"homeboxInstanceId\": \"hb-prod\",\n"
        "  \"deviceId\": \"MC75-XYZ\"\n"
        "}\n";
    CHECK(WriteTextFile(path, kLegacyJson));

    Config c;
    CHECK(c.Load(path));

    // activeBackend is absent, so it has to follow whatever HomeBox is called
    // here -- defaulting to the literal "hb" would name nothing that exists.
    CHECK_EQ_STR(c.GetActiveBackendId(), TEXT("hb-prod"));
    CHECK_EQ_STR(c.GetApiBaseUrl(), TEXT("http://server:9000/api"));

    // And nothing invents a NetBox that was never configured.
    CHECK_EQ_STR(c.GetNetboxBaseUrl(), TEXT(""));
    CHECK_EQ_STR(c.GetNetboxToken(), TEXT(""));

    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario C3 - a save must not delete the keys the app does not own
 * ------------------------------------------------------------------------- */
TEST_CASE("Config: keys the app does not own survive a save")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_config_foreign.json");
    DeleteFile(path);

    // A deployed hb_conf.json the way an installer or a site admin leaves it:
    // the keys the app knows, plus a scalar of their own, a nested object, and
    // the annotated _readme array the shipped template carries. The legacy
    // offlineMode spelling is in there too, because Load still honours it.
    static const char* const kDeployedJson =
        "{\n"
        "  \"apiBaseUrl\": \"http://server:9000/api\",\n"
        "  \"deviceId\": \"MC75-XYZ\",\n"
        "  \"siteCode\": \"AKR-07\",\n"
        "  \"authToken\": \"\",\n"
        "  \"offlineModeEnabled\": true,\n"
        "  \"offlineMode\": false,\n"
        "  \"provisioning\": {\"wave\": 3, \"owner\": \"depot \\\"north\\\"\", \"pilot\": false},\n"
        "  \"_readme\": [\"Template for \\\\Program Files\\\\HBXClient.\", \"Line two.\"]\n"
        "}\n";
    CHECK(WriteTextFile(path, kDeployedJson));

    Config c;
    CHECK(c.Load(path));
    CHECK_EQ_STR(c.GetApiBaseUrl(), TEXT("http://server:9000/api"));
    CHECK(c.IsOfflineModeEnabled());

    // Exactly what Controller::PersistAuthToken does on the first successful
    // authentication of a run: no settings screen, no warning, whole file
    // rewritten.
    c.SetAuthToken(TEXT("tok-from-homebox"));
    CHECK(c.Save(path));

    char raw[4096];
    CHECK(ReadTextFile(path, raw, 4096));

    TCHAR* text = Str::FromUtf8Alloc(raw);
    CHECK(text != NULL);

    Models::JsonLite doc;
    CHECK(doc.Parse(text)); // what was written is still strict JSON
    delete[] text;

    // Known keys are written from the live fields...
    TCHAR buf[128];
    CHECK(doc.GetString(TEXT("authToken"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("tok-from-homebox"));
    CHECK(doc.GetString(TEXT("apiBaseUrl"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("http://server:9000/api"));

    // ...and an unknown scalar is carried through untouched.
    CHECK(doc.GetString(TEXT("siteCode"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("AKR-07"));

    // An unknown nested object keeps its members and their types, escapes and
    // all -- it is re-emitted, not flattened to a string.
    int wave = 0;
    CHECK(doc.GetNestedInt(TEXT("provisioning"), TEXT("wave"), &wave));
    CHECK_EQ_INT(wave, 3);
    CHECK(doc.GetNestedString(TEXT("provisioning"), TEXT("owner"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("depot \"north\""));

    Models::JsonLite provisioning;
    CHECK(doc.GetObject(TEXT("provisioning"), &provisioning));
    bool pilot = true;
    CHECK(provisioning.GetBool(TEXT("pilot"), &pilot));
    CHECK_FALSE(pilot);

    // The _readme array comes back with the same strings in the same order,
    // backslashes included. This is the block the shipped template documents
    // the file with, so nothing about it may be normalised away.
    int readmeIndex = -1;
    int members = doc.GetMemberCount();
    for (int i = 0; i < members; i++) {
        if (lstrcmp(doc.GetMemberName(i), TEXT("_readme")) == 0) {
            readmeIndex = i;
        }
    }
    CHECK(readmeIndex >= 0);

    TCHAR* readmeJson = doc.GetMemberJson(readmeIndex);
    CHECK(readmeJson != NULL);
    CHECK_EQ_STR(readmeJson,
        TEXT("[\"Template for \\\\Program Files\\\\HBXClient.\",\"Line two.\"]"));
    delete[] readmeJson;

    Models::JsonLite readme;
    CHECK(doc.GetArray(TEXT("_readme"), &readme));
    CHECK_EQ_INT(readme.GetArrayLength(), 2);

    // The legacy alias is owned by Config, so it is rewritten in the canonical
    // spelling rather than preserved: two keys claiming one setting would let
    // the stale one win on whichever reader looks at it first.
    CHECK_FALSE(doc.HasKey(TEXT("offlineMode")));
    bool offline = false;
    CHECK(doc.GetBool(TEXT("offlineModeEnabled"), &offline));
    CHECK(offline);

    // Saving again changes nothing, so a device that authenticates every
    // morning does not rewrite the file differently every morning.
    char again[4096];
    CHECK(c.Save(path));
    CHECK(ReadTextFile(path, again, 4096));
    CHECK_EQ_STR(again, raw);

    // And a plain reload still sees the settings it did before.
    Config reloaded;
    CHECK(reloaded.Load(path));
    CHECK_EQ_STR(reloaded.GetDeviceId(), TEXT("MC75-XYZ"));
    CHECK_EQ_STR(reloaded.GetAuthToken(), TEXT("tok-from-homebox"));
    CHECK(reloaded.IsOfflineModeEnabled());

    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario C4 - a file too broken to parse still saves as a valid config
 * ------------------------------------------------------------------------- */
TEST_CASE("Config: saving over an unparseable file still writes a valid config")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_config_broken.json");
    DeleteFile(path);

    // Hand-edited on the device and left with a trailing comma, which is enough
    // to fail a strict parse. Load falls back to its tolerant scanner.
    static const char* const kBrokenJson =
        "{\n"
        "  \"apiBaseUrl\": \"http://server:9000/api\",\n"
        "  \"deviceId\": \"MC75-XYZ\",\n"
        "  \"siteCode\": \"AKR-07\",\n"
        "  \"syncIntervalSeconds\": 120,\n"
        "}\n";
    CHECK(WriteTextFile(path, kBrokenJson));

    Config c;
    CHECK(c.Load(path));
    CHECK_EQ_STR(c.GetApiBaseUrl(), TEXT("http://server:9000/api"));
    CHECK_EQ_INT(c.GetSyncIntervalSeconds(), 120);

    c.SetAuthToken(TEXT("tok-from-homebox"));
    CHECK(c.Save(path));

    char raw[2048];
    CHECK(ReadTextFile(path, raw, 2048));

    TCHAR* text = Str::FromUtf8Alloc(raw);
    CHECK(text != NULL);

    Models::JsonLite doc;
    CHECK(doc.Parse(text)); // the replacement parses strictly again
    delete[] text;

    // Nothing could be enumerated out of the broken file, so the foreign key is
    // gone -- the deliberate trade: dropping siteCode costs an annotation,
    // refusing to save would cost the operator's settings.
    CHECK_FALSE(doc.HasKey(TEXT("siteCode")));

    Config reloaded;
    CHECK(reloaded.Load(path));
    CHECK_EQ_STR(reloaded.GetApiBaseUrl(), TEXT("http://server:9000/api"));
    CHECK_EQ_STR(reloaded.GetDeviceId(), TEXT("MC75-XYZ"));
    CHECK_EQ_INT(reloaded.GetSyncIntervalSeconds(), 120);
    CHECK_EQ_STR(reloaded.GetAuthToken(), TEXT("tok-from-homebox"));

    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * A backend that records what it was asked to replay.
 *
 * What is under test in scenarios D-G is routing -- which backend a queued
 * entry reaches -- so nothing here leaves the process. The base URL points at a
 * name that always resolves, because the engine refuses to replay an entry
 * whose backend it cannot reach and the batch would otherwise stop before any
 * routing happened.
 * ------------------------------------------------------------------------- */
class RecordingBackend : public InventoryBackend {
public:
    enum {
        KIND_CAP = 8,
        ID_CAP   = 32,
        URL_CAP  = 128,
        TYPE_CAP = 64,
        DATA_CAP = 256
    };

    RecordingBackend(const TCHAR* kind, const TCHAR* instanceId)
        : m_replayCount(0)
        , m_result(REPLAY_SENT)
    {
        Str::Copy(m_kind, KIND_CAP, kind);
        Str::Copy(m_instanceId, ID_CAP, instanceId);
        Str::Copy(m_baseUrl, URL_CAP, kResolvable);
        m_lastType[0] = 0;
        m_lastData[0] = 0;
    }

    // ---- test inspection -----------------------------------------------
    int ReplayCount() const { return m_replayCount; }
    const TCHAR* LastType() const { return m_lastType; }
    const TCHAR* LastData() const { return m_lastData; }
    void SetReplayResult(ReplayResult result) { m_result = result; }

    // ---- InventoryBackend ----------------------------------------------
    const TCHAR* GetKind() const { return m_kind; }
    const TCHAR* GetInstanceId() const { return m_instanceId; }
    const TCHAR* GetDisplayName() const { return m_instanceId; }

    void SetBaseUrl(const TCHAR* baseUrl) { Str::Copy(m_baseUrl, URL_CAP, baseUrl ? baseUrl : TEXT("")); }
    const TCHAR* GetBaseUrl() const { return m_baseUrl; }
    void SetRequestTimeout(DWORD) {}

    bool IsAuthenticated() const { return true; }
    int GetLastStatusCode() const { return 200; }
    bool SessionIsRenewable() const { return false; }
    bool Authenticate() { return true; }

    bool LookupByCode(const TCHAR*, Models::AssetSummary*) { return false; }
    int GetMatchCount() const { return 0; }
    bool GetMatch(int, Models::AssetSummary*) { return false; }

    bool SupportsStatus() const { return false; }
    int GetStatusChoiceCount() const { return 0; }
    const TCHAR* GetStatusChoice(int) const { return NULL; }

    ReplayResult Replay(const TCHAR* type, const TCHAR* data)
    {
        m_replayCount++;
        Str::Copy(m_lastType, TYPE_CAP, type ? type : TEXT(""));
        Str::Copy(m_lastData, DATA_CAP, data ? data : TEXT(""));
        return m_result;
    }

private:
    TCHAR m_kind[KIND_CAP];
    TCHAR m_instanceId[ID_CAP];
    TCHAR m_baseUrl[URL_CAP];
    TCHAR m_lastType[TYPE_CAP];
    TCHAR m_lastData[DATA_CAP];
    int m_replayCount;
    ReplayResult m_result;
};

/* ------------------------------------------------------------------------- *
 * Scenario D - an entry for a backend nobody configured
 * ------------------------------------------------------------------------- */
TEST_CASE("Backends: an entry for an unregistered backend is skipped, not failed")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_backend_skip.dat");
    DeleteFile(path);

    Journal journal;
    CHECK(journal.Initialize(path));

    RecordingBackend homebox(TEXT("hb"), TEXT("hb"));
    SyncEngine engine(&homebox, &journal);

    // The one-argument-plus-journal form still registers and activates.
    CHECK(engine.GetActiveBackend() == &homebox);
    CHECK_EQ_INT(engine.GetBackendCount(), 1);

    CHECK(engine.QueueScan(TEXT("ITEM-1"), NULL));

    // Work left behind by a NetBox that has since been taken out of the
    // configuration. The type is already qualified, so it is stored as-is.
    CHECK(engine.QueueTransaction(TEXT("nb-retired.DEVICE_MOVE"),
                                  TEXT("MOVE:42:rack-17")));
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 2);

    CHECK(engine.Sync());

    // The orphan must not drag the whole sync down with it: an operator whose
    // status line says "failed" after every sync stops believing the queue.
    CHECK(engine.GetSyncStatus() == SyncEngine::SYNC_SUCCESS);
    CHECK_EQ_INT(engine.GetSkippedCount(), 1);

    // The addressable entry went; the orphan is still there.
    CHECK_EQ_INT(homebox.ReplayCount(), 1);
    CHECK_EQ_STR(homebox.LastType(), TEXT("ITEM_SCAN"));
    CHECK_EQ_STR(homebox.LastData(), TEXT("SCAN:ITEM-1"));
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 1);
    CHECK_EQ_INT(QueueDepth(engine), 1);

    // Repeat syncs keep skipping it instead of degrading into a failure, and
    // never replay it against the backend that happens to be active.
    CHECK(engine.Sync());
    CHECK(engine.GetSyncStatus() == SyncEngine::SYNC_SUCCESS);
    CHECK_EQ_INT(engine.GetSkippedCount(), 1);
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 1);
    CHECK_EQ_INT(homebox.ReplayCount(), 1);

    // Nothing was lost: configure that backend again and the entry drains.
    RecordingBackend netbox(TEXT("nb"), TEXT("nb-retired"));
    engine.RegisterBackend(&netbox);
    CHECK_EQ_INT(engine.GetBackendCount(), 2);

    CHECK(engine.Sync());
    CHECK_EQ_INT(engine.GetSkippedCount(), 0);
    CHECK_EQ_INT(netbox.ReplayCount(), 1);
    CHECK_EQ_STR(netbox.LastType(), TEXT("DEVICE_MOVE"));
    CHECK_EQ_STR(netbox.LastData(), TEXT("MOVE:42:rack-17"));
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 0);

    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario E - a queue written before entries carried a backend tag
 * ------------------------------------------------------------------------- */
TEST_CASE("Backends: an untagged legacy record still replays against HomeBox")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_backend_legacy.dat");
    DeleteFile(path);

    Journal journal;
    CHECK(journal.Initialize(path));

    // Exactly what an installed build left in the journal before backend tags
    // existed: "[tick] TYPE: DATA" with no instance id in front of the type.
    CHECK(journal.QueueTransaction(TEXT("[51234] ITEM_SCAN: SCAN:LEGACY-1"), NULL));
    CHECK(journal.QueueTransaction(
        TEXT("[51290] ITEM_UPDATE: UPDATE:{\"barcode\":\"LEGACY-2\",\"quantity\":3}"), NULL));

    // NetBox is registered first, so it is the active backend, and HomeBox is
    // deliberately not called "hb" -- an untagged record has to be routed by
    // what a backend *is*, not by what it was named or by what happens to be
    // active, or upgrading a renamed installation strands its whole queue.
    RecordingBackend netbox(TEXT("nb"), TEXT("nb-prod"));
    RecordingBackend homebox(TEXT("hb"), TEXT("hb-prod"));

    SyncEngine engine(&netbox, &journal);
    engine.RegisterBackend(&homebox);
    CHECK(engine.GetActiveBackend() == &netbox);

    CHECK(engine.Sync());

    CHECK_EQ_INT(netbox.ReplayCount(), 0);
    CHECK_EQ_INT(homebox.ReplayCount(), 2);
    CHECK_EQ_STR(homebox.LastType(), TEXT("ITEM_UPDATE"));
    CHECK_EQ_STR(homebox.LastData(),
                 TEXT("UPDATE:{\"barcode\":\"LEGACY-2\",\"quantity\":3}"));
    CHECK_EQ_INT(engine.GetSkippedCount(), 0);
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 0);

    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario F - switching the active backend
 * ------------------------------------------------------------------------- */
TEST_CASE("Backends: switching backends never replays one queue against the other")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_backend_switch.dat");
    DeleteFile(path);

    Journal journal;
    CHECK(journal.Initialize(path));

    RecordingBackend homebox(TEXT("hb"), TEXT("hb"));
    RecordingBackend netbox(TEXT("nb"), TEXT("nb-prod"));

    SyncEngine engine(&homebox, &journal);
    engine.RegisterBackend(&netbox);

    // Scanned while HomeBox was selected.
    CHECK(engine.QueueScan(TEXT("ITEM-1"), TEXT("LOC-A2")));

    // The operator switches systems. The queue does not drain on the way out.
    CHECK(engine.SetActiveBackend(TEXT("nb-prod")));
    CHECK(engine.GetActiveBackend() == &netbox);
    CHECK(engine.QueueTransaction(TEXT("DEVICE_MOVE"), TEXT("MOVE:9:rack-3")));

    // An id nobody registered must not fall back to whatever is active: sending
    // a move to the wrong instance succeeds and moves the wrong device.
    CHECK_FALSE(engine.SetActiveBackend(TEXT("nb-staging")));
    CHECK(engine.GetActiveBackend() == &netbox);
    CHECK(engine.FindBackend(TEXT("nb-staging")) == NULL);
    CHECK(engine.FindBackend(TEXT("hb")) == &homebox);

    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 2);
    CHECK(engine.Sync());

    // Each entry went home, and only home.
    CHECK_EQ_INT(homebox.ReplayCount(), 1);
    CHECK_EQ_STR(homebox.LastType(), TEXT("ITEM_SCAN"));
    CHECK_EQ_STR(homebox.LastData(), TEXT("SCANLOC:6:ITEM-1LOC-A2"));

    CHECK_EQ_INT(netbox.ReplayCount(), 1);
    CHECK_EQ_STR(netbox.LastType(), TEXT("DEVICE_MOVE"));
    CHECK_EQ_STR(netbox.LastData(), TEXT("MOVE:9:rack-3"));

    CHECK_EQ_INT(engine.GetSkippedCount(), 0);
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 0);

    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario G - the three replay outcomes are not two
 * ------------------------------------------------------------------------- */
TEST_CASE("Backends: a transient replay failure keeps its entry queued")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_backend_retry.dat");
    DeleteFile(path);

    Journal journal;
    CHECK(journal.Initialize(path));

    RecordingBackend homebox(TEXT("hb"), TEXT("hb"));
    SyncEngine engine(&homebox, &journal);

    CHECK(engine.QueueScan(TEXT("ITEM-1"), NULL));

    // The server was reached and refused the write - unlike a skip, this is
    // worth retrying, and unlike a skip it is a real failure.
    homebox.SetReplayResult(REPLAY_RETRY);
    CHECK_FALSE(engine.Sync());
    CHECK(engine.GetSyncStatus() == SyncEngine::SYNC_FAILED);
    CHECK_EQ_INT(engine.GetSkippedCount(), 0);
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 1);
    CHECK_EQ_INT(homebox.ReplayCount(), 1);

    // When it clears, the same entry goes through untouched.
    homebox.SetReplayResult(REPLAY_SENT);
    CHECK(engine.Sync());
    CHECK(engine.GetSyncStatus() == SyncEngine::SYNC_SUCCESS);
    CHECK_EQ_INT(homebox.ReplayCount(), 2);
    CHECK_EQ_STR(homebox.LastData(), TEXT("SCAN:ITEM-1"));
    CHECK_EQ_INT(engine.GetQueuedTransactionCount(), 0);

    DeleteFile(path);
}
