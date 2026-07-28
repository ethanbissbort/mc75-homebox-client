/*
 * test_hb_backend.cpp  --  HBX::HbClient as an InventoryBackend
 * -------------------------------------------------------------
 * Covers the interface HbClient exposes to the controller and the sync engine
 * once it is one of two possible inventory systems:
 *   - backend identity (kind / display name / operator-assigned instance id),
 *   - the capability answers the UI branches on (status support, renewable
 *     session, match count),
 *   - the Item -> AssetSummary projection the detail screen renders,
 *   - queue replay outcomes, including the distinction between "not ours"
 *     (skipped) and "could not be sent" (retry, and therefore still queued).
 *
 * Nothing here opens a socket: every path exercised either fails before the
 * transport (no credentials, no base URL, not authenticated) or is pure
 * projection logic.
 *
 * Host build: TCHAR == char, TEXT("x") == "x". C++03 only.
 */
#include "test_framework.hpp"

#include "HbClient.hpp"
#include "InventoryBackend.hpp"
#include "Models/AssetSummary.hpp"
#include "Models/Item.hpp"
#include "StrUtil.hpp"

using namespace HBX;

/* ------------------------------------------------------------------------ */
/* IDENTITY                                                                  */
/* The instance id is what queue records are tagged with, so its default      */
/* matters: an untagged record written by an older build must resolve here.   */
/* ------------------------------------------------------------------------ */
TEST_CASE("HbClient reports a stable kind and display name")
{
    HbClient c;
    CHECK_EQ_STR(c.GetKind(), TEXT("hb"));
    CHECK_EQ_STR(c.GetDisplayName(), TEXT("HomeBox"));
}

TEST_CASE("HbClient instance id defaults to hb and round-trips")
{
    HbClient c;
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("hb"));

    c.SetInstanceId(TEXT("hb-prod"));
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("hb-prod"));

    // Clearing it restores the default rather than leaving an empty tag, which
    // would make every record this client queues unroutable.
    c.SetInstanceId(TEXT(""));
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("hb"));

    c.SetInstanceId(TEXT("hb-warehouse"));
    c.SetInstanceId(NULL);
    CHECK_EQ_STR(c.GetInstanceId(), TEXT("hb"));
}

TEST_CASE("HbClient truncates an over-long instance id consistently")
{
    HbClient c;

    TCHAR longId[80];
    for (int i = 0; i < 79; i++) {
        longId[i] = (TCHAR)'h';
    }
    longId[79] = 0;

    c.SetInstanceId(longId);

    // Truncated to the cap, never overflowed, and still non-empty: the same
    // value is used to tag a record and to route it back, so both sides agree.
    CHECK_EQ_INT(Str::Length(c.GetInstanceId()), HbClient::INSTANCE_ID_MAX - 1);
}

/* ------------------------------------------------------------------------ */
/* CAPABILITIES                                                              */
/* ------------------------------------------------------------------------ */
TEST_CASE("HbClient reports no status concept and a renewable session")
{
    HbClient c;

    // HomeBox items have no status field; the UI uses this to hide the action.
    CHECK_FALSE(c.SupportsStatus());
    CHECK_EQ_INT(c.GetStatusChoiceCount(), 0);
    CHECK(c.GetStatusChoice(0) == NULL);
    CHECK(c.GetStatusChoice(-1) == NULL);

    // Credentials are exchanged for a token, so a 401 is worth one retry.
    CHECK(c.SessionIsRenewable());
}

TEST_CASE("HbClient has no matches until a lookup succeeds")
{
    HbClient c;
    c.SetBaseUrl(TEXT("http://localhost:8080/api"));

    Models::AssetSummary summary;
    summary.SetTitle(TEXT("untouched"));

    CHECK_EQ_INT(c.GetMatchCount(), 0);
    CHECK_FALSE(c.GetMatch(0, &summary));
    CHECK_FALSE(c.GetMatch(-1, &summary));

    // A failed lookup (unauthenticated) must not leave a match behind, and must
    // not overwrite the caller's summary.
    CHECK_FALSE(c.LookupByCode(TEXT("ABC"), &summary));
    CHECK_EQ_INT(c.GetMatchCount(), 0);
    CHECK_EQ_STR(summary.GetTitle(), TEXT("untouched"));

    // Null arguments are rejected outright.
    CHECK_FALSE(c.LookupByCode(NULL, &summary));
    CHECK_FALSE(c.LookupByCode(TEXT("ABC"), NULL));
}

/* ------------------------------------------------------------------------ */
/* POLYMORPHIC USE                                                           */
/* The controller and sync engine only ever hold an InventoryBackend*.        */
/* ------------------------------------------------------------------------ */
TEST_CASE("HbClient is fully usable through an InventoryBackend pointer")
{
    InventoryBackend* b = new HbClient();

    b->SetBaseUrl(TEXT("http://localhost:8080/api"));
    CHECK_EQ_STR(b->GetBaseUrl(), TEXT("http://localhost:8080/api"));

    b->SetRequestTimeout(5000);

    CHECK_EQ_STR(b->GetKind(), TEXT("hb"));
    CHECK_EQ_STR(b->GetDisplayName(), TEXT("HomeBox"));
    CHECK_EQ_STR(b->GetInstanceId(), TEXT("hb"));
    CHECK_FALSE(b->IsAuthenticated());
    CHECK_EQ_INT(b->GetLastStatusCode(), 0);
    CHECK(b->SessionIsRenewable());
    CHECK_FALSE(b->SupportsStatus());

    // No credentials have been recorded, so there is nothing to exchange.
    CHECK_FALSE(b->Authenticate());

    // Virtual destructor: deleting through the base must run ~HbClient.
    delete b;
}

/* ------------------------------------------------------------------------ */
/* CREDENTIALS                                                               */
/* ------------------------------------------------------------------------ */
TEST_CASE("HbClient authentication needs recorded credentials")
{
    HbClient c;

    // Nothing recorded yet.
    CHECK_FALSE(c.Authenticate());

    // Null halves are rejected and leave nothing recorded.
    CHECK_FALSE(c.SetCredentials(NULL, TEXT("key")));
    CHECK_FALSE(c.SetCredentials(TEXT("MC75-1"), NULL));
    CHECK_FALSE(c.Authenticate());

    // Recorded, but with no base URL the request cannot be built, so the client
    // stays unauthenticated rather than believing in a session it never got.
    CHECK(c.SetCredentials(TEXT("MC75-1"), TEXT("sk_test")));
    CHECK_FALSE(c.Authenticate());
    CHECK_FALSE(c.IsAuthenticated());

    // The two-argument form still rejects nulls, unchanged.
    CHECK_FALSE(c.Authenticate(NULL, TEXT("key")));
    CHECK_FALSE(c.Authenticate(TEXT("MC75-1"), NULL));
}

/* ------------------------------------------------------------------------ */
/* ITEM -> ASSETSUMMARY PROJECTION                                           */
/* ------------------------------------------------------------------------ */
TEST_CASE("SummarizeItem projects a HomeBox item into the neutral summary")
{
    Models::Item item;
    item.SetId(TEXT("42"));
    item.SetBarcode(TEXT("BC1"));
    item.SetName(TEXT("Widget"));
    item.SetDescription(TEXT("Blue widget, 10mm"));
    item.SetLocationId(TEXT("L7"));
    item.SetQuantity(7);
    item.SetCategory(TEXT("Fasteners"));

    Models::AssetSummary s;
    HbClient::SummarizeItem(&item, &s);

    CHECK_EQ_STR(s.GetId(), TEXT("42"));
    CHECK_EQ_STR(s.GetTitle(), TEXT("Widget"));
    CHECK_EQ_STR(s.GetSubtitle(), TEXT("Fasteners"));
    CHECK_EQ_STR(s.GetCode(), TEXT("BC1"));
    CHECK_EQ_STR(s.GetSource(), TEXT("HomeBox"));

    // No status concept in HomeBox, so the field stays empty.
    CHECK_EQ_STR(s.GetStatus(), TEXT(""));

    // Four rows, in the order the operator reads them.
    CHECK_EQ_INT(s.GetFieldCount(), 4);
    CHECK_EQ_STR(s.GetLabel(0), TEXT("Barcode"));
    CHECK_EQ_STR(s.GetValue(0), TEXT("BC1"));
    CHECK_EQ_STR(s.GetLabel(1), TEXT("Quantity"));
    CHECK_EQ_STR(s.GetValue(1), TEXT("7"));
    CHECK_EQ_STR(s.GetLabel(2), TEXT("Location"));
    CHECK_EQ_STR(s.GetValue(2), TEXT("L7"));
    CHECK_EQ_STR(s.GetLabel(3), TEXT("Description"));
    CHECK_EQ_STR(s.GetValue(3), TEXT("Blue widget, 10mm"));

    CHECK_EQ_STR(s.FindValue(TEXT("Quantity")), TEXT("7"));
}

TEST_CASE("SummarizeItem keeps every row when the item is sparse")
{
    // Only a barcode: this is the minimum HomeBox considers a valid item.
    Models::Item item;
    item.SetBarcode(TEXT("BC-ONLY"));

    Models::AssetSummary s;
    HbClient::SummarizeItem(&item, &s);

    CHECK_EQ_STR(s.GetCode(), TEXT("BC-ONLY"));
    CHECK_EQ_STR(s.GetTitle(), TEXT(""));
    CHECK_EQ_STR(s.GetSubtitle(), TEXT(""));

    // An empty Location is a finding ("not placed anywhere"), not a reason to
    // drop the row, so the row count is fixed regardless of what is populated.
    CHECK_EQ_INT(s.GetFieldCount(), 4);
    CHECK_EQ_STR(s.FindValue(TEXT("Location")), TEXT(""));
    CHECK_EQ_STR(s.FindValue(TEXT("Description")), TEXT(""));

    // Quantity defaults to zero and is still rendered as a number.
    CHECK_EQ_STR(s.FindValue(TEXT("Quantity")), TEXT("0"));
}

TEST_CASE("SummarizeItem truncates over-long values instead of overflowing")
{
    TCHAR longText[300];
    for (int i = 0; i < 299; i++) {
        longText[i] = (TCHAR)'d';
    }
    longText[299] = 0;

    Models::Item item;
    item.SetBarcode(TEXT("BC1"));
    item.SetName(longText);
    item.SetDescription(longText);

    Models::AssetSummary s;
    HbClient::SummarizeItem(&item, &s);

    // Display-only truncation: the authoritative value stays in the Item.
    CHECK_EQ_INT(Str::Length(s.GetTitle()), Models::AssetSummary::VALUE_MAX - 1);
    CHECK_EQ_INT(Str::Length(s.FindValue(TEXT("Description"))),
                 Models::AssetSummary::VALUE_MAX - 1);
}

TEST_CASE("SummarizeItem tolerates null arguments")
{
    Models::AssetSummary s;
    s.AddField(TEXT("Stale"), TEXT("row"));

    // A null item clears the summary rather than leaving the previous asset on
    // screen next to the new scan's header.
    HbClient::SummarizeItem(NULL, &s);
    CHECK_EQ_INT(s.GetFieldCount(), 0);
    CHECK_EQ_STR(s.GetTitle(), TEXT(""));

    Models::Item item;
    HbClient::SummarizeItem(&item, NULL);   // must not crash
}

/* ------------------------------------------------------------------------ */
/* QUEUE REPLAY                                                              */
/* Replay receives the type with its backend prefix already stripped.        */
/* ------------------------------------------------------------------------ */
TEST_CASE("Replay skips transaction types this backend does not own")
{
    HbClient c;
    c.SetBaseUrl(TEXT("http://localhost:8080/api"));

    // A NetBox entry left in the queue is not a HomeBox failure: reporting it
    // as one would pin the sync status at "failed" for as long as it sits there.
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_MOVE"), TEXT("MOVE:1")), (int)REPLAY_SKIPPED);
    CHECK_EQ_INT((int)c.Replay(TEXT("DEVICE_STATUS"), TEXT("STATUS:1")), (int)REPLAY_SKIPPED);
    CHECK_EQ_INT((int)c.Replay(TEXT(""), TEXT("")), (int)REPLAY_SKIPPED);
    CHECK_EQ_INT((int)c.Replay(NULL, TEXT("SCAN:1")), (int)REPLAY_SKIPPED);

    // The type is matched exactly; a prefix that was not stripped is not ours.
    CHECK_EQ_INT((int)c.Replay(TEXT("hb.ITEM_SCAN"), TEXT("SCAN:1")), (int)REPLAY_SKIPPED);
}

TEST_CASE("Replay leaves an unsendable HomeBox scan queued")
{
    HbClient c;
    c.SetBaseUrl(TEXT("http://localhost:8080/api"));

    // Well-formed, but the client is not authenticated, so nothing reached the
    // server: retry, which keeps the entry in the queue.
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCAN:123456789")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCANLOC:3:ABCL7")), (int)REPLAY_RETRY);
}

TEST_CASE("Replay rejects malformed scan payloads without dropping them")
{
    HbClient c;
    c.SetBaseUrl(TEXT("http://localhost:8080/api"));

    // Unrecognised data prefix.
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("NONSENSE")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("")), (int)REPLAY_RETRY);

    // Empty barcode.
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCAN:")), (int)REPLAY_RETRY);

    // SCANLOC with no length terminator, a non-numeric length, a zero length,
    // and a length longer than the remaining payload.
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCANLOC:3")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCANLOC:x:ABCL7")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCANLOC:0:ABCL7")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), TEXT("SCANLOC:99:ABC")), (int)REPLAY_RETRY);

    // A NULL payload is treated as an empty one, not dereferenced.
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_SCAN"), NULL), (int)REPLAY_RETRY);
}

TEST_CASE("Replay rejects malformed item updates without dropping them")
{
    HbClient c;
    c.SetBaseUrl(TEXT("http://localhost:8080/api"));

    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_UPDATE"), TEXT("NONSENSE")), (int)REPLAY_RETRY);
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_UPDATE"), TEXT("UPDATE:")), (int)REPLAY_RETRY);

    // Parses as JSON but is not a valid item (no barcode).
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_UPDATE"), TEXT("UPDATE:{\"name\":\"NoCode\"}")),
                 (int)REPLAY_RETRY);

    // Valid item, but unauthenticated: still queued.
    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_UPDATE"),
                               TEXT("UPDATE:{\"id\":\"42\",\"barcode\":\"BC1\",\"name\":\"W\"}")),
                 (int)REPLAY_RETRY);

    CHECK_EQ_INT((int)c.Replay(TEXT("ITEM_UPDATE"), NULL), (int)REPLAY_RETRY);
}
