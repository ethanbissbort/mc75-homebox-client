/*
 * test_journal.cpp  --  Unit tests for HBX::Journal
 * -------------------------------------------------
 * Host-side (Win32 shim) unit tests. In this build TCHAR == char and
 * TEXT("x") expands to a narrow "x". All string literals handed to the
 * code under test are wrapped in TEXT(...).
 *
 * The journal is both the audit log and the durable offline queue. Each test
 * uses a unique temp path and DeleteFile()s it first so every run starts clean.
 *
 * Several cases here are regression tests for defects found in review; each is
 * labelled with what it would catch if it came back.
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

// Helper: how many entries are actually pending on disk right now.
static int PendingCount(Journal& j)
{
    TCHAR** pending = NULL;
    int n = 0;
    if (!j.GetPendingTransactions(&pending, &n)) {
        return -1;
    }
    FreePending(pending, n);
    return n;
}

TEST_CASE("Journal: Initialize creates file and count starts at zero")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_init.dat");
    DeleteFile(path); // fine if it did not exist

    Journal j;
    CHECK(j.Initialize(path));
    CHECK_EQ_INT(j.GetTransactionCount(), 0);
    CHECK_EQ_INT(PendingCount(j), 0);

    DeleteFile(path);
}

TEST_CASE("Journal: QueueTransaction x3 yields count 3 and 3 pending entries")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_queue.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    DWORD s1 = 0, s2 = 0, s3 = 0;
    CHECK(j.QueueTransaction(TEXT("[100] ITEM_SCAN: SCAN:111"), &s1));
    CHECK(j.QueueTransaction(TEXT("[101] ITEM_SCAN: SCAN:222"), &s2));
    CHECK(j.QueueTransaction(TEXT("[102] ITEM_SCAN: SCAN:333"), &s3));

    CHECK_EQ_INT(j.GetTransactionCount(), 3);

    // Sequence numbers must be unique and strictly increasing: the SYNCED
    // markers correlate by sequence, so a repeat would acknowledge the wrong
    // record.
    CHECK(s1 < s2);
    CHECK(s2 < s3);

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 3);
    for (int i = 0; i < n; i++) {
        CHECK(pending[i] != NULL);
        CHECK(lstrlen(pending[i]) > 0);
    }

    // Entries come back oldest first, and the payload survives the round trip.
    CHECK_EQ_STR(Journal::PayloadOf(pending[0]), TEXT("[100] ITEM_SCAN: SCAN:111"));
    CHECK_EQ_STR(Journal::PayloadOf(pending[2]), TEXT("[102] ITEM_SCAN: SCAN:333"));

    DWORD parsed = 0;
    CHECK(Journal::ParseSequence(pending[0], &parsed));
    CHECK_EQ_INT((int)parsed, (int)s1);

    FreePending(pending, n);
    DeleteFile(path);
}

TEST_CASE("Journal: audit logging never creates queue work")
{
    // Regression: LogTransaction used to write a record labelled TRANS, so
    // Controller's per-scan audit call turned into a queue entry that could
    // never be replayed and permanently degraded the sync status.
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_audit.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.LogTransaction(TEXT("SCAN"), TEXT("123456789"), TEXT("Barcode scanned")));
    CHECK(j.LogInfo(TEXT("informational message")));
    CHECK(j.LogError(TEXT("E001"), TEXT("something went wrong")));

    CHECK_EQ_INT(j.GetTransactionCount(), 0);
    CHECK_EQ_INT(PendingCount(j), 0);

    // Real queue work still counts, and audit records around it are ignored.
    CHECK(j.QueueTransaction(TEXT("[1] ITEM_SCAN: SCAN:999"), NULL));
    CHECK(j.LogTransaction(TEXT("SCAN"), TEXT("999"), TEXT("Barcode scanned")));

    CHECK_EQ_INT(j.GetTransactionCount(), 1);
    CHECK_EQ_INT(PendingCount(j), 1);

    DeleteFile(path);
}

TEST_CASE("Journal: a synced transaction is never handed out again")
{
    // Regression: GetPendingTransactions filtered lines with
    // strstr(line,"TRANS") && !strstr(line,"SYNCED"). The original TRANS line
    // never gains the substring "SYNCED", so every already-synced transaction
    // was replayed on every sync, forever -- duplicate server operations, and
    // newer entries starved behind the stale ones.
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_nodup.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.QueueTransaction(TEXT("[1] ITEM_SCAN: SCAN:aaa"), NULL));
    CHECK(j.QueueTransaction(TEXT("[2] ITEM_SCAN: SCAN:bbb"), NULL));
    CHECK(j.QueueTransaction(TEXT("[3] ITEM_SCAN: SCAN:ccc"), NULL));

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 3);

    // Acknowledge the middle one, exactly as SyncEngine::Sync does.
    CHECK(j.MarkTransactionSynced(pending[1]));
    CHECK_EQ_INT(j.GetTransactionCount(), 2);
    FreePending(pending, n);

    // It must be gone from the pending set, and the other two must remain.
    TCHAR** after = NULL;
    int an = 0;
    CHECK(j.GetPendingTransactions(&after, &an));
    CHECK_EQ_INT(an, 2);
    CHECK_EQ_STR(Journal::PayloadOf(after[0]), TEXT("[1] ITEM_SCAN: SCAN:aaa"));
    CHECK_EQ_STR(Journal::PayloadOf(after[1]), TEXT("[3] ITEM_SCAN: SCAN:ccc"));
    FreePending(after, an);

    // Acknowledging everything drains the queue.
    TCHAR** rest = NULL;
    int rn = 0;
    CHECK(j.GetPendingTransactions(&rest, &rn));
    for (int i = 0; i < rn; i++) {
        CHECK(j.MarkTransactionSynced(rest[i]));
    }
    FreePending(rest, rn);

    CHECK_EQ_INT(j.GetTransactionCount(), 0);
    CHECK_EQ_INT(PendingCount(j), 0);

    DeleteFile(path);
}

TEST_CASE("Journal: pending transactions survive a restart")
{
    // Regression: Initialize never rebuilt the pending count from disk, so
    // after a restart (a battery swap on the MC75 ends the process)
    // GetPendingTransactions early-returned 0 and Sync() reported success while
    // queued work sat unsent on disk.
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_restart.dat");
    DeleteFile(path);

    {
        Journal first;
        CHECK(first.Initialize(path));
        CHECK(first.QueueTransaction(TEXT("[1] ITEM_SCAN: SCAN:aaa"), NULL));
        CHECK(first.QueueTransaction(TEXT("[2] ITEM_SCAN: SCAN:bbb"), NULL));
        CHECK(first.QueueTransaction(TEXT("[3] ITEM_SCAN: SCAN:ccc"), NULL));

        TCHAR** pending = NULL;
        int n = 0;
        CHECK(first.GetPendingTransactions(&pending, &n));
        CHECK(first.MarkTransactionSynced(pending[0]));
        FreePending(pending, n);

        CHECK_EQ_INT(first.GetTransactionCount(), 2);
    } // destructor closes the file, simulating process exit

    Journal second;
    CHECK(second.Initialize(path));

    // The count is recovered without anyone having to enumerate first.
    CHECK_EQ_INT(second.GetTransactionCount(), 2);

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(second.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_STR(Journal::PayloadOf(pending[0]), TEXT("[2] ITEM_SCAN: SCAN:bbb"));
    CHECK_EQ_STR(Journal::PayloadOf(pending[1]), TEXT("[3] ITEM_SCAN: SCAN:ccc"));
    FreePending(pending, n);

    // Sequence numbering must continue past the highest one already on disk,
    // otherwise a reused number would be matched by the old SYNCED marker and
    // the new transaction would vanish before it was ever sent.
    DWORD next = 0;
    CHECK(second.QueueTransaction(TEXT("[4] ITEM_SCAN: SCAN:ddd"), &next));
    CHECK(next > 3);
    CHECK_EQ_INT(PendingCount(second), 3);

    DeleteFile(path);
}

TEST_CASE("Journal: a payload containing the word SYNCED is still tracked")
{
    // Regression: classification was done with substring tests, so a payload
    // that merely contained "SYNCED" was mistaken for an acknowledgement and
    // the transaction was dropped without ever being sent.
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_substring.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.QueueTransaction(
        TEXT("[1] ITEM_UPDATE: UPDATE:{\"name\":\"BOX SYNCED TRANS\",\"quantity\":1}"), NULL));
    CHECK(j.QueueTransaction(TEXT("[2] ITEM_SCAN: SCAN:normal"), NULL));

    CHECK_EQ_INT(j.GetTransactionCount(), 2);
    CHECK_EQ_INT(PendingCount(j), 2);

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_STR(Journal::PayloadOf(pending[0]),
        TEXT("[1] ITEM_UPDATE: UPDATE:{\"name\":\"BOX SYNCED TRANS\",\"quantity\":1}"));
    CHECK(j.MarkTransactionSynced(pending[0]));
    FreePending(pending, n);

    CHECK_EQ_INT(PendingCount(j), 1);

    // And it survives compaction + a restart with the right one left.
    CHECK(j.Compact());
    CHECK_EQ_INT(PendingCount(j), 1);

    DeleteFile(path);
}

TEST_CASE("Journal: long payloads round-trip without truncation")
{
    // Regression: MarkTransactionSynced copied the whole transaction line into
    // syncEntry[512] with wsprintf, and records were assembled with wsprintf,
    // which caps at 1024 characters on Windows CE. An ITEM_UPDATE carrying a
    // serialised item easily exceeds both.
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_long.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    const int kLen = 2000;
    TCHAR* payload = new TCHAR[kLen + 40];
    lstrcpy(payload, TEXT("[7] ITEM_UPDATE: UPDATE:"));
    int base = lstrlen(payload);
    for (int i = 0; i < kLen; i++) {
        payload[base + i] = (TCHAR)('a' + (i % 26));
    }
    payload[base + kLen] = 0;

    CHECK(j.QueueTransaction(payload, NULL));
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 1);

    // The full payload must come back byte for byte.
    CHECK_EQ_STR(Journal::PayloadOf(pending[0]), payload);

    // And acknowledging a long line must work rather than overrunning a buffer.
    CHECK(j.MarkTransactionSynced(pending[0]));
    FreePending(pending, n);

    CHECK_EQ_INT(PendingCount(j), 0);

    delete[] payload;
    DeleteFile(path);
}

TEST_CASE("Journal: high-bit payload bytes survive the on-disk encoding")
{
    // Records are UTF-8 on disk. The previous writer truncated each character
    // to a single byte, corrupting anything outside US-ASCII.
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_utf8.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    TCHAR payload[64];
    int p = 0;
    const TCHAR* prefix = TEXT("[1] ITEM_UPDATE: UPDATE:");
    while (prefix[p] != 0) { payload[p] = prefix[p]; p++; }
    payload[p++] = (TCHAR)0xC3;   // as UTF-8 bytes, "Ã©" -> U+00E9
    payload[p++] = (TCHAR)0xA9;
    payload[p++] = (TCHAR)'!';
    payload[p] = 0;

    CHECK(j.QueueTransaction(payload, NULL));

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_STR(Journal::PayloadOf(pending[0]), payload);
    FreePending(pending, n);

    DeleteFile(path);
}

TEST_CASE("Journal: Clear truncates file and resets pending to zero")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_clear.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.QueueTransaction(TEXT("[1] ITEM_SCAN: SCAN:aaa"), NULL));
    CHECK(j.QueueTransaction(TEXT("[2] ITEM_SCAN: SCAN:bbb"), NULL));
    CHECK_EQ_INT(j.GetTransactionCount(), 2);

    CHECK(j.Clear());
    CHECK_EQ_INT(j.GetTransactionCount(), 0);
    CHECK_EQ_INT(PendingCount(j), 0);

    // A transaction queued after the clear must still be visible: sequence
    // numbers continue rather than restarting, so a stale marker cannot match.
    CHECK(j.QueueTransaction(TEXT("[3] ITEM_SCAN: SCAN:ccc"), NULL));
    CHECK_EQ_INT(PendingCount(j), 1);

    DeleteFile(path);
}

TEST_CASE("Journal: Compact drops synced transactions, keeps unsynced")
{
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_compact.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));

    CHECK(j.QueueTransaction(TEXT("[1] ITEM_SCAN: SCAN:aaa"), NULL));
    CHECK(j.QueueTransaction(TEXT("[2] ITEM_SCAN: SCAN:bbb"), NULL));
    CHECK(j.LogError(TEXT("E001"), TEXT("an error line")));

    TCHAR** pending = NULL;
    int n = 0;
    CHECK(j.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 2);
    CHECK(j.MarkTransactionSynced(pending[0]));
    CHECK_EQ_INT(j.GetTransactionCount(), 1);
    FreePending(pending, n);

    CHECK(j.Compact());
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    TCHAR** remaining = NULL;
    int rn = 0;
    CHECK(j.GetPendingTransactions(&remaining, &rn));
    CHECK_EQ_INT(rn, 1);
    CHECK_EQ_STR(Journal::PayloadOf(remaining[0]), TEXT("[2] ITEM_SCAN: SCAN:bbb"));
    FreePending(remaining, rn);

    // Compaction must not resurrect the acknowledged entry after a restart.
    Journal reopened;
    CHECK(reopened.Initialize(path));
    CHECK_EQ_INT(reopened.GetTransactionCount(), 1);

    DeleteFile(path);
}

TEST_CASE("Journal: automatic compaction bounds the file without losing work")
{
    // Regression: Compact() was never called from production code, so the
    // journal grew without bound on a device with a small persistent store.
    const TCHAR* path = TEXT("/tmp/hbx_ut_journal_autocompact.dat");
    DeleteFile(path);

    Journal j;
    CHECK(j.Initialize(path));
    j.SetMaxFileBytes(4096);

    // Churn well past the threshold: queue and immediately acknowledge.
    for (int i = 0; i < 200; i++) {
        TCHAR payload[64];
        payload[0] = 0;
        lstrcat(payload, TEXT("[9] ITEM_SCAN: SCAN:"));
        TCHAR digits[16];
        wsprintf(digits, TEXT("%d"), i);
        lstrcat(payload, digits);

        CHECK(j.QueueTransaction(payload, NULL));

        TCHAR** pending = NULL;
        int n = 0;
        CHECK(j.GetPendingTransactions(&pending, &n));
        for (int k = 0; k < n; k++) {
            CHECK(j.MarkTransactionSynced(pending[k]));
        }
        FreePending(pending, n);
    }

    CHECK_EQ_INT(j.GetTransactionCount(), 0);

    // One unacknowledged entry must survive whatever compaction happened.
    CHECK(j.QueueTransaction(TEXT("[10] ITEM_SCAN: SCAN:survivor"), NULL));
    CHECK_EQ_INT(PendingCount(j), 1);

    Journal reopened;
    CHECK(reopened.Initialize(path));
    CHECK_EQ_INT(reopened.GetTransactionCount(), 1);
    TCHAR** pending = NULL;
    int n = 0;
    CHECK(reopened.GetPendingTransactions(&pending, &n));
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_STR(Journal::PayloadOf(pending[0]), TEXT("[10] ITEM_SCAN: SCAN:survivor"));
    FreePending(pending, n);

    DeleteFile(path);
}

TEST_CASE("Journal: re-Initialize on the same object is safe")
{
    // Regression: re-initialising leaked the previously open handle, and the
    // share mode of 0 made re-opening the same path fail on the device.
    const TCHAR* pathA = TEXT("/tmp/hbx_ut_journal_reinit_a.dat");
    const TCHAR* pathB = TEXT("/tmp/hbx_ut_journal_reinit_b.dat");
    DeleteFile(pathA);
    DeleteFile(pathB);

    Journal j;
    CHECK(j.Initialize(pathA));
    CHECK(j.QueueTransaction(TEXT("[1] ITEM_SCAN: SCAN:aaa"), NULL));
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    CHECK(j.Initialize(pathB));
    CHECK_EQ_INT(j.GetTransactionCount(), 0);

    CHECK(j.Initialize(pathA));
    CHECK_EQ_INT(j.GetTransactionCount(), 1);

    // Two Journals may hold the same file open at once.
    Journal other;
    CHECK(other.Initialize(pathA));
    CHECK_EQ_INT(other.GetTransactionCount(), 1);

    DeleteFile(pathA);
    DeleteFile(pathB);
}

TEST_CASE("Journal: ParseSequence and PayloadOf reject non-transaction lines")
{
    DWORD seq = 12345;
    CHECK_FALSE(Journal::ParseSequence(NULL, &seq));
    CHECK_FALSE(Journal::ParseSequence(TEXT(""), &seq));
    CHECK_FALSE(Journal::ParseSequence(TEXT("[2026-07-25 09:00:00] INFO: hello"), &seq));
    CHECK_FALSE(Journal::ParseSequence(TEXT("[2026-07-25 09:00:00] SYNCED 00000007"), &seq));
    CHECK_FALSE(Journal::ParseSequence(TEXT("[2026-07-25 09:00:00] TRANS notanum: x"), &seq));

    CHECK(Journal::PayloadOf(TEXT("[2026-07-25 09:00:00] INFO: hello")) == NULL);

    CHECK(Journal::ParseSequence(TEXT("[2026-07-25 09:00:00] TRANS 00000042: payload"), &seq));
    CHECK_EQ_INT((int)seq, 42);
    CHECK_EQ_STR(Journal::PayloadOf(TEXT("[2026-07-25 09:00:00] TRANS 00000042: payload")),
                 TEXT("payload"));
}
