/*
 * test_journal.cpp  --  Unit tests for HBX::Journal
 * -------------------------------------------------
 * Host-side (Win32 shim) unit tests. In this build TCHAR == char and
 * TEXT("x") expands to a narrow "x". All string literals handed to the
 * code under test are wrapped in TEXT(...).
 *
 * The journal is a file-backed audit log. Each test uses a unique temp
 * path under /tmp and DeleteFile()s it first so every run starts clean.
 */
#include "test_framework.hpp"
#include <windows.h>
#include "Journal.hpp"

using namespace HBX;

// Helper: free the TCHAR** array returned by GetPendingTransactions.
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

TEST_CASE("Journal: Initialize creates file and count starts at zero")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_init.dat");
    DeleteFile(path); // fine if it did not exist

    Journal j;
    CHECK(j.Initialize(path));
    CHECK_EQ_INT(j.GetTransactionCount(), 0);

    DeleteFile(path);
}

TEST_CASE("Journal: LogTransaction x3 yields count 3 and 3 pending entries")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_trans.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t1"), TEXT("first item")));
    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t2"), TEXT("second item")));
    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t3"), TEXT("third item")));

    CHECK_EQ_INT(j.GetTransactionCount(), 3);

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 3);
    for (int i = 0; i < n; i++) {
        CHECK(pending[i] != NULL);
        CHECK(lstrlen(pending[i]) > 0);
    }
    FreePending(pending, n);

    DeleteFile(path);
}

TEST_CASE("Journal: MarkTransactionSynced decrements the count")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_synced.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t1"), TEXT("first item")));
    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t2"), TEXT("second item")));
    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t3"), TEXT("third item")));
    CHECK_EQ_INT(j.GetTransactionCount(), 3);

    CHECK(j.MarkTransactionSynced(TEXT("t1")));
    CHECK_EQ_INT(j.GetTransactionCount(), 2);

    DeleteFile(path);
}

TEST_CASE("Journal: LogInfo and LogError do not change the count")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_infoerr.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t1"), TEXT("only item")));
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    CHECK(j.LogInfo(TEXT("informational message")));
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    CHECK(j.LogError(TEXT("E001"), TEXT("something went wrong")));
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    DeleteFile(path);
}

TEST_CASE("Journal: Clear truncates file and resets pending to zero")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_clear.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t1"), TEXT("first item")));
    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t2"), TEXT("second item")));
    CHECK_EQ_INT(j.GetTransactionCount(), 2);

    CHECK(j.Clear());
    CHECK_EQ_INT(j.GetTransactionCount(), 0);

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 0);
    FreePending(pending, n);

    DeleteFile(path);
}

TEST_CASE("Journal: Compact drops synced transactions, keeps unsynced")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_compact.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t1"), TEXT("first item")));
    CHECK(j.LogTransaction(TEXT("ADD"), TEXT("t2"), TEXT("second item")));
    CHECK(j.LogError(TEXT("E001"), TEXT("an error line")));

    // Mark one transaction synced the way SyncEngine::Sync does: by passing the
    // *actual* pending transaction line text back to MarkTransactionSynced.
    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 2);
    CHECK(pending != NULL && pending[0] != NULL);

    CHECK(j.MarkTransactionSynced(pending[0]));
    // In-memory count drops immediately from 2 to 1.
    CHECK_EQ_INT(j.GetTransactionCount(), 1);
    FreePending(pending, n);

    // Compaction rewrites the journal, correlating the SYNCED marker with its
    // transaction so the synced entry is truly removed (not resurrected).
    CHECK(j.Compact());
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    // Exactly the other (unsynced) transaction should remain pending.
    TCHAR** remaining = NULL;
    int rn = 0;
    CHECK(j.GetPendingTransactions(&remaining, &rn));
    CHECK_EQ_INT(rn, 1);
    FreePending(remaining, rn);

    DeleteFile(path);
}
