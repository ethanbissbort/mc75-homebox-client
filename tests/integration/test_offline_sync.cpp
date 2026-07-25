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
 *                                        keys, and the missing-file fallback.
 *
 * The HbClient here points at an unresolvable host, so the engine is genuinely
 * offline for the whole run -- which is exactly the state these tests care
 * about. Replaying against a live server needs a server and belongs in a
 * device-side test, not here.
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
#include "SyncEngine.hpp"
#include "HbClient.hpp"

using namespace HBX;

// A host name that cannot resolve, so CheckConnectivity() reports offline
// without waiting on a real DNS server.
static const TCHAR* const kUnreachable =
    TEXT("http://mc75-offline.invalid:8080/api");

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

    DeleteFile(path);
    DeleteFile(missing);
}
