/*
 * test_json.cpp  --  Unit tests for HBX::Models::JsonLite
 * -------------------------------------------------------
 * Exercises the lightweight JSON parser/builder used by the MC75 HomeBox
 * client. Compiled on the host through the Win32 shim (TCHAR == char,
 * TEXT("x") == "x"), so all literals handed to the code under test are wrapped
 * in TEXT( ) to stay faithful to the Windows Mobile build.
 *
 * Memory contract: TCHAR* returned by ToString() is heap allocated and must be
 * released with delete[]. JsonLite nodes handed back by GetArrayElement(),
 * GetObject() and GetArray() are BORROWED (the parent parser still owns them),
 * so a borrowing view must not be read after its parent is cleared or
 * destroyed, and must not be freed independently.
 */
#include "test_framework.hpp"
#include "Models/JsonLite.hpp"

#include <cstring>

using HBX::Models::JsonLite;

/* Small absolute-difference helper (avoid pulling in <cmath>). */
static double dabs(double v) { return v < 0.0 ? -v : v; }

/* --------------------------------------------------------------- parsing */

TEST_CASE("JsonLite parses a flat object of mixed types")
{
    JsonLite j;
    CHECK(j.Parse(TEXT("{\"name\":\"Widget\",\"quantity\":42,\"price\":9.99,")
                  TEXT("\"active\":true,\"gone\":null}")));

    CHECK(j.IsObject());
    CHECK_FALSE(j.IsArray());

    TCHAR sbuf[64];
    CHECK(j.GetString(TEXT("name"), sbuf, 64));
    CHECK_EQ_STR(sbuf, TEXT("Widget"));

    int qty = 0;
    CHECK(j.GetInt(TEXT("quantity"), &qty));
    CHECK_EQ_INT(qty, 42);

    bool active = false;
    CHECK(j.GetBool(TEXT("active"), &active));
    CHECK(active);

    double price = 0.0;
    CHECK(j.GetDouble(TEXT("price"), &price));
    CHECK(dabs(price - 9.99) < 0.001);

    /* A null-typed member still exists as a key. */
    CHECK(j.HasKey(TEXT("gone")));
    CHECK(j.HasKey(TEXT("name")));
    CHECK_FALSE(j.HasKey(TEXT("missing")));
}

TEST_CASE("JsonLite returns false for missing keys and wrong types")
{
    JsonLite j;
    CHECK(j.Parse(TEXT("{\"name\":\"Widget\",\"quantity\":42,\"active\":true}")));

    TCHAR sbuf[64];
    int ival = 123;
    bool bval = false;

    /* Missing key -> every typed getter fails and leaves outputs untouched. */
    CHECK_FALSE(j.GetString(TEXT("nope"), sbuf, 64));
    CHECK_FALSE(j.GetInt(TEXT("nope"), &ival));
    CHECK_EQ_INT(ival, 123);
    CHECK_FALSE(j.GetBool(TEXT("nope"), &bval));

    /* Wrong type: quantity is a number, not a string. */
    CHECK_FALSE(j.GetString(TEXT("quantity"), sbuf, 64));

    /* Wrong type: name is a string, not a bool. */
    CHECK_FALSE(j.GetBool(TEXT("name"), &bval));

    /* GetInt on a string-typed value also fails. */
    CHECK_FALSE(j.GetInt(TEXT("name"), &ival));
    CHECK_EQ_INT(ival, 123);
}

TEST_CASE("JsonLite parses a negative integer")
{
    JsonLite j;
    CHECK(j.Parse(TEXT("{\"n\":-17}")));

    int n = 0;
    CHECK(j.GetInt(TEXT("n"), &n));
    CHECK_EQ_INT(n, -17);
}

TEST_CASE("JsonLite parses arrays and borrows elements")
{
    JsonLite arr;
    CHECK(arr.Parse(TEXT("[{\"id\":\"1\"},{\"id\":\"2\"},{\"id\":\"3\"}]")));

    CHECK(arr.IsArray());
    CHECK_FALSE(arr.IsObject());
    CHECK_EQ_INT(arr.GetArrayLength(), 3);

    TCHAR sbuf[32];

    /* Element 0 -> id == "1" */
    JsonLite e;
    CHECK(arr.GetArrayElement(0, &e));
    CHECK(e.GetString(TEXT("id"), sbuf, 32));
    CHECK_EQ_STR(sbuf, TEXT("1"));

    /* Element 1 -> id == "2" (re-uses e; borrowed node is swapped safely) */
    CHECK(arr.GetArrayElement(1, &e));
    CHECK(e.GetString(TEXT("id"), sbuf, 32));
    CHECK_EQ_STR(sbuf, TEXT("2"));

    /* Out-of-range index -> false */
    CHECK_FALSE(arr.GetArrayElement(9, &e));

    /* e (borrowing) is destroyed before arr, matching the ownership contract. */
}

TEST_CASE("JsonLite decodes backslash escape sequences")
{
    JsonLite j;
    /* JSON text is {"s":"a\tb\nc"} with literal backslash-t / backslash-n. */
    CHECK(j.Parse(TEXT("{\"s\":\"a\\tb\\nc\"}")));

    TCHAR out[64];
    CHECK(j.GetString(TEXT("s"), out, 64));

    /* out == 'a', TAB, 'b', NEWLINE, 'c' */
    CHECK(out[0] == 'a');
    CHECK(out[1] == '\t');
    CHECK_EQ_INT((int)out[1], 9);
    CHECK(out[2] == 'b');
    CHECK(out[3] == '\n');
    CHECK_EQ_INT((int)out[3], 10);
    CHECK(out[4] == 'c');
}

/* -------------------------------------------------------------- building */

TEST_CASE("JsonLite builds an object with exact serialization")
{
    JsonLite b;
    b.BeginObject();
    b.AddString(TEXT("name"), TEXT("Widget"));
    b.AddInt(TEXT("quantity"), 5);
    b.EndObject();

    TCHAR* s = b.ToString();
    CHECK(s != NULL);
    CHECK_EQ_STR(s, TEXT("{\"name\":\"Widget\",\"quantity\":5}"));
    delete[] s;
}

TEST_CASE("JsonLite renders a boolean value as true")
{
    JsonLite b;
    b.BeginObject();
    b.AddBool(TEXT("active"), true);
    b.EndObject();

    TCHAR* s = b.ToString();
    CHECK(s != NULL);
    CHECK(std::strstr(s, TEXT("true")) != NULL);
    delete[] s;
}

TEST_CASE("JsonLite renders a double with six fractional digits")
{
    JsonLite b;
    b.BeginObject();
    b.AddDouble(TEXT("x"), 2.5);
    b.EndObject();

    TCHAR* s = b.ToString();
    CHECK(s != NULL);
    CHECK(std::strstr(s, TEXT("2.500000")) != NULL);
    delete[] s;
}

TEST_CASE("JsonLite round-trips a built object back through Parse")
{
    JsonLite b;
    b.BeginObject();
    b.AddString(TEXT("name"), TEXT("Widget"));
    b.AddInt(TEXT("quantity"), 7);
    b.EndObject();

    TCHAR* s = b.ToString();
    CHECK(s != NULL);

    JsonLite p;
    CHECK(p.Parse(s));
    delete[] s;

    CHECK(p.IsObject());

    TCHAR sbuf[64];
    CHECK(p.GetString(TEXT("name"), sbuf, 64));
    CHECK_EQ_STR(sbuf, TEXT("Widget"));

    int qty = 0;
    CHECK(p.GetInt(TEXT("quantity"), &qty));
    CHECK_EQ_INT(qty, 7);
}

/* ---------------------------------------------------------- edge cases */

TEST_CASE("JsonLite rejects a NULL input and reports no type before parse")
{
    JsonLite fresh;
    /* Nothing parsed yet: it is neither object nor array. */
    CHECK_FALSE(fresh.IsObject());
    CHECK_FALSE(fresh.IsArray());

    JsonLite j;
    CHECK_FALSE(j.Parse(NULL));
}

/* ------------------------------------------------- nested NetBox responses */

/*
 * A trimmed but structurally faithful NetBox 4.x device list. Everything the
 * on-device reader has to cope with is here:
 *   - the {"count","next","previous","results":[...]} envelope every list
 *     endpoint returns, even for a single hit
 *   - foreign keys as nested objects (site, location, rack, device_type, role)
 *     and device_type->manufacturer three levels deep
 *   - "status"/"face" as {"value","label"} pairs on read
 *   - "rack": null / "position": null for an unracked device
 *   - a fractional half-U rack position on the racked one
 *   - a raw UTF-8 site name (NetBox leaves ensure_ascii off) alongside a
 *     \uXXXX escape in a description, which must decode to the same encoding
 */
static const TCHAR* kDeviceListJson =
    TEXT("{")
      TEXT("\"count\":2,")
      TEXT("\"next\":null,")
      TEXT("\"previous\":null,")
      TEXT("\"results\":[")
        TEXT("{")
          TEXT("\"id\":231,")
          TEXT("\"url\":\"https://netbox.example.com/api/dcim/devices/231/\",")
          TEXT("\"display\":\"sw-akr-01\",")
          TEXT("\"name\":\"sw-akr-01\",")
          TEXT("\"device_type\":{")
            TEXT("\"id\":12,")
            TEXT("\"display\":\"Catalyst 9300-48P\",")
            TEXT("\"manufacturer\":{\"id\":3,\"name\":\"Cisco\",\"slug\":\"cisco\"},")
            TEXT("\"model\":\"Catalyst 9300-48P\",")
            TEXT("\"slug\":\"cat9300-48p\"")
          TEXT("},")
          TEXT("\"role\":{\"id\":5,\"name\":\"Access Switch\",\"slug\":\"access-switch\"},")
          TEXT("\"tenant\":null,")
          TEXT("\"serial\":\"FDO2447L0XY\",")
          TEXT("\"asset_tag\":\"ACME-004821\",")
          TEXT("\"site\":{\"id\":2,\"name\":\"Zürich-Nord\",\"slug\":\"zurich-nord\"},")
          TEXT("\"location\":{\"id\":9,\"name\":\"Comms Room 1\",\"slug\":\"comms-room-1\",\"_depth\":1},")
          TEXT("\"rack\":null,")
          TEXT("\"position\":null,")
          TEXT("\"face\":{\"value\":\"\",\"label\":\"\"},")
          TEXT("\"status\":{\"value\":\"inventory\",\"label\":\"Inventory\"},")
          TEXT("\"primary_ip\":null,")
          TEXT("\"description\":\"caf\\u00e9 spare\",")
          TEXT("\"tags\":[],")
          TEXT("\"custom_fields\":{},")
          TEXT("\"last_updated\":\"2026-07-12T09:41:03.221417Z\"")
        TEXT("},")
        TEXT("{")
          TEXT("\"id\":904,")
          TEXT("\"name\":\"sw-akr-02\",")
          TEXT("\"device_type\":{\"id\":12,\"manufacturer\":{\"id\":3,\"name\":\"Cisco\"},")
            TEXT("\"model\":\"Catalyst 9300-48P\"},")
          TEXT("\"serial\":\"FDO2447L0ZZ\",")
          TEXT("\"asset_tag\":\"ACME-004822\",")
          TEXT("\"site\":{\"id\":2,\"name\":\"Zürich-Nord\",\"slug\":\"zurich-nord\"},")
          TEXT("\"location\":{\"id\":9,\"name\":\"Comms Room 1\",\"slug\":\"comms-room-1\"},")
          TEXT("\"rack\":{\"id\":14,\"name\":\"R-201\",\"device_count\":18},")
          TEXT("\"position\":42.5,")
          TEXT("\"face\":{\"value\":\"front\",\"label\":\"Front\"},")
          TEXT("\"status\":{\"value\":\"active\",\"label\":\"Active\"}")
        TEXT("}")
      TEXT("]")
    TEXT("}");

/* A miss is 200 OK with an empty array, never a 404. */
static const TCHAR* kEmptyListJson =
    TEXT("{\"count\":0,\"next\":null,\"previous\":null,\"results\":[]}");

TEST_CASE("JsonLite descends into a NetBox list envelope")
{
    JsonLite env;
    CHECK(env.Parse(kDeviceListJson));
    CHECK(env.IsObject());

    int count = 0;
    CHECK(env.GetInt(TEXT("count"), &count));
    CHECK_EQ_INT(count, 2);

    /* "next":null is a present key with no string value: the client has to read
       "no further page" from the failed get, not from a missing key. */
    TCHAR buf[128];
    CHECK(env.HasKey(TEXT("next")));
    CHECK_FALSE(env.GetString(TEXT("next"), buf, 128));

    JsonLite results;
    CHECK(env.GetArray(TEXT("results"), &results));
    CHECK(results.IsArray());
    CHECK_EQ_INT(results.GetArrayLength(), 2);

    /* Type mismatches are rejected rather than silently borrowed. */
    JsonLite wrong;
    CHECK_FALSE(env.GetArray(TEXT("count"), &wrong));
    CHECK_FALSE(env.GetObject(TEXT("results"), &wrong));
    CHECK_FALSE(env.GetObject(TEXT("missing"), &wrong));
    CHECK_FALSE(env.GetArray(TEXT("missing"), &wrong));

    /* An element of a borrowed array is itself borrowable. */
    JsonLite dev;
    CHECK(results.GetArrayElement(0, &dev));

    int id = 0;
    CHECK(dev.GetInt(TEXT("id"), &id));
    CHECK_EQ_INT(id, 231);

    CHECK(dev.GetString(TEXT("asset_tag"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("ACME-004821"));

    /* One level down: site->name and the choice-field pair. */
    CHECK(dev.GetNestedString(TEXT("site"), TEXT("name"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("Zürich-Nord"));
    CHECK(dev.GetNestedString(TEXT("status"), TEXT("value"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("inventory"));
    CHECK(dev.GetNestedString(TEXT("status"), TEXT("label"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("Inventory"));
    CHECK(dev.GetNestedString(TEXT("location"), TEXT("name"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("Comms Room 1"));

    /* The ids a PATCH needs are only ever returned nested. */
    CHECK(dev.GetNestedInt(TEXT("site"), TEXT("id"), &id));
    CHECK_EQ_INT(id, 2);
    CHECK(dev.GetNestedInt(TEXT("location"), TEXT("id"), &id));
    CHECK_EQ_INT(id, 9);

    /* Two and three levels down, through a borrowed view. */
    JsonLite dtype;
    CHECK(dev.GetObject(TEXT("device_type"), &dtype));
    CHECK(dtype.GetString(TEXT("model"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("Catalyst 9300-48P"));

    JsonLite mfr;
    CHECK(dtype.GetObject(TEXT("manufacturer"), &mfr));
    CHECK(mfr.GetString(TEXT("name"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("Cisco"));
    CHECK(dtype.GetNestedInt(TEXT("manufacturer"), TEXT("id"), &id));
    CHECK_EQ_INT(id, 3);

    /* A \uXXXX escape decodes to the same bytes as the raw UTF-8 above it. */
    CHECK(dev.GetString(TEXT("description"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("café spare"));

    /* Descending into oneself is refused instead of freeing the tree first. */
    CHECK_FALSE(dev.GetObject(TEXT("site"), &dev));
    CHECK(dev.GetString(TEXT("asset_tag"), buf, 128));
    CHECK_EQ_STR(buf, TEXT("ACME-004821"));
}

TEST_CASE("JsonLite treats a null sub-object as absent, not empty")
{
    JsonLite env;
    CHECK(env.Parse(kDeviceListJson));

    JsonLite results;
    CHECK(env.GetArray(TEXT("results"), &results));

    JsonLite dev;
    CHECK(results.GetArrayElement(0, &dev));

    /* An unracked device carries "rack": null -- the key is there, the object
       is not. Reading it must not leave a previous device's rack on screen. */
    CHECK(dev.HasKey(TEXT("rack")));

    JsonLite rack;
    CHECK_FALSE(dev.GetObject(TEXT("rack"), &rack));

    TCHAR buf[64];
    buf[0] = (TCHAR)'!';
    buf[1] = 0;
    CHECK_FALSE(dev.GetNestedString(TEXT("rack"), TEXT("name"), buf, 64));
    CHECK_EQ_STR(buf, TEXT("!"));

    CHECK(dev.GetNestedStringAlloc(TEXT("rack"), TEXT("name")) == NULL);

    int id = -1;
    CHECK_FALSE(dev.GetNestedInt(TEXT("rack"), TEXT("id"), &id));
    CHECK_EQ_INT(id, -1);

    /* A present sub-object with a missing member fails the same way. */
    CHECK_FALSE(dev.GetNestedString(TEXT("site"), TEXT("nope"), buf, 64));
    CHECK_EQ_STR(buf, TEXT("!"));

    /* Truncation is a failure here too, as it is for GetString. */
    TCHAR small[4];
    CHECK_FALSE(dev.GetNestedString(TEXT("device_type"), TEXT("model"), small, 4));

    TCHAR* model = dev.GetNestedStringAlloc(TEXT("device_type"), TEXT("model"));
    CHECK(model != NULL);
    CHECK_EQ_STR(model, TEXT("Catalyst 9300-48P"));
    delete[] model;
}

TEST_CASE("JsonLite reads the racked device's decimal position")
{
    JsonLite env;
    CHECK(env.Parse(kDeviceListJson));

    JsonLite results;
    CHECK(env.GetArray(TEXT("results"), &results));

    JsonLite dev;
    CHECK(results.GetArrayElement(1, &dev));

    TCHAR buf[64];
    CHECK(dev.GetNestedString(TEXT("rack"), TEXT("name"), buf, 64));
    CHECK_EQ_STR(buf, TEXT("R-201"));

    int rackId = 0;
    CHECK(dev.GetNestedInt(TEXT("rack"), TEXT("id"), &rackId));
    CHECK_EQ_INT(rackId, 14);

    /* Half-U mounting: the position must not round to a whole rack unit. */
    double position = 0.0;
    CHECK(dev.GetDouble(TEXT("position"), &position));
    CHECK(dabs(position - 42.5) < 0.0001);

    CHECK(dev.GetNestedString(TEXT("face"), TEXT("value"), buf, 64));
    CHECK_EQ_STR(buf, TEXT("front"));
    CHECK(dev.GetNestedString(TEXT("status"), TEXT("value"), buf, 64));
    CHECK_EQ_STR(buf, TEXT("active"));

    /* The first device is unracked, so the same reads all fail there. */
    JsonLite unracked;
    CHECK(results.GetArrayElement(0, &unracked));
    CHECK_FALSE(unracked.GetDouble(TEXT("position"), &position));
    CHECK(dabs(position - 42.5) < 0.0001); /* output left untouched */
    CHECK(unracked.GetNestedString(TEXT("face"), TEXT("value"), buf, 64));
    CHECK_EQ_STR(buf, TEXT("")); /* cleared face is "", not null */
}

TEST_CASE("JsonLite keeps NetBox ids and positions numerically intact")
{
    JsonLite j;
    CHECK(j.Parse(TEXT("{\"id\":2147483647,\"other_id\":987654321,")
                  TEXT("\"half\":42.5,\"whole\":42.0,\"flush\":1,\"cleared\":null}")));

    /* Ids are read through a double internally, which is exact far past the
       32-bit range NetBox primary keys live in -- nothing rounds. */
    int id = 0;
    CHECK(j.GetInt(TEXT("id"), &id));
    CHECK_EQ_INT(id, 2147483647);
    CHECK(j.GetInt(TEXT("other_id"), &id));
    CHECK_EQ_INT(id, 987654321);

    /* position arrives as a JSON number in both forms: NetBox writes the
       trailing .0 for a whole unit and .5 for a half-U slot. */
    double pos = 0.0;
    CHECK(j.GetDouble(TEXT("half"), &pos));
    CHECK(dabs(pos - 42.5) < 0.0001);
    CHECK(j.GetDouble(TEXT("whole"), &pos));
    CHECK(dabs(pos - 42.0) < 0.0001);
    CHECK(j.GetDouble(TEXT("flush"), &pos));
    CHECK(dabs(pos - 1.0) < 0.0001);

    /* Reading a decimal as an int truncates rather than failing, so an id-like
       field is never silently dropped -- but positions must use GetDouble. */
    int truncated = 0;
    CHECK(j.GetInt(TEXT("half"), &truncated));
    CHECK_EQ_INT(truncated, 42);

    /* A cleared position is null: neither an int nor a double. */
    CHECK(j.HasKey(TEXT("cleared")));
    CHECK_FALSE(j.GetDouble(TEXT("cleared"), &pos));
    CHECK_FALSE(j.GetInt(TEXT("cleared"), &id));
    CHECK_EQ_INT(id, 987654321);
}

TEST_CASE("JsonLite reads an empty result set and a paginated one")
{
    JsonLite env;
    CHECK(env.Parse(kEmptyListJson));

    int count = -1;
    CHECK(env.GetInt(TEXT("count"), &count));
    CHECK_EQ_INT(count, 0);

    JsonLite results;
    CHECK(env.GetArray(TEXT("results"), &results));
    CHECK(results.IsArray());
    CHECK_EQ_INT(results.GetArrayLength(), 0);

    JsonLite none;
    CHECK_FALSE(results.GetArrayElement(0, &none));

    /* The many-results envelope: count is large and next is a URL, not null. */
    JsonLite page;
    CHECK(page.Parse(
        TEXT("{\"count\":2861,")
        TEXT("\"next\":\"http://netbox.example.com/api/dcim/devices/?limit=50&offset=50\",")
        TEXT("\"previous\":null,")
        TEXT("\"results\":[{\"id\":1,\"name\":\"sw-akr-01\"}]}")));

    CHECK(page.GetInt(TEXT("count"), &count));
    CHECK_EQ_INT(count, 2861);

    TCHAR next[160];
    CHECK(page.GetString(TEXT("next"), next, 160));
    CHECK_EQ_STR(next,
        TEXT("http://netbox.example.com/api/dcim/devices/?limit=50&offset=50"));
    CHECK_FALSE(page.GetString(TEXT("previous"), next, 160));
}

TEST_CASE("JsonLite refuses to borrow into the parser that owns the tree")
{
    JsonLite doc;
    CHECK(doc.Parse(kDeviceListJson));

    JsonLite results;
    CHECK(doc.GetArray(TEXT("results"), &results));

    JsonLite dev;
    CHECK(results.GetArrayElement(0, &dev));

    /* Ping-ponging a path walk back into the owning document would free the
       tree the node being handed over lives in. Refused, and doc still works. */
    CHECK_FALSE(dev.GetObject(TEXT("site"), &doc));

    int count = 0;
    CHECK(doc.GetInt(TEXT("count"), &count));
    CHECK_EQ_INT(count, 2);

    /* An unrelated document is a legitimate destination: its own tree is
       released and it starts borrowing instead. */
    JsonLite other;
    CHECK(other.Parse(TEXT("{\"n\":1}")));
    CHECK(dev.GetObject(TEXT("site"), &other));

    TCHAR buf[64];
    CHECK(other.GetString(TEXT("name"), buf, 64));
    CHECK_EQ_STR(buf, TEXT("Zürich-Nord"));
}

TEST_CASE("JsonLite borrowed views free nothing, whatever the order")
{
    JsonLite orphan;

    {
        JsonLite env;
        CHECK(env.Parse(kDeviceListJson));

        JsonLite results;
        CHECK(env.GetArray(TEXT("results"), &results));

        JsonLite dev;
        CHECK(results.GetArrayElement(0, &dev));
        CHECK(dev.GetObject(TEXT("site"), &orphan));

        TCHAR buf[64];
        CHECK(orphan.GetString(TEXT("slug"), buf, 64));
        CHECK_EQ_STR(buf, TEXT("zurich-nord"));

        /* dev, results and env are all destroyed here while `orphan` still
           points into env's tree. A borrowed view must free nothing, or this
           is a double free; the reverse order (view first) is covered by the
           array test above. */
    }

    /* Reading through `orphan` now would be a use-after-free, but re-parsing is
       safe: Parse() drops the borrow before it takes ownership of a new tree. */
    CHECK(orphan.Parse(TEXT("{\"n\":7}")));
    int n = 0;
    CHECK(orphan.GetInt(TEXT("n"), &n));
    CHECK_EQ_INT(n, 7);
}
