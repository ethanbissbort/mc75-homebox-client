/*
 * test_api_endpoints.cpp  --  Integration tests for HBX::HbClient
 * ----------------------------------------------------------------
 * These tests exercise the parts of the HomeBox API client that do NOT
 * require a live socket:
 *   - authentication-state gating (every request must be refused until the
 *     client has authenticated, and null arguments must be rejected),
 *   - the base-URL configuration round-trip,
 *   - the JSON payloads the Item / Location models serialize and parse,
 *     which are exactly the bodies these endpoints send and receive.
 *
 * Host build: TCHAR == char, TEXT("x") == "x". All string literals handed to
 * the code under test are wrapped in TEXT(). C++03 only.
 */
#include "test_framework.hpp"

#include "HbClient.hpp"
#include "Models/Item.hpp"
#include "Models/Location.hpp"

#include <cstring>

using namespace HBX;

/* ------------------------------------------------------------------------ */
/* AUTH-STATE GATING                                                         */
/* Every request path must return false while the client is unauthenticated, */
/* and null arguments must be rejected outright.                             */
/* ------------------------------------------------------------------------ */
TEST_CASE("HbClient refuses all requests when not authenticated")
{
    HbClient c;
    c.SetBaseUrl(TEXT("http://localhost:8080/api"));

    Models::Item item;
    item.SetBarcode(TEXT("X"));
    Models::Location loc;

    // Precondition: fresh client is not authenticated.
    CHECK_FALSE(c.IsAuthenticated());

    // Item reads/writes are all gated on authentication.
    CHECK_FALSE(c.GetItem(TEXT("ABC"), &item));
    CHECK_FALSE(c.CreateItem(&item));
    CHECK_FALSE(c.UpdateItem(&item));   // also false: item has no id

    // Location reads are gated too.
    CHECK_FALSE(c.GetLocation(TEXT("1"), &loc));

    // GetAllLocations must both return false and zero out the out-params.
    int n = 99;
    Models::Location* arr = NULL;
    CHECK_FALSE(c.GetAllLocations(&arr, &n));
    CHECK_EQ_INT(n, 0);
    CHECK(arr == NULL);

    // Sync is gated on authentication.
    CHECK_FALSE(c.SyncPendingTransactions());

    // Null-argument rejection (independent of auth state).
    CHECK_FALSE(c.Authenticate(NULL, TEXT("k")));
    CHECK_FALSE(c.GetItem(NULL, &item));
}

/* ------------------------------------------------------------------------ */
/* BASE URL ROUND-TRIP                                                       */
/* ------------------------------------------------------------------------ */
TEST_CASE("HbClient base URL round-trips through Set/GetBaseUrl")
{
    HbClient c;
    c.SetBaseUrl(TEXT("http://localhost:8080/api"));
    CHECK_EQ_STR(c.GetBaseUrl(), TEXT("http://localhost:8080/api"));

    // Overwriting replaces the previous value.
    c.SetBaseUrl(TEXT("https://hb.example.com/v2"));
    CHECK_EQ_STR(c.GetBaseUrl(), TEXT("https://hb.example.com/v2"));
}

/* ------------------------------------------------------------------------ */
/* ITEM PAYLOAD                                                              */
/* The JSON the item endpoints send/receive.                                 */
/* ------------------------------------------------------------------------ */
TEST_CASE("Item ToJson emits the documented key order and round-trips")
{
    Models::Item item;
    item.SetId(TEXT("42"));
    item.SetBarcode(TEXT("BC1"));
    item.SetName(TEXT("Widget"));
    item.SetQuantity(7);

    // A barcode makes the item valid.
    CHECK(item.IsValid());

    TCHAR* json = item.ToJson();
    CHECK(json != NULL);

    // Exact format: only the set keys appear, in id,barcode,name order,
    // and "quantity" is always emitted last as a bare number.
    CHECK_EQ_STR(json, TEXT("{\"id\":\"42\",\"barcode\":\"BC1\",\"name\":\"Widget\",\"quantity\":7}"));

    // Round-trip the payload back into a second item.
    Models::Item parsed;
    CHECK(parsed.FromJson(json));
    CHECK_EQ_STR(parsed.GetId(), TEXT("42"));
    CHECK_EQ_STR(parsed.GetBarcode(), TEXT("BC1"));
    CHECK_EQ_STR(parsed.GetName(), TEXT("Widget"));
    CHECK_EQ_INT(parsed.GetQuantity(), 7);

    delete[] json;
}

TEST_CASE("Item validity requires a non-empty barcode")
{
    Models::Item empty;
    CHECK_FALSE(empty.IsValid());

    empty.SetBarcode(TEXT("HAS-BC"));
    CHECK(empty.IsValid());

    // FromJson returns false when the resulting item is invalid (no barcode).
    Models::Item noBarcode;
    CHECK_FALSE(noBarcode.FromJson(TEXT("{\"name\":\"NoCode\",\"quantity\":3}")));
}

/* ------------------------------------------------------------------------ */
/* LOCATION PAYLOAD                                                          */
/* ------------------------------------------------------------------------ */
TEST_CASE("Location ToJson round-trips id/name/path")
{
    Models::Location loc;
    loc.SetId(TEXT("L1"));
    loc.SetName(TEXT("Garage"));
    loc.SetPath(TEXT("/Home/Garage"));

    CHECK(loc.IsValid());

    TCHAR* json = loc.ToJson();
    CHECK(json != NULL);

    Models::Location parsed;
    CHECK(parsed.FromJson(json));
    CHECK_EQ_STR(parsed.GetId(), TEXT("L1"));
    CHECK_EQ_STR(parsed.GetName(), TEXT("Garage"));
    CHECK_EQ_STR(parsed.GetPath(), TEXT("/Home/Garage"));

    delete[] json;
}

TEST_CASE("Location validity requires a non-empty id")
{
    // A location carrying only a name is not valid.
    Models::Location nameOnly;
    nameOnly.SetName(TEXT("Garage"));
    CHECK_FALSE(nameOnly.IsValid());

    // Its serialized form parses structurally but FromJson reports invalid.
    TCHAR* json = nameOnly.ToJson();
    CHECK(json != NULL);

    Models::Location parsed;
    CHECK_FALSE(parsed.FromJson(json));   // no id -> invalid -> false

    delete[] json;
}
