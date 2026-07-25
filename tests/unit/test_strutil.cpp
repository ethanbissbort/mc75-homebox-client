/*
 * test_strutil.cpp  --  Unit tests for HBX::Str
 * ---------------------------------------------
 * These cover the bounded-string, JSON-escaping and UTF-8 helpers that the
 * rest of the codebase now routes every caller- or server-supplied string
 * through. The originals were fixed stack buffers written with wsprintf, so
 * the properties asserted here (always NUL-terminated, never write past cap,
 * report truncation, escape correctly) are exactly what used to be missing.
 *
 * Host build: TCHAR == char, TEXT("x") == "x".
 */
#include "test_framework.hpp"
#include <windows.h>
#include "StrUtil.hpp"

using namespace HBX;

// Fills a buffer with a sentinel so an overrun past `cap` is detectable.
static void Poison(TCHAR* buf, int total)
{
    for (int i = 0; i < total; i++) {
        buf[i] = (TCHAR)'#';
    }
}

TEST_CASE("Str: Copy truncates safely and always terminates")
{
    TCHAR buf[16];

    Poison(buf, 16);
    CHECK(Str::Copy(buf, 8, TEXT("abc")));
    CHECK_EQ_STR(buf, TEXT("abc"));
    CHECK(buf[8] == (TCHAR)'#'); // nothing written past cap

    Poison(buf, 16);
    CHECK_FALSE(Str::Copy(buf, 4, TEXT("abcdefgh")));
    CHECK_EQ_STR(buf, TEXT("abc")); // 3 chars + NUL fits in cap 4
    CHECK(buf[4] == (TCHAR)'#');

    // Exact fit is not truncation.
    Poison(buf, 16);
    CHECK(Str::Copy(buf, 4, TEXT("abc")));
    CHECK_EQ_STR(buf, TEXT("abc"));

    // Degenerate inputs must not crash.
    CHECK_FALSE(Str::Copy(NULL, 8, TEXT("abc")));
    CHECK_FALSE(Str::Copy(buf, 0, TEXT("abc")));
    CHECK(Str::Copy(buf, 4, NULL));
    CHECK_EQ_STR(buf, TEXT(""));
}

TEST_CASE("Str: Append respects capacity and reports truncation")
{
    TCHAR buf[16];

    Poison(buf, 16);
    buf[0] = 0;
    CHECK(Str::Append(buf, 8, TEXT("abc")));
    CHECK(Str::Append(buf, 8, TEXT("de")));
    CHECK_EQ_STR(buf, TEXT("abcde"));
    CHECK(buf[8] == (TCHAR)'#');

    CHECK_FALSE(Str::Append(buf, 8, TEXT("XYZQRS")));
    CHECK_EQ_STR(buf, TEXT("abcdeXY"));
    CHECK(buf[8] == (TCHAR)'#');

    // Appending to a full buffer is a no-op, not an overrun.
    CHECK_FALSE(Str::Append(buf, 8, TEXT("more")));
    CHECK_EQ_STR(buf, TEXT("abcdeXY"));
}

TEST_CASE("Str: AppendInt handles zero, negatives and the extremes")
{
    TCHAR buf[64];

    buf[0] = 0; CHECK(Str::AppendInt(buf, 64, 0));           CHECK_EQ_STR(buf, TEXT("0"));
    buf[0] = 0; CHECK(Str::AppendInt(buf, 64, 42));          CHECK_EQ_STR(buf, TEXT("42"));
    buf[0] = 0; CHECK(Str::AppendInt(buf, 64, -42));         CHECK_EQ_STR(buf, TEXT("-42"));
    buf[0] = 0; CHECK(Str::AppendInt(buf, 64, 2147483647L)); CHECK_EQ_STR(buf, TEXT("2147483647"));

    // -2147483648 written without ever negating the minimum value.
    buf[0] = 0;
    CHECK(Str::AppendInt(buf, 64, -2147483647L - 1L));
    CHECK_EQ_STR(buf, TEXT("-2147483648"));

    buf[0] = 0;
    CHECK(Str::Append(buf, 64, TEXT("n=")));
    CHECK(Str::AppendInt(buf, 64, 7));
    CHECK_EQ_STR(buf, TEXT("n=7"));
}

TEST_CASE("Str: ParseInt saturates instead of wrapping")
{
    // Regression: ItemView parsed its quantity field with a hand-rolled loop
    // that wrapped to garbage on a long digit string.
    int v = -1;

    CHECK(Str::ParseInt(TEXT("0"), &v));      CHECK_EQ_INT(v, 0);
    CHECK(Str::ParseInt(TEXT("123"), &v));    CHECK_EQ_INT(v, 123);
    CHECK(Str::ParseInt(TEXT("-123"), &v));   CHECK_EQ_INT(v, -123);
    CHECK(Str::ParseInt(TEXT("+7"), &v));     CHECK_EQ_INT(v, 7);
    CHECK(Str::ParseInt(TEXT("  12  "), &v)); CHECK_EQ_INT(v, 12);

    CHECK(Str::ParseInt(TEXT("2147483647"), &v));
    CHECK_EQ_INT(v, 2147483647);

    // Overflow clamps and reports failure rather than wrapping.
    v = 0;
    CHECK_FALSE(Str::ParseInt(TEXT("99999999999999999999"), &v));
    CHECK_EQ_INT(v, 2147483647);

    v = 0;
    CHECK_FALSE(Str::ParseInt(TEXT("-99999999999999999999"), &v));
    CHECK(v == (-2147483647 - 1));

    // Malformed input is rejected.
    CHECK_FALSE(Str::ParseInt(TEXT(""), &v));
    CHECK_FALSE(Str::ParseInt(TEXT("abc"), &v));
    CHECK_FALSE(Str::ParseInt(TEXT("12x"), &v));
    CHECK_FALSE(Str::ParseInt(TEXT("-"), &v));
    CHECK_FALSE(Str::ParseInt(NULL, &v));
}

TEST_CASE("Str: JsonEscape escapes quotes, backslashes and control characters")
{
    // Regression: Item::ToJson emitted values raw, so an item name containing
    // a quote produced invalid JSON that the server rejected and the offline
    // queue could never replay.
    TCHAR buf[128];

    CHECK(Str::JsonEscape(buf, 128, TEXT("plain")));
    CHECK_EQ_STR(buf, TEXT("plain"));

    CHECK(Str::JsonEscape(buf, 128, TEXT("say \"hi\"")));
    CHECK_EQ_STR(buf, TEXT("say \\\"hi\\\""));

    CHECK(Str::JsonEscape(buf, 128, TEXT("C:\\temp")));
    CHECK_EQ_STR(buf, TEXT("C:\\\\temp"));

    CHECK(Str::JsonEscape(buf, 128, TEXT("a\nb\tc\rd")));
    CHECK_EQ_STR(buf, TEXT("a\\nb\\tc\\rd"));

    // Other control characters become \u00XX.
    TCHAR raw[4];
    raw[0] = (TCHAR)'a';
    raw[1] = (TCHAR)0x01;
    raw[2] = (TCHAR)'b';
    raw[3] = 0;
    CHECK(Str::JsonEscape(buf, 128, raw));
    CHECK_EQ_STR(buf, TEXT("a\\u0001b"));

    // Truncation stops on an escape boundary so the result stays valid.
    CHECK_FALSE(Str::JsonEscape(buf, 4, TEXT("ab\"cd")));
    CHECK_EQ_STR(buf, TEXT("ab"));

    CHECK(Str::JsonEscape(buf, 128, NULL));
    CHECK_EQ_STR(buf, TEXT(""));
}

TEST_CASE("Str: UTF-8 round-trips and never emits a partial sequence")
{
    char utf8[64];
    TCHAR back[64];

    CHECK(Str::ToUtf8(utf8, 64, TEXT("hello")));
    CHECK(std::strcmp(utf8, "hello") == 0);
    CHECK(Str::FromUtf8(back, 64, utf8));
    CHECK_EQ_STR(back, TEXT("hello"));

    // Size query includes the NUL.
    CHECK_EQ_INT(Str::Utf8Size(TEXT("hello")), 6);
    CHECK_EQ_INT(Str::Utf8Size(TEXT("")), 1);
    CHECK_EQ_INT(Str::Utf8Size(NULL), 1);

    // Truncation is reported and terminated.
    CHECK_FALSE(Str::ToUtf8(utf8, 4, TEXT("hello")));
    CHECK(std::strlen(utf8) <= 3);

    // High-bit bytes survive a round trip unchanged.
    TCHAR high[8];
    high[0] = (TCHAR)0xC3;
    high[1] = (TCHAR)0xA9;
    high[2] = (TCHAR)'!';
    high[3] = 0;
    CHECK(Str::ToUtf8(utf8, 64, high));
    CHECK(Str::FromUtf8(back, 64, utf8));
    CHECK_EQ_STR(back, high);

    // Heap variants.
    char* alloc = Str::ToUtf8Alloc(TEXT("abc"));
    CHECK(alloc != NULL);
    CHECK(std::strcmp(alloc, "abc") == 0);
    TCHAR* wide = Str::FromUtf8Alloc(alloc);
    CHECK(wide != NULL);
    CHECK_EQ_STR(wide, TEXT("abc"));
    delete[] alloc;
    delete[] wide;

    CHECK(Str::ToUtf8(utf8, 64, NULL));
    CHECK_EQ_INT((int)std::strlen(utf8), 0);
}

TEST_CASE("Str: Buffer grows past any fixed size and builds valid JSON")
{
    // Regression: ToJson used a fixed new TCHAR[2048] and HttpClient a fixed
    // char[4096], both filled from unbounded input.
    Str::Buffer b;
    CHECK_EQ_INT(b.Length(), 0);
    CHECK_EQ_STR(b.Get(), TEXT(""));
    CHECK_FALSE(b.Failed());

    b.AppendChar((TCHAR)'{');
    b.AppendJsonPair(TEXT("name"), TEXT("Widget \"A\"\\B"));
    b.AppendChar((TCHAR)',');
    b.AppendJsonInt(TEXT("quantity"), 42);
    b.AppendChar((TCHAR)'}');

    CHECK_FALSE(b.Failed());
    CHECK_EQ_STR(b.Get(),
        TEXT("{\"name\":\"Widget \\\"A\\\"\\\\B\",\"quantity\":42}"));

    // Growth well past every fixed buffer the code used to rely on.
    Str::Buffer big;
    for (int i = 0; i < 5000; i++) {
        big.Append(TEXT("0123456789"));
    }
    CHECK_FALSE(big.Failed());
    CHECK_EQ_INT(big.Length(), 50000);
    CHECK(big.Get()[49999] == (TCHAR)'9');
    CHECK(big.Get()[50000] == 0);

    // Escaping stays correct across the internal chunk boundary.
    Str::Buffer chunked;
    TCHAR quotes[400];
    for (int i = 0; i < 399; i++) {
        quotes[i] = (TCHAR)'"';
    }
    quotes[399] = 0;
    CHECK(chunked.AppendJsonString(quotes));
    CHECK_FALSE(chunked.Failed());
    // 399 escaped quotes (2 chars each) plus the two surrounding quotes.
    CHECK_EQ_INT(chunked.Length(), 399 * 2 + 2);

    // Detach hands over ownership and leaves the buffer reusable.
    Str::Buffer d;
    d.Append(TEXT("owned"));
    TCHAR* taken = d.Detach();
    CHECK(taken != NULL);
    CHECK_EQ_STR(taken, TEXT("owned"));
    delete[] taken;
    CHECK_EQ_INT(d.Length(), 0);

    // Detaching an empty buffer still yields an owned empty string.
    Str::Buffer empty;
    TCHAR* none = empty.Detach();
    CHECK(none != NULL);
    CHECK_EQ_STR(none, TEXT(""));
    delete[] none;
}

TEST_CASE("Str: Dup and Length tolerate NULL")
{
    CHECK_EQ_INT(Str::Length(NULL), 0);
    CHECK_EQ_INT(Str::Length(TEXT("abcd")), 4);

    TCHAR* copy = Str::Dup(TEXT("abcd"));
    CHECK(copy != NULL);
    CHECK_EQ_STR(copy, TEXT("abcd"));
    delete[] copy;

    TCHAR* partial = Str::DupN(TEXT("abcdef"), 3);
    CHECK(partial != NULL);
    CHECK_EQ_STR(partial, TEXT("abc"));
    delete[] partial;

    TCHAR* empty = Str::Dup(NULL);
    CHECK(empty != NULL);
    CHECK_EQ_STR(empty, TEXT(""));
    delete[] empty;
}
