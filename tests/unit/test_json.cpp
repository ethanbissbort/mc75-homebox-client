/*
 * test_json.cpp  --  Unit tests for HBX::Models::JsonLite
 * -------------------------------------------------------
 * Exercises the lightweight JSON parser/builder used by the MC75 HomeBox
 * client. Compiled on the host through the Win32 shim (TCHAR == char,
 * TEXT("x") == "x"), so all literals handed to the code under test are wrapped
 * in TEXT( ) to stay faithful to the Windows Mobile build.
 *
 * Memory contract: TCHAR* returned by ToString() is heap allocated and must be
 * released with delete[]. JsonLite nodes handed back by GetArrayElement() are
 * BORROWED (the parent parser still owns them), so the borrowing element must
 * not outlive its parent and must not be freed independently.
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
