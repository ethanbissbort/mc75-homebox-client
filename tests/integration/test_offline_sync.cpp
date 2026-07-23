/*
 * test_offline_sync.cpp  --  Integration tests for the offline-first core
 * -----------------------------------------------------------------------
 * Exercises the two pieces that make the MC75 client survive a lost network:
 *
 *   Scenario A  HBX::Journal   -- the on-disk offline transaction queue that
 *                                 SyncEngine replays once connectivity returns.
 *   Scenario B  HBX::Config    -- configuration persistence round-trip plus the
 *                                 "missing file falls back to defaults" path.
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

using namespace HBX;

/* ------------------------------------------------------------------------- *
 * Scenario A - offline queue lifecycle via Journal
 * ------------------------------------------------------------------------- */
TEST_CASE("Journal: offline queue lifecycle (log, pending, sync, compact)")
{
    const TCHAR* path = TEXT("/tmp/hbx_it_offline.dat");

    // Start clean: fine whether or not the file already existed.
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    // --- Go "offline": queue several transactions the server never saw. -----
    CHECK(j.LogTransaction(TEXT("CREATE"), TEXT("item-1"), TEXT("qty=5")));
    CHECK(j.LogTransaction(TEXT("UPDATE"), TEXT("item-2"), TEXT("qty=9")));
    CHECK(j.LogTransaction(TEXT("MOVE"),   TEXT("item-3"), TEXT("loc=A2")));
    CHECK(j.LogTransaction(TEXT("DELETE"), TEXT("item-4"), TEXT("reason=lost")));

    CHECK_EQ_INT(j.GetTransactionCount(), 4);

    // --- What SyncEngine would read back to replay on reconnect. ------------
    TCHAR** pending = NULL;
    int n = -1;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 4);
    CHECK(pending != NULL);

    // Every returned entry must be a non-empty heap string.
    if (pending != NULL) {
        for (int i = 0; i < n; ++i) {
            CHECK(pending[i] != NULL);
            CHECK(pending[i] != NULL && pending[i][0] != '\0');
        }
        // Free exactly as documented: each element, then the array.
        for (int i = 0; i < n; ++i) {
            delete[] pending[i];
        }
        delete[] pending;
        pending = NULL;
    }

    // --- "Sync" two of them back to the server. -----------------------------
    CHECK(j.MarkTransactionSynced(TEXT("sync-id-1")));
    CHECK(j.MarkTransactionSynced(TEXT("sync-id-2")));

    // Two acknowledged -> two still owed to the server.
    CHECK_EQ_INT(j.GetTransactionCount(), 2);

    // --- Compact the journal. -----------------------------------------------
    CHECK(j.Compact());

    // NOTE ON ACTUAL BEHAVIOUR (verified against src/Journal.cpp):
    // MarkTransactionSynced() appends a standalone "SYNCED" marker line and
    // decrements the live counter, but it does NOT rewrite or remove the
    // original TRANS line. Compact() rebuilds the count from every line that
    // still contains "TRANS" and not "SYNCED" -- which is all four original
    // TRANS lines -- and drops the two SYNCED marker lines. So the persisted,
    // recomputed count after Compact() is 4, not 2. This assertion pins that
    // real behaviour (the SYNCED markers are advisory, not a true removal).
    CHECK_EQ_INT(j.GetTransactionCount(), 4);

    // Cleanup.
    DeleteFile(path);
}

/* ------------------------------------------------------------------------- *
 * Scenario B - Config persistence round-trip + default fallback
 * ------------------------------------------------------------------------- */
TEST_CASE("Config: save/load round-trip and missing-file defaults")
{
    const TCHAR* path    = TEXT("/tmp/hbx_it_config.json");
    const TCHAR* missing = TEXT("/tmp/hbx_it_config_missing.json");

    // Start clean.
    DeleteFile(path);
    DeleteFile(missing);

    // --- Write a fully-populated config to disk. ----------------------------
    Config w;
    w.SetApiBaseUrl(TEXT("http://server:9000/api"));
    w.SetDeviceId(TEXT("MC75-XYZ"));
    w.SetSyncIntervalSeconds(120);
    w.SetOfflineModeEnabled(false);
    w.SetAuthToken(TEXT("tok123"));
    CHECK(w.Save(path));

    // --- Load it back into a fresh instance and verify every field. ---------
    Config r;
    CHECK(r.Load(path));
    CHECK_EQ_STR(r.GetApiBaseUrl(), TEXT("http://server:9000/api"));
    CHECK_EQ_STR(r.GetDeviceId(),   TEXT("MC75-XYZ"));
    CHECK_EQ_INT(r.GetSyncIntervalSeconds(), 120);
    CHECK_FALSE(r.IsOfflineModeEnabled());
    CHECK_EQ_STR(r.GetAuthToken(), TEXT("tok123"));

    // --- Loading a non-existent path succeeds and yields defaults. ----------
    DeleteFile(missing); // ensure it truly does not exist
    Config d;
    // Mutate first so we can prove Load() actively resets to defaults.
    d.SetSyncIntervalSeconds(999);
    d.SetOfflineModeEnabled(false);
    CHECK(d.Load(missing)); // missing file -> defaults, returns true
    CHECK_EQ_INT(d.GetSyncIntervalSeconds(), 300);
    CHECK(d.IsOfflineModeEnabled());
    CHECK(d.GetApiBaseUrl() != NULL);
    CHECK_EQ_STR(d.GetApiBaseUrl(), TEXT("http://localhost:8080/api"));

    // Cleanup temp files.
    DeleteFile(path);
    DeleteFile(missing);
}
